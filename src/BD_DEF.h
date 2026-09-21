#ifndef BD_DEF_H
#define BD_DEF_H

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)
#define XTR_ERR_NODE DT_ALIAS(myinput)

static const struct gpio_dt_spec sensor_vdd_on =
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, sensor_vdd_on_gpios);

static const struct gpio_dt_spec heater_vdd_on =
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, heater_vdd_on_gpios);

static const struct gpio_dt_spec xtr_out_on =
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, xtr_out_on_gpios);

static const struct gpio_dt_spec xtr_err =
    GPIO_DT_SPEC_GET(XTR_ERR_NODE, gpios);

#endif // BD_DEF_H