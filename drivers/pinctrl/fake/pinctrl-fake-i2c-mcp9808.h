#ifndef PINCTRL_FAKE_I2C_MCP9808_H_
#define PINCTRL_FAKE_I2C_MCP9808_H_

#include <linux/types.h>
#include <linux/workqueue.h>
#include "mcp9808-regs.h"

enum {
	MCP9808_CONFIG_ALERT_MOD_COMPARATOR,
	MCP9808_CONFIG_ALERT_MOD_INTERRUPT,
};
enum {
	MCP9808_CONFIG_ALERT_POL_ACTIVE_LOW,
	MCP9808_CONFIG_ALERT_POL_ACTIVE_HIGH,
};
enum {
	MCP9808_CONFIG_ALERT_SEL_TUPPER_TLOWER_TCRIT,
	MCP9808_CONFIG_ALERT_SEL_TCRIT,
};
enum {
	MCP9808_CONFIG_ALERT_CNT_DISABLED,
	MCP9808_CONFIG_ALERT_CNT_ENABLED,
};
enum {
	MCP9808_CONFIG_ALERT_STAT_NOT_ASSERTED,
	MCP9808_CONFIG_ALERT_STAT_ASSERTED,
};
enum {
	MCP9808_CONFIG_INT_CLEAR = 1,
};
enum {
	MCP9808_CONFIG_WIN_LOCK_UNLOCKED,
	MCP9808_CONFIG_WIN_LOCK_LOCKED,
};
enum {
	MCP9808_CONFIG_CRIT_LOCK_UNLOCKED,
	MCP9808_CONFIG_CRIT_LOCK_LOCKED,
};
enum {
	MCP9808_CONFIG_SHDN_ON,
	MCP9808_CONFIG_SHDN_OFF,
};
enum {
	MCP9808_CONFIG_THYST_0P0_DEGC,
	MCP9808_CONFIG_THYST_1P5_DEGC,
	MCP9808_CONFIG_THYST_3P0_DEGC,
	MCP9808_CONFIG_THYST_6P0_DEGC,
};

enum {
	MCP9808_RES_0P5000_DEGREES_C,
	MCP9808_RES_0P2500_DEGREES_C,
	MCP9808_RES_0P1250_DEGREES_C,
	MCP9808_RES_0P0625_DEGREES_C,
};

#include "pinctrl-fake-i2c-mcp9808-worker.h"


#define MCP9808_MANUFACTURER_ID_MSB 0x00
#define MCP9808_MANUFACTURER_ID_LSB 0x54

#define MCP9808_DEVICE_ID       0x04
#define MCP9808_DEVICE_REVISION 0x00

#define I2C_ADDR_MCP9808_MIN 0x18
#define I2C_ADDR_MCP9808_MAX 0x1f

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
