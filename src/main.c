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
#include "BT.h"

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   100

/* Devicetree node identifiers */
#define LED0_NODE DT_ALIAS(led0)

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

// reset pin and initialize bluetooth
int init_set() {
	int ret;

	if (!gpio_is_ready_dt(&led0)) return 0; 

	ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) return 0;

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
	while(1) {
		if (k_sem_take(&adc_loop_semaphore, K_FOREVER) == 0 && adc_flag == 1) {
			int ret;
			ret = adc_write((uint8_t[]){ CMD_START }, 1);
			if (ret != 0) continue;

			while(adc_flag == 1){
				//adc_test(); // to print debug message
				adc_read_value();
			}
			
			ret = adc_write((uint8_t[]){ CMD_PW_DONW }, 1);
			if (ret != 0) continue;
		}
	}
}

int adc_send_thread_entry() {
	while(1) {
		adc_send_value();
	}
}

int bt_send_thread_entry() {
	while(1) {
		hydro_notify_data();

		k_msleep(100);
	}
}

void DATARATE_TEST() {
	// WRITE RESET COMMAND TO ADC
	printf("\n**************************************************\n");
	printf("\n- Reset ADC\n");
	adc_write((uint8_t[]){ CMD_RESET }, 1);

	// WRITE DATA REGISTER (CONFIGURATION REGISTER 0)
	printf("\n- Change Configuration Register 0 :: %02X\n", conf0_set);
	adc_write_conf0(conf0_set);

	printf("\n- Change Configuration Register 1 :: %02X\n", conf1_set);
	adc_write_conf1(0xC8);

	adc_write((uint8_t[]){ CMD_START }, 1);

	int received = 0;
	uint32_t start_cycles = k_cycle_get_32();
	while (received < 1000) {
		// WRITE START DATA CONVERSION COMMAND TO ADC
		int cnt = 0;

		do {
			cnt++;
			adc_read_reg(CONF2_REG);
			adc_receive(REC_REG_BUF_1, sizeof(REC_REG_BUF_1));

			if (cnt == ADC_TIMEOUT) return;

			if (!(REC_REG_BUF_1[0] & 0x80)) {
            k_yield(); 
        }
		} while (!(REC_REG_BUF_1[0] & 0x80));

		int ret;
		ret = adc_write((uint8_t[]){ CMD_RDATA }, 1);
		if (ret != 0) return;
		ret = adc_receive(REC_DAT_TMP, sizeof(REC_DAT_TMP));
		if (ret != 0) return;

		received++;
	}
	uint32_t end_cycles = k_cycle_get_32();
    uint32_t cycles_spent = end_cycles - start_cycles;

    uint64_t ns_spent = k_cyc_to_ns_floor64(cycles_spent);
    uint32_t us_spent = (uint32_t)(ns_spent / 1000);
    uint32_t ms_spent = (uint32_t)(us_spent / 1000);

	printf("Time Spent: %d, Sample Count: %d", ms_spent, received);

	adc_write((uint8_t[]){ CMD_PW_DONW }, 1);
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
	adc_receive(REC_DAT_TMP, sizeof(REC_DAT_TMP));

	// SET RESULT VALUE AND PRINT
	get_digital_count(REC_DAT_TMP); 
	double v_in = get_actual_voltage(digital_count);
	printf("Gain Set To: 2^%d\n", pga_gain);
	printf("Digital Count: %02X: %d\n", digital_count, digital_count);
	LPF(digital_count);
	double avg_filtered = MAF(prev_filtered);
	printf("DC After LPF: %lf\n", prev_filtered);
	printf("DC After AF: %lf\n", avg_filtered);
	printf("Voltage in = %lf\n", v_in);
	printf("\n**************************************************\n");

	// NOTICE USING LED
	gpio_pin_toggle_dt(&led0);

	// READ DATA END
}

int check_filter(int32_t dc) {
	int ret;
	int32_t last_filtered = dc;
	uint8_t inserted = 0;
	uint32_t size = 10;

	if (saf_stat) {
        last_filtered = SAF(last_filtered, saf_stat);

		if (last_filtered == 0) return 0;
    }

	unsigned char byteArray[6+sizeof(int32_t)*3];
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

	ret = filter_adjust(SND_DAT_BUF, size, byteArray);
	return ret;
}

void adc_read_value() {
	int ret;
	int cnt = 0;

	//int sleep_time = 1000 / data_rate;
	//k_msleep(sleep_time);

	do {
		cnt++;
		adc_read_reg(CONF2_REG);
		adc_receive(REC_REG_BUF_1, sizeof(REC_REG_BUF_1));

		if (cnt == ADC_TIMEOUT) return;

		if (!(REC_REG_BUF_1[0] & 0x80)) {
            k_yield(); 
        }
	} while (!(REC_REG_BUF_1[0] & 0x80));

	//ret = adc_read_write(REC_DAT_BUF, sizeof(REC_DAT_BUF), (uint8_t[]){ CMD_RDATA }, 1);
	//if (ret != 0) return;

	ret = adc_write((uint8_t[]){ CMD_RDATA }, 1);
	if (ret != 0) return;
	ret = adc_receive(REC_DAT_TMP, sizeof(REC_DAT_TMP));
	if (ret != 0) return;

	if (k_msgq_put(&adc_queue, REC_DAT_TMP, K_NO_WAIT) != 0) {}
}

void adc_send_value() {
	int ret;

	if (k_msgq_get(&adc_queue, GET_DAT_TMP, K_FOREVER) != 0) {}
	else {
		get_digital_count(GET_DAT_TMP);

		ret = check_filter(digital_count);
		if (ret != 0) return;
	}

	gpio_pin_toggle_dt(&led0);
}

int main(void)
{
	// PROGRAM START NOTICE
	init_set();

	k_thread_create(&adc_loop_thread, adc_loop_stack, K_THREAD_STACK_SIZEOF(adc_loop_stack),
	adc_loop_thread_entry, NULL, NULL, NULL, 5, 0, K_NO_WAIT);

	k_thread_create(&adc_send_thread, adc_send_stack, K_THREAD_STACK_SIZEOF(adc_send_stack),
	adc_send_thread_entry, NULL, NULL, NULL, 5, 0, K_NO_WAIT);

	// k_thread_create(&bt_send_thread, bt_send_stack, K_THREAD_STACK_SIZEOF(bt_send_stack),
	// bt_send_thread_entry, NULL, NULL, NULL, 5, 0, K_NO_WAIT);

	callback_set_pc_uart();
	rx_enable_pc_uart();

	//uart_send_pc(STRT_BUF, sizeof(STRT_BUF));

	for(int i = 0; i <= 3; i++) {
		gpio_pin_toggle_dt(&led0);
		k_msleep(200);
	}
	
	// DAT TEST: ERASE LATER
	//dac_write_cont();
	//while (1) {
	//	uint16_t count = dac_calculate_count(100.0f);
	//	dac_write_count(count);
	//	printf("DIGITAL COUNT: %d\n", count);
	//	gpio_pin_toggle_dt(&led0);
	//	k_msleep(100);
	//}

	// DATARATE_TEST: ERASE LATER
	// DATARATE_TEST();

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