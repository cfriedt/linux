#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>

#include <linux/i2c.h>

#include <linux/interrupt.h>

#include "pinctrl-fake.h"

static int pinctrl_fake_i2c_adapter_algo_master_xfer( struct i2c_adapter *adap, struct i2c_msg *msgs, int num ) {
	int r;
	u16 addr;

	if ( num <= 0 ) {
		r = 0;
		goto out;
	}

	addr = msgs[ 0 ].addr;

	r = -ENODEV;

	if ( addr >= I2C_ADDR_MIN_AT24 && addr <= I2C_ADDR_MAX_AT24 ) {
		r = pinctrl_fake_i2c_at24_xfer( adap, msgs, num );
	}


	if ( addr >= I2C_ADDR_MIN_MCP9808 && addr <= I2C_ADDR_MAX_MCP9808 ) {
		r = pinctrl_fake_i2c_mcp9808_xfer( adap, msgs, num );
	}

out:
	return r;
}

static u32 pinctrl_fake_i2c_adapter_algo_functionality( struct i2c_adapter *adap ) {
	u32 r;

	r = I2C_FUNC_I2C;

	return r;
}

static const struct i2c_algorithm pinctrl_fake_i2c_algorithm = {
	.master_xfer = pinctrl_fake_i2c_adapter_algo_master_xfer,
	.functionality = pinctrl_fake_i2c_adapter_algo_functionality,
};

static const struct pinctrl_fake_i2c_chip pinctrl_fake_i2c_chip_template = {
	.adapter = {
		.owner = THIS_MODULE,
		.name = "pinctrl-fake-i2c",
		.algo = & pinctrl_fake_i2c_algorithm,
	},
};

int pinctrl_fake_i2c_init( struct pinctrl_fake *pctrl ) {

	int r;

	struct pinctrl_fake_i2c_chip *ichip;
	int i;
	char name_base[] = "a";

	dev_info( pctrl->dev, "Fake I2C Bus, Copyright (C) 2016, Christopher Friedt\n" );

	for( i = 0; i < ARRAY_SIZE( pctrl->fi2cchip ); i++  ) {
		ichip = kzalloc( sizeof( *ichip ), GFP_KERNEL );
		if ( NULL == ichip ) {
			dev_err( pctrl->dev, "unable to allocate memory for fake i2c chip\n" );
			r = -ENOMEM;
			goto do_remove;
		}
		memcpy( ichip, & pinctrl_fake_i2c_chip_template, sizeof( *ichip ) );

		strcat( ichip->adapter.name, "-" );
		strcat( ichip->adapter.name, name_base );
		name_base[ 0 ]++;

		r = i2c_add_adapter( & ichip->adapter );
		if ( EXIT_SUCCESS != r ) {
			dev_err( pctrl->dev, "failed to add i2c adapter (%d)\n", r );
			goto do_remove;
		}
		ichip->adapter.algo_data = pctrl;
		dev_info( pctrl->dev, "added i2c adapter\n" );

		pctrl->fi2cchip[ i ] = ichip;

#ifdef CONFIG_PINCTRL_FAKE_I2C_AT24
		r = pinctrl_fake_i2c_at24_init( & ichip->eeprom, I2C_ADDR_MIN_AT24, I2C_AT24_MEM_SIZE_DEFAULT );
		if ( EXIT_SUCCESS != r ) {
			dev_err( pctrl->dev, "failed to add eeprom (%d)\n", r );
			goto do_remove;
		}
#endif // CONFIG_PINCTRL_FAKE_I2C_AT24

#ifdef CONFIG_PINCTRL_FAKE_I2C_MCP9808
		r = pinctrl_fake_i2c_mcp9808_init( & ichip->therm, I2C_ADDR_MIN_MCP9808 );
		if ( EXIT_SUCCESS != r ) {
			dev_err( pctrl->dev, "failed to add temperature sensor (%d)\n", r );
			goto do_remove;
		}
#endif // CONFIG_PINCTRL_FAKE_I2C_MCP9808
	}

	r = EXIT_SUCCESS;
	goto out;

do_remove:
	for( ; i >= 0; i-- ) {
		ichip = pctrl->fi2cchip[ i ];
		if ( NULL != ichip ) {

#ifdef CONFIG_PINCTRL_FAKE_I2C_MCP9808
			pinctrl_fake_i2c_mcp9808_fini( & ichip->therm );
#endif // CONFIG_PINCTRL_FAKE_I2C_MCP9808

#ifdef CONFIG_PINCTRL_FAKE_I2C_AT24
			pinctrl_fake_i2c_at24_fini( & ichip->eeprom );
#endif // CONFIG_PINCTRL_FAKE_I2C_AT24

			i2c_del_adapter( & ichip->adapter );
			kfree( ichip );
			pctrl->fi2cchip[ i ] = NULL;
		}
	}

out:
	return r;
}

void pinctrl_fake_i2c_fini( struct pinctrl_fake *pctrl ) {

	struct pinctrl_fake_i2c_chip *ichip;
	int i;

	for( i = 0; i < ARRAY_SIZE( pctrl->fi2cchip ); i++  ) {
		ichip = pctrl->fi2cchip[ i ];
		if ( NULL != ichip ) {

			dev_info( & ichip->adapter.dev, "removing adapter\n" );

#ifdef CONFIG_PINCTRL_FAKE_I2C_MCP9808
			pinctrl_fake_i2c_mcp9808_fini( & ichip->therm );
#endif // CONFIG_PINCTRL_FAKE_I2C_MCP9808

#ifdef CONFIG_PINCTRL_FAKE_I2C_AT24
			pinctrl_fake_i2c_at24_fini( & ichip->eeprom );
#endif // CONFIG_PINCTRL_FAKE_I2C_AT24

			i2c_del_adapter( & ichip->adapter );
			kfree( ichip );
			pctrl->fi2cchip[ i ] = NULL;
		}
	}

	dev_info( pctrl->dev, "Fake I2C Bus Unloading..\n" );
}
