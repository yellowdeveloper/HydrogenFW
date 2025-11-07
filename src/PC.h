#ifndef PC_H
#define PC_H

// CONST BYTES
#define HEADER1 0x09
#define HEADER2 0x0D
#define HEADER3 0x09
#define HEADER4 0x0D

#define FOOTER1 0x27
#define FOOTER2 0x22
#define FOOTER3 0x27
#define FOOTER4 0x22

#define LPF_EN_TMP 2
#define LPF_EN 1
#define LPF_DIS 0


#define AVG_EN2 2
#define AVG_EN4 4
#define AVG_EN8 8
#define AVG_DIS 0

#define ALPHA 0.1

#define UART_NODE DT_NODELABEL(uart0)

extern uint8_t REC_CMD_BUF[9];

extern double prev_filtered;
extern int adc_flag;
extern uint8_t conf0_set; // default == gain 1
extern uint8_t conf1_set; // default == dr 20
extern struct k_sem uart_rec_semaphore;
extern uint8_t now_command;
extern uint8_t lpf_stat;
extern uint8_t af_stat;

extern void rx_enable_pc_uart();
extern void callback_set_pc_uart();
extern uint8_t read_cmd(const uint8_t *received_buff, uint32_t size);
extern uint8_t process_cmd(uint8_t cmd, struct k_sem *adc_loop_sem);
extern void queue_init_for_pc(uint8_t samp);
extern double LPF(int32_t current_raw);
extern double AF(double current);
extern int uart_send_pc(const uint8_t *buf, uint32_t size);
#endif