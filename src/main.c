/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>

#include "BD_DEF.h"
#include "ADC.h"
#include "DAC.h"
#include "PC.h"
#include "fs_transfer.h"
#include "BT.h"
#include "COMMON_SEM.h"

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS 100

/* Devicetree node identifiers */
#define LED0_NODE DT_ALIAS(led0)

/* Use dt node and declare entry ∵ Using automount in device tree */
#define PARTITION_NODE DT_NODELABEL(lfs1)
FS_FSTAB_DECLARE_ENTRY(PARTITION_NODE);

// mode 0 = run mode (전원 인가 시 바로 동작)
// mode 1 = test mode (UART 값 수신 시 동작)
#define RUN_MODE 0

struct fs_mount_t *mountpoint = &FS_FSTAB_ENTRY(PARTITION_NODE);

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

// buffer to receive data from ADC
uint8_t REC_DAT_TMP[3];
uint8_t GET_DAT_TMP[3];
uint8_t REC_REG_BUF_1[1];

// Buffer to send data to PC
uint8_t STRT_BUF[5] = {0x00, 0x00, 0x00, 0x00, 0xAA};
uint8_t SND_DAT_BUF[28];

K_MSGQ_DEFINE(adc_queue, sizeof(REC_DAT_TMP), 32, 4);

K_SEM_DEFINE(rec_semaphore, 0, 1);
K_SEM_DEFINE(fs_sem, 0, 1);

// Thread Resoures For ADC Test
K_SEM_DEFINE(adc_loop_semaphore, 0, 1);
K_THREAD_STACK_DEFINE(adc_loop_stack, 1024);
struct k_thread adc_loop_thread;

K_THREAD_STACK_DEFINE(adc_send_stack, 1024);
struct k_thread adc_send_thread;

// Thread Resources For Bluetooth Notify
K_SEM_DEFINE(bt_loop_semaphore, 0, 1);
K_THREAD_STACK_DEFINE(bt_loop_stack, 1024);
struct k_thread bt_loop_thread;

K_THREAD_STACK_DEFINE(bt_send_stack, 1024);
struct k_thread bt_send_thread;

char fs_info_send[4096];

int file_state = 0;
struct fs_file_t file;

static void init_one(const char *name, const char *initial) {
    char path[160];
    struct fs_file_t f;
    int n = snprintf(path, sizeof(path), "%s/%s", mountpoint->mnt_point, name);
    if (n < 0 || n >= (int)sizeof(path)) {
        return;
    }
    fs_file_t_init(&f);
    int rc = fs_open(&f, path, FS_O_CREATE | FS_O_RDWR);
    if (rc < 0) {
        printf("init open: %d\n", rc);
        return;
    }
    rc = fs_seek(&f, 0, FS_SEEK_END);
    if (rc == 0) {
        off_t size = fs_tell(&f);
        if (size == 0) {
            size_t len = strlen(initial);
            ssize_t written = fs_write(&f, initial, len);
            if (written != (ssize_t)len) {
                printf("init write failed: %zd/%zu\n", written, len);
            }
        } else if (size < 0) {
            printf("init tell failed\n");
        }
    } else {
        printf("init seek: %d\n", rc);
    }
    rc = fs_close(&f);
    if (rc < 0) {
        printf("init close: %d\n", rc);
    }
}

static void init_file_create(void) {
    init_one("min_max", "0 8388608");
    init_one("table", "1 1 2 2 3 3 4 4 5 5 6 6 7 7 8 8 9 9 10 10");
}

static void load_min_max_calib(void) {
    struct fs_file_t f;
    char path[128];
    char buf[64];
    char extra;
    int new_min;
    int new_max;

    snprintf(path, sizeof(path), "%s/min_max", mountpoint->mnt_point);

    fs_file_t_init(&f);

    if (fs_open(&f, path, FS_O_READ) < 0) {
        return;
    }

    ssize_t len = fs_read(&f, buf, sizeof(buf) - 1);
    fs_close(&f);

    if (len <= 0 || len >= (ssize_t)(sizeof(buf) - 1)) {
        return;
    }

    buf[len] = '\0';

    if (sscanf(buf, "%d %d %c", &new_min, &new_max, &extra) != 2) {
        return;
    }

    if (new_max <= new_min) {
        return;
    }

    k_sched_lock();
    min_calib = new_min;
    max_calib = new_max;
    k_sched_unlock();
}

// reset pin and initialize bluetooth
int init_set() {
    int ret;

    if (!gpio_is_ready_dt(&led0)) {
        return 0;
    }

    ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        return 0;
    }

    // Bluetooth Initialization
    ret = hydro_bt_enable();
    if (ret < 0) {
        printf("Failed to Enable Bluetooth\n");
        return 0;
    }

    // GATT Callbacks Enable
    enable_gatt_callbacks();

    // BlueTooth Advertising Start
    ret = hydro_adv_start();
    if (ret < 0) {
        printf("Failed to Start Advertising\n");
        return 0;
    }
}

int adc_loop_thread_entry() {
    while (1) {
        if (k_sem_take(&adc_loop_semaphore, K_FOREVER) == 0 && adc_flag == 1) {
            int ret;
            ret = adc_write((uint8_t[]){CMD_START}, 1);
            if (ret != 0) {
                continue;
            }

            while (adc_flag == 1) {
                // adc_test(); // to print debug message
                adc_read_value();
            }

            ret = adc_write((uint8_t[]){CMD_PW_DONW}, 1);
            if (ret != 0) {
                continue;
            }
        }
    }
}

int adc_send_thread_entry() {
    while (1) {
        adc_send_value();
    }
}

int bt_send_thread_entry() {
    while (1) {
        hydro_notify_data();

        k_msleep(100);
    }
}

