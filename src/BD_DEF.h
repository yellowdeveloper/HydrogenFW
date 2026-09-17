#ifndef BD_DEF_H
#define BD_DEF_H

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec sensor_vdd_on =
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, sensor_vdd_on_gpios);

#endif // BD_DEF_H