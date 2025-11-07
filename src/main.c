/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include "ADC.h"
#include "PC.h"

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   100

/* Devicetree node identifiers */
#define LED0_NODE DT_ALIAS(led0)
#define UART_NODE DT_NODELABEL(uart0)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
//static const struct device *uart = DEVICE_DT_GET(UART_NODE);

// buffer to receive data from ADC
uint8_t REC_DAT_BUF[3];
uint8_t REC_REG_BUF_1[1];

// Buffer to send data to PC
uint8_t SND_DAT_BUF_RAW[11];
uint8_t SND_DAT_BUF_LPF[19];
uint8_t SND_DAT_BUF_AVG[27];

// Thread Resoures For ADC Test
K_SEM_DEFINE(adc_loop_semaphore, 0, 1);
K_THREAD_STACK_DEFINE(adc_loop_stack, 1024);
struct k_thread adc_loop_thread;

// reset gpio pins
int reset_pin() {
	int ret;

	if (!gpio_is_ready_dt(&led0)) return 0; 

	ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) return 0;
}

int adc_loop_thread_entry() {
	while(1) {
		if (k_sem_take(&adc_loop_semaphore, K_FOREVER) == 0 && adc_flag == 1) {
			while(adc_flag == 1){
				//adc_test(); // to print debug message
				adc_send_value();
			}
		}
	}
}

void adc_test() {
    // WRITE RESET COMMAND TO ADC
	printf("\n**************************************************\n");
	printf("\n- Reset ADC\n");
	adc_write((uint8_t[]){ CMD_RESET }, 1);


	// WRITE DATA REGISTER (CONFIGURATION REGISTER 0)
	printf("\n- Change Configuration Register 0 :: %02X\n", conf0_set);
	adc_write_conf0(conf0_set);

	printf("\n- Change Configuration Register 1 :: %02X\n", conf1_set);
	adc_write_conf1(conf1_set);

	// READ DATA REGISTER (CONFIGURATION REGISTER 0)
	printf("\n- Read Configuration Register 0\n");
	adc_read_reg(CONF0_REG);
	adc_receive(REC_REG_BUF_1, sizeof(REC_REG_BUF_1));
	

	// WRITE START DATA CONVERSION COMMAND TO ADC
	printf("\n- Send Read Command to ADC\n");
	adc_write((uint8_t[]){ CMD_START }, 1);
	
	int sleep_time = 1000 / data_rate;
	printf("\n- Data Rate :: %d wait for %d ms\n", data_rate, sleep_time);
	k_msleep(sleep_time);

	// READ DATA REGISTER (CONFIGURATION REGISTER 3) :: TO CHECK DRDY VALUE
	adc_read_reg(CONF2_REG);
	adc_receive(REC_REG_BUF_1, sizeof(REC_REG_BUF_1));


	// READ DATA FROM ADC
	printf("\n- Read Data From ADC\n");
	adc_write((uint8_t[]){ CMD_RDATA }, 1);
	adc_receive(REC_DAT_BUF, sizeof(REC_DAT_BUF));

	// SET RESULT VALUE AND PRINT
	get_digital_count(REC_DAT_BUF); 
	double v_in = get_actual_voltage(digital_count);
	printf("Gain Set To: 2^%d\n", pga_gain);
	printf("Digital Count: %02X: %d\n", digital_count, digital_count);
	LPF(digital_count);
	double avg_filtered = AF(prev_filtered);
	printf("DC After LPF: %lf\n", prev_filtered);
	printf("DC After AF: %lf\n", avg_filtered);
	printf("Voltage in = %lf\n", v_in);
	printf("\n**************************************************\n");

	// NOTICE USING LED
	gpio_pin_toggle_dt(&led0);

	// READ DATA END
}

