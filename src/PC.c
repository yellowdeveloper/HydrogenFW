#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include "PC.h"
#include "FIFO.h"
#include "COMMON_SEM.h"
#include "fs_transfer.h"

static const struct device *uart = DEVICE_DT_GET(UART_NODE);
int32_t prev_filtered = 0;
int adc_flag;
uint8_t conf0_set = 0x50, conf1_set = 0xA8;
uint16_t now_command;
uint8_t saf_stat = SAF_DIS, lpf_stat = LPF_DIS, maf_stat = MAF_DIS;
bool edit_calib = false;
uint8_t REC_CMD_BUF[10], REC_CMD_BUF_NEXT[10];
uint8_t *next_buf = REC_CMD_BUF_NEXT;
/* Kept for compatibility with existing PC.h; editor now uses its private queue. */
char FS_REC_BUF[256];
Queue MOV_AVG_QUEUE, SAMP_AVG_QUEUE;
void rx_enable_pc_uart(void);

uint8_t process_cmd(uint16_t cmd, struct k_sem *adc_loop_sem) {
    k_sem_give(adc_loop_sem);
    if ((cmd >> 8) == 0xFA) {
        check_filter_cmd((uint8_t)cmd);
    } else if ((cmd >> 8) == 0xFD) {
        filter_disable_cmd((uint8_t)cmd);
    } else if ((cmd >> 8) == 0xCA) {
        check_control_cmd((uint8_t)cmd, adc_loop_sem);
    }
    return 0;
}

void check_control_cmd(uint8_t ctrl_cmd, struct k_sem *adc_loop_sem) {
    if (ctrl_cmd == 0x10) {
        adc_flag = 1;
        k_sem_give(adc_loop_sem);
    } else if (ctrl_cmd == 0x02) {
        adc_flag = 0;
    } else if ((ctrl_cmd & 0xF0) == 0x30 || (ctrl_cmd & 0xF0) == 0xA0 ||
               (ctrl_cmd & 0xF0) == 0x50) {
        conf0_set = ctrl_cmd;
    } else if ((ctrl_cmd & 0x1F) == 0x08) {
        conf1_set = ctrl_cmd;
    } else if (ctrl_cmd == 0xFE) {
        fs_editor_reset();
        edit_calib = true;
    }
}

void filter_disable_cmd(uint8_t c) {
    if (c == 0xFD) {
        lpf_stat = LPF_DIS;
    } else if (c == 0x2D) {
        maf_stat = MAF_DIS;
    } else if (c == 0x4D) {
        saf_stat = SAF_DIS;
    }
}

void check_filter_cmd(uint8_t c) {
    if (c == 0x0A || c == 0x6A || c == 0xAA || c == 0xCA || c == 0xEA) {
        switch (c) {
            case 0x0A:
                saf_stat = SAF_EN2;
                break;
            case 0x6A:
                saf_stat = SAF_EN4;
                break;
            case 0xAA:
                saf_stat = SAF_EN8;
                break;
            case 0xCA:
                saf_stat = SAF_EN16;
                break;
            case 0xEA:
                saf_stat = SAF_EN32;
                break;
        }
        queue_init(&SAMP_AVG_QUEUE, saf_stat);
    } else if (c == 0xFA) {
        lpf_stat = LPF_EN;
    } else if (c == 0x2A || c == 0x4A || c == 0x8A) {
        switch (c) {
            case 0x2A:
                maf_stat = MAF_EN2;
                break;
            case 0x4A:
                maf_stat = MAF_EN4;
                break;
            case 0x8A:
                maf_stat = MAF_EN8;
                break;
        }
        queue_init(&MOV_AVG_QUEUE, maf_stat);
    }
}

K_MUTEX_DEFINE(uart_tx_mutex);
K_SEM_DEFINE(uart_tx_completed, 0, 1);
static int uart_tx_result;
static uint8_t command_frame[10];
static size_t command_bytes_received;

/* Replace read_cmd. */
uint16_t read_cmd(const uint8_t *b, uint32_t size) {
    if (size != 10) {
        return 0;
    }
    if (b[0] != HEADER1 || b[1] != HEADER2 || b[2] != HEADER3 || b[3] != HEADER4 ||
        b[6] != FOOTER1 || b[7] != FOOTER2 || b[8] != FOOTER3 || b[9] != FOOTER4) {
        return 0;
    }
    return ((uint16_t)b[4] << 8) | b[5];
}

