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

extern uint8_t read_cmd(const uint8_t *received_buff, uint32_t size);
extern void handle_received_cmd(uint8_t cmd);

#endif