int filter_adjust() {
	int ret;
	if (af_stat) {
		double lp_filtered = LPF(digital_count);
		double avg_filtered = AF(lp_filtered);

		unsigned char byteArray[sizeof(double)*2];
		memcpy(byteArray, &lp_filtered, sizeof(double));
		memcpy(byteArray + sizeof(double), &avg_filtered, sizeof(double));

		SND_DAT_BUF_AVG[0] = HEADER1;
		SND_DAT_BUF_AVG[1] = HEADER2;
		SND_DAT_BUF_AVG[2] = HEADER3;
		SND_DAT_BUF_AVG[3] = HEADER4;

		SND_DAT_BUF_AVG[4] = REC_DAT_BUF[0];
		SND_DAT_BUF_AVG[5] = REC_DAT_BUF[1];
		SND_DAT_BUF_AVG[6] = REC_DAT_BUF[2];

		SND_DAT_BUF_AVG[7]  = byteArray[0];
		SND_DAT_BUF_AVG[8]  = byteArray[1];
		SND_DAT_BUF_AVG[9]  = byteArray[2];
		SND_DAT_BUF_AVG[10] = byteArray[3];
		SND_DAT_BUF_AVG[11] = byteArray[4];
		SND_DAT_BUF_AVG[12] = byteArray[5];
		SND_DAT_BUF_AVG[13] = byteArray[6];
		SND_DAT_BUF_AVG[14] = byteArray[7];

		SND_DAT_BUF_AVG[15]  = byteArray[8];
		SND_DAT_BUF_AVG[16]  = byteArray[9];
		SND_DAT_BUF_AVG[17]  = byteArray[10];
		SND_DAT_BUF_AVG[18] = byteArray[11];
		SND_DAT_BUF_AVG[19] = byteArray[12];
		SND_DAT_BUF_AVG[20] = byteArray[13];
		SND_DAT_BUF_AVG[21] = byteArray[14];
		SND_DAT_BUF_AVG[22] = byteArray[15];

		SND_DAT_BUF_AVG[23]  = FOOTER1;
		SND_DAT_BUF_AVG[24]  = FOOTER2;
		SND_DAT_BUF_AVG[25]  = FOOTER3;
		SND_DAT_BUF_AVG[26]  = FOOTER4;

		ret = uart_send_pc(SND_DAT_BUF_AVG, sizeof(SND_DAT_BUF_AVG));
		return ret;
	}

	if (lpf_stat) {

		double lp_filtered = LPF(digital_count);

		unsigned char byteArray[sizeof(double)];
		memcpy(byteArray, &lp_filtered, sizeof(double));

		SND_DAT_BUF_LPF[0] = HEADER1;
		SND_DAT_BUF_LPF[1] = HEADER2;
		SND_DAT_BUF_LPF[2] = HEADER3;
		SND_DAT_BUF_LPF[3] = HEADER4;

		SND_DAT_BUF_LPF[4] = REC_DAT_BUF[0];
		SND_DAT_BUF_LPF[5] = REC_DAT_BUF[1];
		SND_DAT_BUF_LPF[6] = REC_DAT_BUF[2];

		SND_DAT_BUF_LPF[7]  = byteArray[0];
		SND_DAT_BUF_LPF[8]  = byteArray[1];
		SND_DAT_BUF_LPF[9]  = byteArray[2];
		SND_DAT_BUF_LPF[10] = byteArray[3];
		SND_DAT_BUF_LPF[11] = byteArray[4];
		SND_DAT_BUF_LPF[12] = byteArray[5];
		SND_DAT_BUF_LPF[13] = byteArray[6];
		SND_DAT_BUF_LPF[14] = byteArray[7];

		SND_DAT_BUF_LPF[15]  = FOOTER1;
		SND_DAT_BUF_LPF[16]  = FOOTER2;
		SND_DAT_BUF_LPF[17]  = FOOTER3;
		SND_DAT_BUF_LPF[18]  = FOOTER4;

		ret = uart_send_pc(SND_DAT_BUF_LPF, sizeof(SND_DAT_BUF_LPF));
		return ret;
	}

	SND_DAT_BUF_RAW[0] = HEADER1;
	SND_DAT_BUF_RAW[1] = HEADER2;
	SND_DAT_BUF_RAW[2] = HEADER3;
	SND_DAT_BUF_RAW[3] = HEADER4;

	SND_DAT_BUF_RAW[4] = REC_DAT_BUF[0];
	SND_DAT_BUF_RAW[5] = REC_DAT_BUF[1];
	SND_DAT_BUF_RAW[6] = REC_DAT_BUF[2];

	SND_DAT_BUF_RAW[7] = FOOTER1;
	SND_DAT_BUF_RAW[8] = FOOTER2;
	SND_DAT_BUF_RAW[9] = FOOTER3;
	SND_DAT_BUF_RAW[10] = FOOTER4;

	ret = uart_send_pc(SND_DAT_BUF_RAW, sizeof(SND_DAT_BUF_RAW));
	return ret;
}

void adc_send_value() {
	int ret;
	ret = adc_write((uint8_t[]){ CMD_START }, 1);
	if (ret != 0) return;
	
	int sleep_time = 1000 / data_rate;
	k_msleep(sleep_time);

	adc_read_reg(CONF2_REG);
	adc_receive(REC_REG_BUF_1, sizeof(REC_REG_BUF_1));

	int cnt = 0;

	while (!(REC_REG_BUF_1[0] & 0x80)) {
		k_msleep(1);
		cnt++;
		adc_read_reg(CONF2_REG);
		adc_receive(REC_REG_BUF_1, sizeof(REC_REG_BUF_1));

		if (cnt == ADC_TIMEOUT) return;
	};

	ret = adc_write((uint8_t[]){ CMD_RDATA }, 1);
	if (ret != 0) return;
	ret = adc_receive(REC_DAT_BUF, sizeof(REC_DAT_BUF));
	if (ret != 0) return;

	get_digital_count(REC_DAT_BUF);

	ret = filter_adjust();
	if (ret != 0) return;
	// printf("\n%d\n", digital_count);

	gpio_pin_toggle_dt(&led0);
}

int main(void)
{
	// uart_tx(uart, receive_buffer, sizeof(receive_buffer), SYS_FOREVER_US);

	// PROGRAM START NOTICE
	reset_pin();
	// printf("Hello_World\n");

	for(int i = 0; i <= 3; i++) {
		gpio_pin_toggle_dt(&led0);
		k_msleep(500);
	}

	k_thread_create(&adc_loop_thread, adc_loop_stack, K_THREAD_STACK_SIZEOF(adc_loop_stack),
	adc_loop_thread_entry, NULL, NULL, NULL, 5, 0, K_NO_WAIT);

	callback_set_pc_uart();
	rx_enable_pc_uart();
	
	// adc_reset
	adc_write((uint8_t[]){ CMD_RESET }, 1);

	while (1) {
		if (k_sem_take(&uart_rec_semaphore, K_FOREVER) == 0) {
			process_cmd(now_command, &adc_loop_semaphore);
			
			adc_write_conf0(conf0_set);
			adc_write_conf1(conf1_set);
		}
		// now_command = 0;
		// memset(REC_CMD_BUF, 0, sizeof(REC_CMD_BUF));
	}
}
