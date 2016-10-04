#ifndef PINCTRL_FAKE_I2C_MCP9808_WORKER_H_
#define PINCTRL_FAKE_I2C_MCP9808_WORKER_H_

#include <linux/types.h>
#include <linux/workqueue.h>

#define I2C_MCP9808_PERIOD_MS_MIN 1000
#define I2C_MCP9808_PERIOD_MS_MAX 10000
#define I2C_MCP9808_PERIOD_MS_DEFAULT I2C_MCP9808_PERIOD_MS_MIN

struct pinctrl_fake_gpio_chip;

struct pinctrl_fake_i2c_mcp9808_worker {
	u16 period_ms;
	struct delayed_work dwork;
	struct pinctrl_fake_gpio_chip *fchip;
	int fchip_offset;
};

int pinctrl_fake_i2c_mcp9808_worker_init( struct pinctrl_fake_i2c_mcp9808_worker *worker, unsigned period_ms, struct pinctrl_fake_gpio_chip *fchip, int fchip_offset );
void pinctrl_fake_i2c_mcp9808_worker_fini( struct pinctrl_fake_i2c_mcp9808_worker *worker );

#endif /* PINCTRL_FAKE_I2C_MCP9808_WORKER_H_ */
