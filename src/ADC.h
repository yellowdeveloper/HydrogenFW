#ifndef ADC_H
#define ADC_H

// BASIC ADDRESSES & COMMANDS & VALUES
#define SLAVE_ADDR   0x4F

#define CONF0_REG    0x00       // Configuration0 Register Address
#define CONF1_REG    0x01       // Configuration1 Register Address
#define CONF2_REG    0X02       // Configuration2 Register Address
#define CONF3_REG    0x03       // Configuration3 Register Address

#define CMD_RDATA    0x01 << 4  // 0001 xxxx
#define CMD_RREG     0x02 << 4  // 0010 rrxx (use or op with REG address)
#define CMD_WREG     0x04 << 4  // 0100 rrxx dddd dddd (use or op with REG address)
#define CMD_START    0x04 << 1  // 0000 100x
#define CMD_RESET    0x03 << 1  // 0000 011x
#define CMD_PW_DONW  0x01 << 1  // 0000 001x

// I2C NODE
#define I2C_NODE DT_NODELABEL(arduino_i2c)

// CONFIGURATION REGISTER SETTINGS
// AINp = AIN1 | AINn = AIN0 | GAIN = 1 ~ 128 | PGA = 0 (default)
extern uint8_t WRITE_CONF0[2];
// (default)
extern uint8_t WRITE_CONF1[2];
// (default)
extern uint8_t WRITE_CONF2[2];
// (default)
extern uint8_t WRITE_CONF3[2];

// EDITABLE VARIABLES
extern uint8_t pga_gain; // 0 = 1 / 2 = 2 / 4 = 4 / 6 = 8 ... 14 = 128
extern int32_t digital_count;

// FUNCTION DEFINE
extern void adc_write(const uint8_t *write_buff, uint32_t size);
extern void adc_receive(uint8_t *receive_buff, uint32_t size);
extern void adc_write_reg(uint8_t addr, uint8_t mux, uint8_t gain, uint8_t pga_en);
extern void adc_read_reg(uint8_t addr);
extern void get_digital_count(const uint8_t *received_buff);
extern double get_actual_voltage(uint32_t dc);
#endif