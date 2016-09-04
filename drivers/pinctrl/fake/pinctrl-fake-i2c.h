#ifndef PINCTRL_FAKE_I2C_H_
#define PINCTRL_FAKE_I2C_H_

#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>

#include "pinctrl-fake-i2c-at24.h"
#include "pinctrl-fake-i2c-mcp9808.h"

struct pinctrl_fake_i2c_chip {
	struct i2c_adapter adapter;
	struct pinctrl_fake_i2c_device_at24 eeprom;
	struct pinctrl_fake_i2c_device_mcp9808 therm;
};

int pinctrl_fake_i2c_init( struct pinctrl_fake *pctrl );
void pinctrl_fake_i2c_fini( struct pinctrl_fake *pctrl );

#endif /* PINCTRL_FAKE_I2C_H_ */
