#include "fs_transfer.h"
#include "PC.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>

#define TRANSFER_BLOCK_SIZE 64
#define BLOCK_RECEIVE_TIMEOUT_MS 10000
K_MSGQ_DEFINE(editor_rx_queue, sizeof(uint8_t), 512, 1);
static atomic_t editor_rx_overflow;

void fs_editor_reset(void) {
    k_msgq_purge(&editor_rx_queue);
    atomic_clear(&editor_rx_overflow);
}

void fs_editor_rx(const uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (k_msgq_put(&editor_rx_queue, &data[i], K_NO_WAIT)) {
            atomic_set(&editor_rx_overflow, 1);
        }
    }
}

static int receive_exact_bytes(void *out, size_t length, int timeout_ms) {
    uint8_t *p = out;
    int64_t deadline_ms = timeout_ms < 0 ? 0 : k_uptime_get() + timeout_ms;
    for (size_t i = 0; i < length; ++i) {
        if (atomic_get(&editor_rx_overflow)) {
            return -ENOBUFS;
        }
        int64_t remaining_bytes = timeout_ms < 0 ? 0 : deadline_ms - k_uptime_get();
        if (timeout_ms >= 0 && remaining_bytes <= 0) {
            return -ETIMEDOUT;
        }
        if (k_msgq_get(&editor_rx_queue, p + i,
                       timeout_ms < 0 ? K_FOREVER : K_MSEC(remaining_bytes))) {
            return -ETIMEDOUT;
        }
    }
    return atomic_get(&editor_rx_overflow) ? -ENOBUFS : 0;
}

static int receive_command_line(char *text, size_t capacity, int timeout_ms) {
    size_t n = 0;
    uint8_t c;
    for (;;) {
        int rc = receive_exact_bytes(&c, 1, timeout_ms);
        if (rc) {
            return rc;
        }
        if (c == '\n') {
            text[n] = 0;
            return 0;
        }
        if (c == '\r') {
            continue;
        }
        if (!c || n + 1 >= capacity) {
            return -EINVAL;
        }
        text[n++] = (char)c;
    }
}

/* uart_send_pc must be the synchronous, serialized replacement in uart_replacements.c.txt. */
static int send_response(const char *format, ...) {
    char text[256];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (n < 0 || n >= (int)sizeof(text)) {
        return -EINVAL;
    }
    return uart_send_pc((const uint8_t *)text, (uint32_t)n);
}

static int build_file_path(struct fs_mount_t *mountpoint, const char *name, char *out,
                           size_t capacity) {
    if (!*name || strstr(name, "..") || strchr(name, '/') || strchr(name, '\\') ||
        strchr(name, '\r') || strchr(name, '\n') || strstr(name, ".upload")) {
        return -EINVAL;
    }
    int n = snprintf(out, capacity, "%s/%s", mountpoint->mnt_point, name);
    return n < 0 || n >= (int)capacity ? -ENAMETOOLONG : 0;
}

static int parse_upload_size(const char *text, uint32_t *result) {
    if (strncmp(text, "SIZE ", 5)) {
        return -EINVAL;
    }
    text += 5;
    if (!*text) {
        return -EINVAL;
    }
    uint32_t value = 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9' || value > (INT32_MAX - (unsigned)(*text - '0')) / 10U) {
            return -EINVAL;
        }
        value = value * 10U + (unsigned)(*text - '0');
    }
    *result = value;
    return 0;
}

