#include <stdio.h>
#include "ADC.h"
#include <zephyr/drivers/i2c.h>

uint8_t WRITE_CONF0[2];
uint8_t pga_gain;
int32_t digital_count;

static const struct device *i2c = DEVICE_DT_GET(I2C_NODE);

// Write to adc
void adc_write(const uint8_t *write_buff, uint32_t size){
    int ret;
	ret = i2c_write(i2c, write_buff, size, SLAVE_ADDR);

	if (ret != 0) {
        printf("Failed to write to I2C device! (err %d)\n", ret);
    }
	
	else {
        printf("Send: ");
        for (int i = 0; i < size; i++){
            printf("0x%02X ", write_buff[i]);
        }
        printf("to slave 0x%02X\n", SLAVE_ADDR);
    }
}

// Read from adc
void adc_receive(uint8_t *receive_buff, uint32_t size) {
	int ret;
	ret = i2c_read(i2c, receive_buff, size, SLAVE_ADDR);
	
	if (ret != 0) {
        printf("Failed to write to I2C device! (err %d)\n", ret);
    }
	
	else {
		printf("Received: ");
		for (int i = 0; i < size; i++){
            printf("0x%02X ", receive_buff[i]);
        }
        printf("to slave 0x%02X\n", SLAVE_ADDR);
    }
}

// Set Configuration Register
// CONF0 DEFAULT :: mux = 0011 : AINP = AIN1, AINN = AIN0 (default) gain = 000 : gain = 1 (default) pga_en = 0 : PGA enabled (default)
void adc_write_conf0(uint8_t addr, uint8_t mux, uint8_t gain, uint8_t pga_en) {
    uint8_t cmd;
    
    cmd = (mux  << 4) | gain | pga_en;


    pga_gain = gain;     // save pga_gain value for voltage calculation
    WRITE_CONF0[0] = CMD_WREG | (CONF0_REG << 2);
    WRITE_CONF0[1] = cmd;

    adc_write(WRITE_CONF0, sizeof(WRITE_CONF0));
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

// Get Digital Count Value From Received Bytes
void get_digital_count(const uint8_t *received_buff) {
    int32_t dc;
	dc = (received_buff[0] << 16) |
	     (received_buff[1] << 8)  |
	      received_buff[2];           // Merge All Received Values Into One
	dc = dc << 8;                    //        arithmatic shift
	dc = dc >> 8;                    //   (change into signed decimal)
    digital_count = dc;
}

// Calculate Actual Voltage Value
double get_actual_voltage(uint32_t dc) {
	double v_in;

    uint8_t square = pga_gain / 2;
    int gain = pow(2.0, square);

	v_in = (2 * 2.048f / gain) / pow(2.0, 24.0); // LSB Amount (Voltage Per Bit)
	v_in *= dc;                                      // Multiply LSB with DC Value Returns Voltage In Value
    return v_in;
}
