/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/uart.h>

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
static const struct device *uart = DEVICE_DT_GET(UART_NODE);

// buffer to receive data from ADC
uint8_t REC_DAT_BUF[3];
uint8_t REC_REG_BUF_1[1];

// Buffer to reciev data from PC
uint8_t REC_CMD_BUF[9];

// Thread Receiving Cmd From PC
K_SEM_DEFINE(uart_rec_semaphore, 0, 1);
K_THREAD_STACK_DEFINE(uart_rec_stack, 1024);
struct k_thread uart_rec_thread;

// reset gpio pins
int reset_pin() {
	int ret;

	if (!gpio_is_ready_dt(&led0)) {
		return 0;
	}

	ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return 0;
	}
}

void adc_test() {
    // WRITE RESET COMMAND TO ADC
	printf("\n- Reset ADC\n");
	adc_write((uint8_t[]){ CMD_RESET }, 1);


	// WRITE DATA REGISTER (CONFIGURATION REGISTER 0)
	printf("\n- Change Configuration Register 0 :: %02X\n", WRITE_CONF0[1]);
	adc_write_conf0(CONF0_REG, 0x03, pga_gain = 4, 0x00);


	// READ DATA REGISTER (CONFIGURATION REGISTER 0)
	printf("\n- Read Configuration Register 0\n");
	adc_read_reg(CONF0_REG);
	adc_receive(REC_REG_BUF_1, sizeof(REC_REG_BUF_1));
	

	// WRITE START DATA CONVERSION COMMAND TO ADC
	printf("\n- Send Read Command to ADC\n");
	adc_write((uint8_t[]){ CMD_START }, 1);

	k_msleep(50);

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
	printf("Digital Count: %02X: %d\n", digital_count, digital_count);
	printf("Voltage in = %lf\n", v_in);

	// NOTICE USING LED
	gpio_pin_toggle_dt(&led0);

	// READ DATA END
}

int main(void)
{
	// uart_tx(uart, receive_buffer, sizeof(receive_buffer), SYS_FOREVER_US);

	// PROGRAM START NOTICE
	reset_pin();
	printf("hello_world\n");

	for(int i = 0; i <= 3; i++) {
		gpio_pin_toggle_dt(&led0);
		k_msleep(500);
	}

	while (1) {
	// Command recieve enable
	int ret;
	ret = uart_rx_enable(uart, REC_CMD_BUF, sizeof(REC_CMD_BUF), 1000);
	uint8_t cmd = read_cmd(REC_CMD_BUF, sizeof(REC_CMD_BUF));
	printf("Received command = %d", cmd);

	adc_test();

	k_msleep(200);
	}
}
