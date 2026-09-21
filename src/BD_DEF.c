#include "BD_DEF.h"

int bd_init() {
    int ret;
    ret = sensor_power_on();

    ret = xtr_out();

    ret = heater_pw_on();

    init_xtr_err();

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

int heater_pw_on() {
    int ret;

    ret = gpio_is_ready_dt(&heater_vdd_on); 
    if (!ret) return -1;

    ret = gpio_pin_configure_dt(&heater_vdd_on, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) return ret;

    ret = gpio_pin_set_dt(&heater_vdd_on, 1);
    if (ret != 0) return ret;

    return ret;
}

int xtr_out() {
    int ret;

    ret = gpio_is_ready_dt(&xtr_out_on); 
    if (!ret) return -1;

    ret = gpio_pin_configure_dt(&xtr_out_on, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) return ret;

    ret = gpio_pin_set_dt(&xtr_out_on, 1);
    if (ret != 0) return ret;

    return ret;
}

void init_xtr_err(void)
{
    if (!gpio_is_ready_dt(&xtr_err)) {
        printf("Error: GPIO device not ready\n");
        return;
    }

    int ret = gpio_pin_configure_dt(&xtr_err, GPIO_INPUT);
    if (ret < 0) {
        printf("Error: Failed to configure pin as input\n");
        return;
    }
}

void read_xtr_err(void) {
    int val = gpio_pin_get_dt(&xtr_err);
    if (val < 0) {
        printf("Error: Failed to read pin state\n");
        return;
    }

    if (val == 0) printf("XTR ERROR OCCURED GPIO PIN STATE LOW\n", val);
}