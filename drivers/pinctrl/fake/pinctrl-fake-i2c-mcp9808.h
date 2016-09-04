#ifndef PINCTRL_FAKE_I2C_MCP9808_H_
#define PINCTRL_FAKE_I2C_MCP9808_H_

#include <linux/types.h>
#include <linux/workqueue.h>

#include "pinctrl-fake-i2c-mcp9808-worker.h"

#define I2C_ADDR_MIN_MCP9808 0x18
#define I2C_ADDR_MAX_MCP9808 0x1f

enum {
	MCP9808_RFU    = 0, // RFU, Reserved for Future Use (Read-Only register)
	MCP9808_CONFIG = 1, // Configuration register (CONFIG)
	MCP9808_TUPPER = 2, // Alert Temperature Upper Boundary Trip register (TUPPER)
	MCP9808_TLOWER = 3, // Alert Temperature Lower Boundary Trip register (TLOWER)
	MCP9808_TCRIT  = 4, // Critical Temperature Trip register (TCRIT)
	MCP9808_TA     = 5, // Temperature register (TA)
	MCP9808_MID    = 6, // Manufacturer ID register
	MCP9808_DID    = 7, // Device ID/Revision register
	MCP9808_RES    = 8, // Resolution register
	MCP9808_NREG_,
	// all other registers are reserved (Some registers contain calibration codes and should not be accessed.)
};
struct pinctrl_fake_i2c_device_mcp9808 {
	u16 device_address;
	u8 mem_address;
	// some registers are 16-bit and some are 8-bit, so must use u16
	u16 reg[ MCP9808_NREG_ ];
#ifdef CONFIG_PINCTRL_FAKE_I2C_MCP9808_WORKER
	struct pinctrl_fake_i2c_mcp9808_worker worker;
#endif // CONFIG_PINCTRL_FAKE_I2C_MCP9808_WORKER
};

struct i2c_adapter;
struct i2c_msg;

int pinctrl_fake_i2c_mcp9808_init( struct pinctrl_fake_i2c_device_mcp9808 *therm, u16 addr );
void pinctrl_fake_i2c_mcp9808_fini( struct pinctrl_fake_i2c_device_mcp9808 *therm );

int pinctrl_fake_i2c_mcp9808_xfer( struct i2c_adapter *adap, struct i2c_msg *msgs, int num );


#endif /* PINCTRL_FAKE_I2C_MCP9808_H_ */
