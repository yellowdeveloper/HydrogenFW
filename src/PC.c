#include <stdio.h>
#include "PC.h"

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