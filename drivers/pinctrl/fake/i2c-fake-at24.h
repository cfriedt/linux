#ifndef PINCTRL_FAKE_I2C_AT24_H_
#define PINCTRL_FAKE_I2C_AT24_H_

#include <linux/types.h>

// simple eeprom example
#define I2C_ADDR_MIN_AT24 0x50
#define I2C_ADDR_MAX_AT24 0x57

#define I2C_AT24_MEM_SIZE_MIN ( 8 * 1024 / 8 )
#define I2C_AT24_MEM_SIZE_MAX ( 64 * 1024 / 8 )
#define I2C_AT24_MEM_SIZE_DEFAULT I2C_AT24_MEM_SIZE_MIN

struct pinctrl_fake_i2c_device_at24 {
	u16 device_address;
	u16 mem_address;
	u16 mem_size;
	u8 *mem;
};

struct i2c_adapter;
struct i2c_msg;

int pinctrl_fake_i2c_at24_init( struct pinctrl_fake_i2c_device_at24 *eeprom, u16 addr, u16 size );
void pinctrl_fake_i2c_at24_fini( struct pinctrl_fake_i2c_device_at24 *eeprom );
int pinctrl_fake_i2c_at24_xfer( struct i2c_adapter *adap, struct i2c_msg *msgs, int num );


#endif /* PINCTRL_FAKE_I2C_AT24_H_ */
