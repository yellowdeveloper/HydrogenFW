#include <stdio.h>
#include "ADC.h"
#include <zephyr/drivers/i2c.h>

uint8_t WRITE_CONF0[2];
uint8_t WRITE_CONF1[2];
uint32_t data_rate;
uint8_t pga_gain;
int32_t digital_count;

static const struct device *i2c = DEVICE_DT_GET(I2C_NODE);

// Write to adc
int adc_write(const uint8_t *write_buff, uint32_t size){
    int ret;
	ret = i2c_write(i2c, write_buff, size, SLAVE_ADDR);

	if (ret != 0) {
        //printf("Failed to write to I2C device! (err %d)\n", ret);
    }
	
    /*
	else {
        //printf("Send: ");
        //for (int i = 0; i < size; i++){
        //    printf("0x%02X ", write_buff[i]);
        //}
        //printf("to slave 0x%02X\n", SLAVE_ADDR);
    }
    */
    return ret;
}

// Read from adc
int adc_receive(uint8_t *receive_buff, uint32_t size) {
	int ret;
	ret = i2c_read(i2c, receive_buff, size, SLAVE_ADDR);
	
	if (ret != 0) {
        //printf("Failed to write to I2C device! (err %d)\n", ret);
    }
	/*
	else {
		//printf("Received: ");
		//for (int i = 0; i < size; i++){
        //    printf("0x%02X ", receive_buff[i]);
        //}
        //printf("to slave 0x%02X\n", SLAVE_ADDR);
    }
    */
    return ret;
}

// Read & Write Function Integrated
int adc_read_write(uint8_t *receive_buff, uint32_t size_r, const uint8_t *write_buff, uint32_t size_w) {
    int ret;
	ret = i2c_write_read(i2c, SLAVE_ADDR, write_buff, size_w, receive_buff, size_r);

    return ret;
}

// Set Configuration Register
// CONF0 DEFAULT :: mux = 0011 : AINP = AIN1, AINN = AIN0, gain = 000 : gain = 1, pga_en = 0 : PGA enabled (default)
void adc_write_conf0(uint8_t setting) {
    uint8_t gain = 0x07 & (setting >> 1) ;

    pga_gain = gain;     // save pga_gain value for voltage calculation
    WRITE_CONF0[0] = CMD_WREG | (CONF0_REG << 2);
    WRITE_CONF0[1] = setting;

    adc_write(WRITE_CONF0, sizeof(WRITE_CONF0));
}

void adc_write_conf1(uint8_t setting) {
    uint8_t dr = setting >> 5;

    data_rate = get_data_rate(dr);  // save data_rate to wait for drdy :: k_msleep(1000 / data_rate)
    WRITE_CONF1[0] = CMD_WREG | (CONF1_REG << 2);
    WRITE_CONF1[1] = setting;

    adc_write(WRITE_CONF1, sizeof(WRITE_CONF1));
}

// read byte from adc
void adc_read_reg(uint8_t addr) {
    switch (addr)
    {
    case CONF0_REG:
        addr = CMD_RREG | (CONF0_REG << 2);
        break;
    case CONF1_REG:
        addr = CMD_RREG | (CONF1_REG << 2);
        break;
    case CONF2_REG:
        addr = CMD_RREG | (CONF2_REG << 2);
        break;
    case CONF3_REG:
        addr = CMD_RREG | (CONF3_REG << 2);
        break;
    default:
        break;
    }

    adc_write((uint8_t[]){ addr }, 1);
}

// based on normal mode (do not use turbo mode)
int32_t get_data_rate(uint8_t dara_rate_byte) {
    switch (dara_rate_byte)
    {
    case 0x00:
        return 20;
        break;
    case 0x01:
        return 45;
        break;
    case 0x02:
        return 90;
        break;
    case 0x03:
        return 175;
        break;
    case 0x04:
        return 330;
        break;
    case 0x05:
        return 600;
        break;
    case 0x06:
        return 1000;
        break;
    default:
        break;
    }
}

// Get Digital Count Value From Received Bytes
void get_digital_count(const uint8_t *received_buff) {
    int32_t dc;
	dc = (received_buff[0] << 16) |
	     (received_buff[1] << 8)  |
	      received_buff[2];           // Merge All Received Values Into One
	dc = dc << 8;                     //        arithmatic shift
	dc = dc >> 8;                     //   (change into signed decimal)
    digital_count = dc;
}

// Calculate Actual Voltage Value
double get_actual_voltage(int32_t dc) {
	double v_in;

    uint8_t square = pga_gain;
    int gain = pow(2.0, square);

	v_in = (2 * 2.048f / gain) / pow(2.0, 24.0);     // LSB Amount (Voltage Per Bit)
	v_in *= dc;                                      // Multiply LSB with DC Value Returns Voltage In Value
    return v_in;
}
