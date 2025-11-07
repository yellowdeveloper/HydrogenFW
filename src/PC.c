#include <stdio.h>
#include "PC.h"
#include "FIFO.h"
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

static const struct device *uart = DEVICE_DT_GET(UART_NODE);

double prev_filtered = 0.0;
int adc_flag;
uint8_t conf0_set = 0x30; // default == gain 1
uint8_t conf1_set = 0x00; // default == dr 20
uint8_t now_command;
uint8_t lpf_stat = LPF_DIS;
uint8_t af_stat = AVG_DIS;

uint8_t REC_CMD_BUF[9];
Queue AVG_SAMP_QUEUE;

K_SEM_DEFINE(uart_rec_semaphore, 0, 1);

uint8_t read_cmd(const uint8_t *received_buff, uint32_t size) {
    if (received_buff[0] != HEADER1 ||
        received_buff[1] != HEADER2 ||
        received_buff[2] != HEADER3 ||
        received_buff[3] != HEADER4 )
        return 0;
    
    if (received_buff[5] != FOOTER1 ||
        received_buff[6] != FOOTER2 ||
        received_buff[7] != FOOTER3 ||
        received_buff[8] != FOOTER4 )
        return 0;

    return received_buff[4];
}

uint8_t process_cmd(uint8_t cmd, struct k_sem *adc_loop_sem) {
    if (cmd == 0x10) {
        k_sem_give(adc_loop_sem);
        adc_flag = 1;
    }
    else if (cmd == 0x02)
        adc_flag = 0;

    else if ((cmd & 0xF0) == 0x30)
        conf0_set = cmd;

    else if (cmd != 0 && (cmd & (0x1F)) == 0x00)
        conf1_set = cmd;

    else if (cmd == 0xFA)
        lpf_stat = LPF_EN;

    else if (cmd == 0x2A || cmd == 0x4A || cmd == 0x8A) {
        if (lpf_stat == LPF_DIS) lpf_stat = LPF_EN_TMP;
        switch (cmd)
        {
        case 0x2A:
            af_stat = AVG_EN2;
            break;

        case 0x4A:
            af_stat = AVG_EN4;
            break;

        case 0x8A:
            af_stat = AVG_EN8;
            break;
        default:
            break;
        }
        queue_init_for_pc(af_stat);
    }

    else if (cmd == 0xFD) {
        if (af_stat) af_stat = AVG_DIS;
        lpf_stat = LPF_DIS;
    }

    else if (cmd == 0x2D) {
        if (lpf_stat == LPF_EN_TMP) lpf_stat = LPF_DIS;
        af_stat = AVG_DIS;
    }    
        
}

static void uart_cb(const struct device *dev, struct uart_event *evt, void *user_data) {
    if (evt->type == UART_RX_RDY) {
        int cmd = read_cmd(REC_CMD_BUF, sizeof(REC_CMD_BUF));
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

extern void queue_init_for_pc(uint8_t samp) {
    queue_init(&AVG_SAMP_QUEUE, samp);
}

double LPF(int32_t current_raw) {
    double lp_filtered = ALPHA * (double)current_raw + (1.0-ALPHA) * prev_filtered;
    prev_filtered = lp_filtered;
    return lp_filtered;
}

double AF(double current) {
    if (is_queue_full(&AVG_SAMP_QUEUE)) { dequeue(&AVG_SAMP_QUEUE); }
    enqueue(&AVG_SAMP_QUEUE, current);
    double avg_filtered = queue_content_sum(&AVG_SAMP_QUEUE) / (double)AVG_SAMP_QUEUE.count;
    return avg_filtered;
}

int uart_send_pc(const uint8_t *buf, uint32_t size) {
    int ret;
    ret = uart_tx(uart, buf, size, SYS_FOREVER_US);
    return ret;
}