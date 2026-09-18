#include "DAC.h"
#include <zephyr/drivers/spi.h>

static const struct device *spi = DEVICE_DT_GET(SPI_NODE);

struct spi_config spi_cfg = {
    .frequency = SPI_CLOCK_SLOW,
    .operation = SPI_OP_MODE_MASTER
                | SPI_MODE_CPHA
                | SPI_TRANSFER_MSB
                | SPI_WORD_SET(8),
    .slave = 0,
    .cs = {
        .gpio = GPIO_DT_SPEC_GET_BY_IDX(SPI_NODE, cs_gpios, 0),
        .delay = 0,
    },
};

uint8_t tx_data[3];
uint8_t tx_debug[10] = {0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90};
uint8_t debug_cnt = 0;

struct spi_buf tx_buf = {
    .buf = tx_data,
    .len = sizeof(tx_data),
};

struct spi_buf_set tx = {
    .buffers = &tx_buf,
    .count = 1,
};

static const struct gpio_dt_spec dac_cs =
    GPIO_DT_SPEC_GET_BY_IDX(SPI_NODE, cs_gpios, 0);

void dac_debug(void)
{
    printf("SPI ready : %d\n", device_is_ready(spi));
    printf("CS ready  : %d\n", gpio_is_ready_dt(&dac_cs));
    printf("CS pin    : %d\n", dac_cs.pin);
    printf("CS flags  : 0x%x\n", dac_cs.dt_flags);
    printf("CS is GPIO     : %d\n", spi_cs_is_gpio(&spi_cfg));
}

int dac_write() {
    int ret;

    if (debug_cnt >= sizeof(tx_debug)) debug_cnt = 0;
    tx_data[0] = 0x00;
    tx_data[1] = tx_debug[debug_cnt++];
    tx_data[2] = 0x00;

    ret = spi_write(spi, &spi_cfg, &tx);
    // printf("DAC Write ret: %d\n", ret);
    return ret;
}