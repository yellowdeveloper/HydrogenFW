#include <stdio.h>
#include "PC.h"
#include "FIFO.h"
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

static const struct device *uart = DEVICE_DT_GET(UART_NODE);

int32_t prev_filtered = 0;
int adc_flag;
uint8_t conf0_set = 0x30; // default == gain 1
uint8_t conf1_set = 0xA8; // default == dr 600, Conversion Mode == Continuous
uint16_t now_command;
uint8_t saf_stat = SAF_DIS;
uint8_t lpf_stat = LPF_DIS;
uint8_t maf_stat = MAF_DIS;

uint8_t REC_CMD_BUF[10];
Queue MOV_AVG_QUEUE;
Queue SAMP_AVG_QUEUE;

K_SEM_DEFINE(uart_rec_semaphore, 0, 1);

uint16_t read_cmd(const uint8_t *received_buff, uint32_t size) {
    if (received_buff[0] != HEADER1 ||
        received_buff[1] != HEADER2 ||
        received_buff[2] != HEADER3 ||
        received_buff[3] != HEADER4 )
        return 0x0000;
    
    if (received_buff[6] != FOOTER1 ||
        received_buff[7] != FOOTER2 ||
        received_buff[8] != FOOTER3 ||
        received_buff[9] != FOOTER4 )
        return 0x0000;

    return ((received_buff[4] & 0xFF) << 8) | received_buff[5];
}

uint8_t process_cmd(uint16_t cmd, struct k_sem *adc_loop_sem) {
    k_sem_give(adc_loop_sem);
    if (((cmd >> 8) & 0xFF) == 0xFA) { // == Filter Enable Command
        uint8_t filter_cmd = cmd & 0xFF;

        check_filter_cmd(filter_cmd);
    }

    else if (((cmd >> 8) & 0xFF) == 0xFD) { // == Filter Disable Command
        uint8_t filter_cmd = cmd & 0xFF;

        filter_disable_cmd(filter_cmd);
    }

    else if (((cmd >> 8) & 0xFF) == 0xCA) { // == Control Command
        uint8_t ctrl_cmd = cmd & 0xFF;

        check_control_cmd(ctrl_cmd, adc_loop_sem);
    }
}

void check_control_cmd(uint8_t ctrl_cmd, struct k_sem *adc_loop_sem) {
    if (ctrl_cmd == 0x10) {
        k_sem_give(adc_loop_sem);
        adc_flag = 1;
    }

    else if (ctrl_cmd == 0x02)
        adc_flag = 0;

    else if ((ctrl_cmd & 0xF0) == 0x30)
        conf0_set = ctrl_cmd;

    else if ((ctrl_cmd & (0x1F)) == 0x08)
        conf1_set = ctrl_cmd;
}

void filter_disable_cmd(uint8_t filter_cmd) {
    if (filter_cmd == 0xFD) {
        lpf_stat = LPF_DIS;
    }

    else if (filter_cmd == 0x2D) {
        maf_stat = MAF_DIS;
    }

    else if (filter_cmd == 0x4D) {
        saf_stat = SAF_DIS;
    }
}

void check_filter_cmd(uint8_t filter_cmd) {
    if (filter_cmd == 0x0A || filter_cmd == 0x6A || filter_cmd == 0xAA || filter_cmd == 0xCA || filter_cmd == 0xEA) {
        switch (filter_cmd)
        {
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
            default:
                break;
        }
        queue_init(&SAMP_AVG_QUEUE, saf_stat);
    }

    else if (filter_cmd == 0xFA) {
        lpf_stat = LPF_EN;
    }

    else if (filter_cmd == 0x2A || filter_cmd == 0x4A || filter_cmd == 0x8A) {
        switch (filter_cmd)
        {
        case 0x2A:
            maf_stat = MAF_EN2;
            break;
        case 0x4A:
            maf_stat = MAF_EN4;
            break;
        case 0x8A:
            maf_stat = MAF_EN8;
            break;
        default:
            break;
        }
        queue_init(&MOV_AVG_QUEUE, maf_stat);
    }
}

static void uart_cb(const struct device *dev, struct uart_event *evt, void *user_data) {
    if (evt->type == UART_RX_RDY) {
        uint16_t cmd = read_cmd(REC_CMD_BUF, sizeof(REC_CMD_BUF));
        now_command = cmd;

        if (cmd != 0) {
            k_sem_give(&uart_rec_semaphore);
        }
    }
    else if (evt->type == UART_RX_DISABLED) {
            rx_enable_pc_uart();
            //rx enable
            //semaphor take
    }
}

void rx_enable_pc_uart() {
    int ret = uart_rx_enable(uart, REC_CMD_BUF, sizeof(REC_CMD_BUF), 1000);
	if (ret != 0) {
		printf("ERROR setting rx event");
	}
}

void callback_set_pc_uart() {
    int ret = uart_callback_set(uart, uart_cb, NULL);
	if (ret != 0) {
		printf("ERROR setting callback");
	}
}

int32_t SAF(int32_t current, uint8_t samp) {
    if (is_queue_full(&SAMP_AVG_QUEUE)) { queue_init(&SAMP_AVG_QUEUE, samp); }
    enqueue(&SAMP_AVG_QUEUE, current);
    if (SAMP_AVG_QUEUE.count == samp) {
        int32_t samp_avg_filtered = queue_content_sum(&SAMP_AVG_QUEUE) / samp;
        return samp_avg_filtered;
    }
    return 0;
}

int32_t LPF(int32_t current) {
    int32_t lp_filtered = ALPHA * (double)current + (1.0-ALPHA) * (double)prev_filtered;
    prev_filtered = lp_filtered;
    return lp_filtered;
}

int32_t MAF(int32_t current) {
    if (is_queue_full(&MOV_AVG_QUEUE)) { dequeue(&MOV_AVG_QUEUE); }
    enqueue(&MOV_AVG_QUEUE, current);
    int32_t mov_avg_filtered = (double)queue_content_sum(&MOV_AVG_QUEUE) / (double)MOV_AVG_QUEUE.count;
    return mov_avg_filtered;
}

int uart_send_pc(const uint8_t *buf, uint32_t size) {
    int ret;
    ret = uart_tx(uart, buf, size, SYS_FOREVER_US);
    return ret;
}

int filter_adjust(uint8_t *sendbuff, uint32_t sendbuff_size, uint8_t *filterbuff, uint32_t filterbuff_size) {
    int ret;

    sendbuff[0] = HEADER1;
    sendbuff[1] = HEADER2;
    sendbuff[2] = HEADER3;
    sendbuff[3] = HEADER4;

    for (int i= 4; i< sendbuff_size - 4; i++) {
        sendbuff[i] = filterbuff[i - 4];
    }

    sendbuff[sendbuff_size - 4]  = FOOTER1;
    sendbuff[sendbuff_size - 3]  = FOOTER2;
    sendbuff[sendbuff_size - 2]  = FOOTER3;
    sendbuff[sendbuff_size - 1]  = FOOTER4;

	ret = uart_send_pc(sendbuff, sendbuff_size);
    ret = hydro_notify_data(sendbuff, sendbuff_size);
	return ret;
}