int check_filter(int32_t dc) {
    int ret;
    int32_t last_filtered = dc;
    uint8_t inserted = 0;
    uint32_t size = 10;

    if (saf_stat) {
        last_filtered = SAF(last_filtered, saf_stat);

        if (last_filtered == 0) {
            return 0;
        }
    }

    unsigned char byteArray[6 + sizeof(int32_t) * 3];
    memcpy(byteArray, GET_DAT_TMP, 3);
    inserted += 3;

    if (saf_stat) {
        byteArray[inserted] = FILTER0;
        memcpy(byteArray + inserted + 1, &last_filtered, sizeof(int32_t));
        inserted += 5;
    }

    if (lpf_stat) {
        last_filtered = LPF(last_filtered);

        byteArray[inserted] = FILTER1;
        memcpy(byteArray + inserted + 1, &last_filtered, sizeof(int32_t));
        inserted += 5;
    }

    if (maf_stat) {
        last_filtered = MAF(last_filtered);

        byteArray[inserted] = FILTER2;
        memcpy(byteArray + inserted + 1, &last_filtered, sizeof(int32_t));
        inserted += 5;
    }
    size += inserted;

    uint32_t dac_code = conv_16bit_count(last_filtered);
    uint8_t tx_data[3];
    tx_data[0] = 0x00;
    tx_data[1] = (dac_code >> 8) & 0xFF;
    tx_data[2] = dac_code & 0xFF;

    dac_write(tx_data);

    if (edit_calib) {
        return 0;
    }

    ret = filter_adjust(SND_DAT_BUF, size, byteArray);
    return ret;
}

void adc_read_value() {
    int ret;
    int cnt = 0;

    // int sleep_time = 1000 / data_rate;
    // k_msleep(sleep_time);

    do {
        cnt++;
        adc_read_reg(CONF2_REG);
        adc_receive(REC_REG_BUF_1, sizeof(REC_REG_BUF_1));

        if (cnt == ADC_TIMEOUT) {
            return;
        }

        if (!(REC_REG_BUF_1[0] & 0x80)) {
            k_yield();
        }
    } while (!(REC_REG_BUF_1[0] & 0x80));

    // ret = adc_read_write(REC_DAT_BUF, sizeof(REC_DAT_BUF), (uint8_t[]){ CMD_RDATA }, 1);
    // if (ret != 0) return;

    ret = adc_write((uint8_t[]){CMD_RDATA}, 1);
    if (ret != 0) {
        return;
    }
    ret = adc_receive(REC_DAT_TMP, sizeof(REC_DAT_TMP));
    if (ret != 0) {
        return;
    }

    if (k_msgq_put(&adc_queue, REC_DAT_TMP, K_NO_WAIT) != 0) {
    }
}

void adc_send_value() {
    int ret;

    if (k_msgq_get(&adc_queue, GET_DAT_TMP, K_FOREVER) != 0) {
    } else {
        get_digital_count(GET_DAT_TMP);

        ret = check_filter(digital_count);
        if (ret != 0) {
            return;
        }
    }

    gpio_pin_toggle_dt(&led0);
}

int main(void) {
    // PROGRAM START NOTICE
    init_set();
    init_file_create();
    load_min_max_calib();

    printf("HFCLKSTAT = 0x%08x\n", NRF_CLOCK->HFCLKSTAT);
    printf("LFCLKSTAT = 0x%08x\n", NRF_CLOCK->LFCLKSTAT);

    k_thread_create(&adc_loop_thread, adc_loop_stack, K_THREAD_STACK_SIZEOF(adc_loop_stack),
                    adc_loop_thread_entry, NULL, NULL, NULL, 5, 0, K_NO_WAIT);

    k_thread_create(&adc_send_thread, adc_send_stack, K_THREAD_STACK_SIZEOF(adc_send_stack),
                    adc_send_thread_entry, NULL, NULL, NULL, 5, 0, K_NO_WAIT);

    // k_thread_create(&bt_send_thread, bt_send_stack, K_THREAD_STACK_SIZEOF(bt_send_stack),
    // bt_send_thread_entry, NULL, NULL, NULL, 5, 0, K_NO_WAIT);

    callback_set_pc_uart();
    rx_enable_pc_uart();

    // uart_send_pc(STRT_BUF, sizeof(STRT_BUF));

    if (bd_init() != 0) {
        return 0;
    }
    for (int i = 0; i <= 3; i++) {
        gpio_pin_toggle_dt(&led0);
        k_msleep(200);
        gpio_pin_set_dt(&led0, 0);
    }

    // adc_reset
    adc_write((uint8_t[]){CMD_RESET}, 1);

    if (RUN_MODE == 0) {
        adc_flag = 1;

        conf0_set = 0x54;
        conf1_set = 0xA8;

        check_filter_cmd(0xAA);
        check_filter_cmd(0xFA);
        check_filter_cmd(0x4A);

        adc_write_conf0(conf0_set);
        adc_write_conf1(conf1_set);
    }

    k_msleep(5);

    while (1) {
        // TEST DAC
        // dac_write();
        // read_xtr_err();
        if (edit_calib) {
            k_msgq_purge(&adc_queue);
            fs_editor_run(mountpoint);
            load_min_max_calib();
            file_state = 0;
            edit_calib = false;
            continue;
        }

        if (RUN_MODE == 0) {
            k_sem_give(&adc_loop_semaphore);
        }

        if (k_sem_take(&rec_semaphore, K_FOREVER) == 0) {
            process_cmd(now_command, &adc_loop_semaphore);

            adc_write_conf0(conf0_set);
            adc_write_conf1(conf1_set);
        }
        // now_command = 0;
        // memset(REC_CMD_BUF, 0, sizeof(REC_CMD_BUF));
    }
}