/* Return nonzero on protocol failure: end session, do not parse remaining raw bytes as commands. */
/* 파일 수정: 길이 협상 → 블록 수신/ACK → 저장 확정 → DONE */
static int receive_file_upload(const char *path) {
    char text[64], temporary_path[192];
    struct fs_file_t file_handle;
    uint32_t total_bytes, received_bytes = 0;
    int rc;
    bool file_is_open = false;
    struct fs_dirent file_info;

    /* 대상 파일이 존재하는지 먼저 확인합니다. */
    rc = fs_stat(path, &file_info);
    if (rc || file_info.type != FS_DIR_ENTRY_FILE) {
        send_response("ERR target\n");
        return 0;
    }
    if (send_response("SIZE?\n")) {
        return -EIO;
    }
    rc = receive_command_line(text, sizeof(text), -1);
    if (rc) {
        return rc; /* User may be selecting a local file. */
    }
    if (!strcmp(text, "CANCEL")) {
        send_response("CANCELLED\n");
        return 0;
    }
    if (parse_upload_size(text, &total_bytes)) {
        send_response("ERR size\n");
        return -EINVAL;
    }
    int n = snprintf(temporary_path, sizeof(temporary_path), "%s.upload", path);
    if (n < 0 || n >= (int)sizeof(temporary_path)) {
        send_response("ERR path\n");
        return -EINVAL;
    }

    /* 원본을 유지한 채 임시 파일에 기록합니다. */
    fs_file_t_init(&file_handle);
    rc = fs_open(&file_handle, temporary_path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
    if (rc) {
        send_response("ERR open %d\n", rc);
        return rc;
    }
    file_is_open = true;
    rc = send_response("READY 64\n");
    if (rc != 0) {
        goto fail;
    }

    /* 각 블록을 기록한 뒤 누적 바이트 수로 ACK를 보냅니다. */
    while (received_bytes < total_bytes) {
        uint8_t buf[TRANSFER_BLOCK_SIZE];
        size_t block_length = total_bytes - received_bytes;
        if (block_length > TRANSFER_BLOCK_SIZE) {
            block_length = TRANSFER_BLOCK_SIZE;
        }
        rc = receive_exact_bytes(buf, block_length, BLOCK_RECEIVE_TIMEOUT_MS);
        if (rc != 0) {
            goto fail;
        }
        ssize_t bytes_written = fs_write(&file_handle, buf, block_length);
        if (bytes_written != (ssize_t)block_length) {
            rc = bytes_written < 0 ? (int)bytes_written : -EIO;
            goto fail;
        }
        received_bytes += (uint32_t)block_length;

        /* ACK = accepted by filesystem, not a power-loss durability guarantee. */
        rc = send_response("ACK %lu\n", (unsigned long)received_bytes);
        if (rc != 0) {
            goto fail;
        }
    }

    /* 모든 블록을 받은 후에만 원본을 교체합니다. */
    rc = fs_sync(&file_handle);
    if (rc) {
        goto fail;
    }
    rc = fs_close(&file_handle);
    file_is_open = false;
    if (rc) {
        goto fail;
    }
    rc = fs_rename(temporary_path, path);
    if (rc) {
        goto fail;
    }
    return send_response("DONE %lu\n", (unsigned long)received_bytes);

fail:

    /* 실패한 임시 파일을 정리하고 현재 전송 세션을 종료합니다. */
    if (file_is_open) {
        fs_close(&file_handle);
    }
    fs_unlink(temporary_path);
    send_response("ERR transfer %d\n", rc);
    return rc;
}

static int send_file_contents(const char *path) {
    struct fs_file_t file_handle;
    struct fs_dirent file_info;
    uint8_t buf[64];
    int rc;
    rc = fs_stat(path, &file_info);
    if (rc || file_info.type != FS_DIR_ENTRY_FILE) {
        return send_response("ERR target\n");
    }
    fs_file_t_init(&file_handle);
    rc = fs_open(&file_handle, path, FS_O_READ);
    if (rc) {
        return send_response("ERR open %d\n", rc);
    }
    rc = send_response("DATA %lu\n", (unsigned long)file_info.size);
    size_t remaining_bytes = file_info.size;
    while (!rc && remaining_bytes) {
        size_t block_length = remaining_bytes > sizeof(buf) ? sizeof(buf) : remaining_bytes;
        ssize_t bytes_read = fs_read(&file_handle, buf, block_length);
        if (bytes_read <= 0) {
            rc = -EIO;
            break;
        }
        rc = uart_send_pc(buf, (uint32_t)bytes_read);
        remaining_bytes -= (size_t)bytes_read;
    }
    int close_rc = fs_close(&file_handle);
    if (!rc) {
        rc = close_rc;
    }
    return rc;
}

static int send_directory_listing(struct fs_mount_t *mountpoint) {
    struct fs_dir_t directory;
    struct fs_dirent entry;
    int rc;
    fs_dir_t_init(&directory);
    rc = fs_opendir(&directory, mountpoint->mnt_point);
    if (rc) {
        return send_response("ERR dir %d\n", rc);
    }
    rc = send_response("LIST\n");
    while (!rc) {
        rc = fs_readdir(&directory, &entry);
        if (rc || !entry.name[0]) {
            break;
        }
        rc = send_response("%s %s %lu\n", entry.type == FS_DIR_ENTRY_DIR ? "DIR" : "FILE",
                           entry.name, (unsigned long)entry.size);
    }
    fs_closedir(&directory);
    if (rc) {
        return send_response("ERR list %d\n", rc);
    }
    return send_response("END\n");
}

void fs_editor_run(struct fs_mount_t *mountpoint) {
    char command[32], name[64], path[160];
    if (send_response("EDITOR READY\n")) {
        return;
    }
    for (;;) {
        if (receive_command_line(command, sizeof(command), -1)) {
            return;
        }
        if (!strcmp(command, "ext")) {
            send_response("BYE\n");
            return;
        }
        if (!strcmp(command, "ls")) {
            if (send_directory_listing(mountpoint)) {
                return;
            }
            continue;
        }
        if (strcmp(command, "mod") && strcmp(command, "add") && strcmp(command, "del") &&
            strcmp(command, "seek")) {
            if (send_response("ERR command\n")) {
                return;
            }
            continue;
        }
        if (send_response("NAME\n") || receive_command_line(name, sizeof(name), -1)) {
            return;
        }
        if (build_file_path(mountpoint, name, path, sizeof(path))) {
            send_response("ERR name\n");
            continue;
        }
        if (!strcmp(command, "mod")) {
            if (receive_file_upload(path)) {
                return;
            }
        } else if (!strcmp(command, "seek")) {
            if (send_file_contents(path)) {
                return;
            }
        } else if (!strcmp(command, "del")) {
            int rc = fs_unlink(path);
            if (send_response(rc ? "ERR delete %d\n" : "OK\n", rc)) {
                return;
            }
        } else {
            struct fs_file_t file_handle;
            fs_file_t_init(&file_handle);
            int rc = fs_open(&file_handle, path, FS_O_CREATE | FS_O_RDWR);
            if (!rc) {
                rc = fs_close(&file_handle);
            }
            if (send_response(rc ? "ERR create %d\n" : "OK\n", rc)) {
                return;
            }
        }
    }
}
