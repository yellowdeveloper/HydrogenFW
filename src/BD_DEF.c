#include "BD_DEF.h"

int bd_init() {
    int ret;
    ret = sensor_power_on();

    return ret;
}

int sensor_power_on() {
    int ret;

    ret = gpio_is_ready_dt(&sensor_vdd_on); 
    if (!ret) return -1;

    ret = gpio_pin_configure_dt(&sensor_vdd_on, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) return ret;

    ret = gpio_pin_set_dt(&sensor_vdd_on, 1);
    if (ret != 0) return ret;

    return ret;
}