/* Replace uart_cb. No flash writes, waiting, or TX in this callback. */
static void uart_cb(const struct device *dev, struct uart_event *evt, void *user_data) {
    (void)dev;
    (void)user_data;
    if (evt->type == UART_RX_RDY) {
        const uint8_t *p = evt->data.rx.buf + evt->data.rx.offset;
        size_t len = evt->data.rx.len;
        if (edit_calib) {
            fs_editor_rx(p, len);
            return;
        }
        /* UART event length is not guaranteed to equal a 10-byte command. */
        for (size_t i = 0; i < len; ++i) {
            command_frame[command_bytes_received++] = p[i];
            if (command_bytes_received == 10) {
                uint16_t c = read_cmd(command_frame, 10);
                if (c) {
                    command_bytes_received = 0;
                    now_command = c;
                    k_sem_give(&rec_semaphore);
                } else {
                    memmove(command_frame, command_frame + 1, 9);
                    command_bytes_received = 9;
                }
            }
        }
    } else if (evt->type == UART_RX_BUF_REQUEST) {
        uart_rx_buf_rsp(uart, next_buf, sizeof(REC_CMD_BUF_NEXT));
    } else if (evt->type == UART_RX_BUF_RELEASED) {
        next_buf = evt->data.rx_buf.buf;
    } else if (evt->type == UART_RX_DISABLED) {
        rx_enable_pc_uart();
    } else if (evt->type == UART_TX_DONE || evt->type == UART_TX_ABORTED) {
        uart_tx_result = evt->type == UART_TX_DONE ? 0 : -EIO;
        k_sem_give(&uart_tx_completed);
    }
}

/* Replace uart_send_pc; add the two helpers below before filter_adjust.
   This is thread-only. Buffer remains valid until UART_TX_DONE/ABORTED. */
static int uart_transmit_and_wait_locked(const uint8_t *buf, uint32_t size) {
    if (!size) {
        return 0;
    }
    k_sem_reset(&uart_tx_completed);
    uart_tx_result = -EIO;
    int rc = uart_tx(uart, buf, size, SYS_FOREVER_US);
    if (rc) {
        return rc;
    }
    /* UART at 115200 with HW flow control disabled. Wait for buffer release.
       Never return on a timer while the driver may still reference buf. */
    k_sem_take(&uart_tx_completed, K_FOREVER);
    return uart_tx_result;
}

int uart_send_pc(const uint8_t *buf, uint32_t size) {
    k_mutex_lock(&uart_tx_mutex, K_FOREVER);
    int rc = uart_transmit_and_wait_locked(buf, size);
    k_mutex_unlock(&uart_tx_mutex);
    return rc;
}

static int uart_send_sensor_pc(const uint8_t *buf, uint32_t size) {
    k_mutex_lock(&uart_tx_mutex, K_FOREVER);
    /* Recheck under TX lock, so ADC packets cannot follow EDITOR READY. */
    int rc = edit_calib ? 0 : uart_transmit_and_wait_locked(buf, size);
    k_mutex_unlock(&uart_tx_mutex);
    return rc;
}

void rx_enable_pc_uart(void) {
    int ret = uart_rx_enable(uart, REC_CMD_BUF, sizeof(REC_CMD_BUF), 1000);
    if (ret) {
        printf("ERROR setting rx event: %d\n", ret);
    }
}

void callback_set_pc_uart(void) {
    int ret = uart_callback_set(uart, uart_cb, NULL);
    if (ret) {
        printf("ERROR setting callback: %d\n", ret);
    }
}

int uart_abort_tx(void) {
    return uart_tx_abort(uart);
}

int32_t SAF(int32_t current, uint8_t samp) {
    if (is_queue_full(&SAMP_AVG_QUEUE)) {
        queue_init(&SAMP_AVG_QUEUE, samp);
    }
    enqueue(&SAMP_AVG_QUEUE, current);
    if (SAMP_AVG_QUEUE.count == samp) {
        return queue_content_sum(&SAMP_AVG_QUEUE) / samp;
    }
    return 0;
}

int32_t LPF(int32_t current) {
    int32_t value = ALPHA * (double)current + (1.0 - ALPHA) * (double)prev_filtered;
    prev_filtered = value;
    return value;
}

int32_t MAF(int32_t current) {
    if (is_queue_full(&MOV_AVG_QUEUE)) {
        dequeue(&MOV_AVG_QUEUE);
    }
    enqueue(&MOV_AVG_QUEUE, current);
    return (double)queue_content_sum(&MOV_AVG_QUEUE) / (double)MOV_AVG_QUEUE.count;
}

void add_crc(uint8_t *dst, uint8_t *src, size_t size) {
    uint8_t crc = 0;
    for (size_t i = 0; i < size; i++) {
        crc ^= src[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
    }
    memcpy(dst + 5 + size, &crc, 1);
}

int filter_adjust(uint8_t *sendbuff, uint32_t size, uint8_t *filterbuff) {
    uint32_t len = size - 10;
    sendbuff[0] = HEADER1;
    sendbuff[1] = HEADER2;
    sendbuff[2] = HEADER3;
    sendbuff[3] = HEADER4;
    sendbuff[4] = (uint8_t)len;
    memcpy(sendbuff + 5, filterbuff, len);
    add_crc(sendbuff, filterbuff, len);
    sendbuff[size - 4] = FOOTER1;
    sendbuff[size - 3] = FOOTER2;
    sendbuff[size - 2] = FOOTER3;
    sendbuff[size - 1] = FOOTER4;
    int ret = uart_send_sensor_pc(sendbuff, size);
    int bt_ret = hydro_notify_data(sendbuff, size);
    return ret ? ret : bt_ret;
}
