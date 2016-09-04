#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>

#include <linux/i2c.h>

#include <linux/interrupt.h>

#include "pinctrl-fake.h"

static const u16 pinctrl_fake_i2c_mcp9808_regs_default[ MCP9808_NREG_ ] = {
	[ MCP9808_MID ] = 0x0054,
	[ MCP9808_DID ] = 0x0400,
	[ MCP9808_RES ] = 0x03,
};

static const u16 pinctrl_fake_i2c_mcp9808_regs_size[ MCP9808_NREG_ ] = {
	[ MCP9808_RFU ]    = 0,
	[ MCP9808_CONFIG ] = 2,
	[ MCP9808_TUPPER ] = 2,
	[ MCP9808_TLOWER ] = 2,
	[ MCP9808_TCRIT ]  = 2,
	[ MCP9808_TA ]     = 2,
	[ MCP9808_MID ]    = 2,
	[ MCP9808_DID ]    = 2,
	[ MCP9808_RES ]    = 1,
};

int pinctrl_fake_i2c_mcp9808_xfer( struct i2c_adapter *adap, struct i2c_msg *msgs, int num ) {

	int r;

	int i;
	struct pinctrl_fake_i2c_chip *ichip;
	struct pinctrl_fake *pctrl;
	struct i2c_msg *msg;
	bool read;
	unsigned offset;
	unsigned nbytes;

	pctrl = (struct pinctrl_fake *) adap->algo_data;
	ichip = container_of( adap, struct pinctrl_fake_i2c_chip, adapter );

	r = 0;
	for( i = 0; i < num; i++ ) {
		msg = & msgs[ i ];

		read = 0 != ( msg->flags & I2C_M_RD );

		dev_info( pctrl->dev, "processing msg %u / %u (%s)\n", i + 1, num, read ? "read" : "write" );

		if ( read ) {

			offset = ichip->therm.mem_address;
			nbytes = msg->len;

			// XXX: handle address roll-over
			if ( offset >= MCP9808_NREG_ ) {
				dev_err( pctrl->dev, "offset (%04x) invalid\n", offset );
				r = -EINVAL;
				goto out;
			}

			if ( nbytes > pinctrl_fake_i2c_mcp9808_regs_size[ offset ] ) {
				dev_err( pctrl->dev, "nbytes (%u) invalid for offset (%u)\n", nbytes, offset );
				r = -EINVAL;
				goto out;
			}

			if ( nbytes != pinctrl_fake_i2c_mcp9808_regs_size[ offset ] ) {
				dev_warn( pctrl->dev, "nbytes (%u) not correct length (%u) for offset (%u)\n",
					nbytes, pinctrl_fake_i2c_mcp9808_regs_size[ offset ], offset );
			}

			if ( nbytes >= 0 ) {
				memcpy( msg->buf, & ichip->therm.reg[ offset ], nbytes );
				dev_info( pctrl->dev, "read %u bytes from therm at offset 0x%04x\n", nbytes, offset );
				ichip->therm.mem_address += nbytes;
			}

			r++;

		} else {

			offset = msg->buf[ 0 ];
			nbytes = msg->len - 1;

			// XXX: handle address roll-over
			if ( offset >= MCP9808_NREG_ ) {
				dev_err( pctrl->dev, "offset (%04x) and nbytes (%u) combination invalid\n", offset, nbytes );
				r = -EINVAL;
				goto out;
			}


			if ( nbytes > pinctrl_fake_i2c_mcp9808_regs_size[ offset ] ) {
				dev_err( pctrl->dev, "nbytes (%u) invalid for offset (%u)\n", nbytes, offset );
				r = -EINVAL;
				goto out;
			}

			if ( nbytes != pinctrl_fake_i2c_mcp9808_regs_size[ offset ] ) {
				dev_warn( pctrl->dev, "nbytes (%u) not correct length (%u) for offset (%u)\n",
					nbytes, pinctrl_fake_i2c_mcp9808_regs_size[ offset ], offset );
			}

			ichip->therm.mem_address = offset;
			if ( nbytes > 0 ) {
				memcpy( msg->buf, & ichip->therm.reg[ offset ], nbytes );
				dev_info( pctrl->dev, "read %u bytes from therm at offset 0x%04x\n", nbytes, offset );
				ichip->therm.mem_address += nbytes;
			} else {
				dev_info( pctrl->dev, "dummy write set address pointer to offset 0x%04x\n", offset );
			}

			r++;
		}
	}

out:
	return r;
}

int pinctrl_fake_i2c_mcp9808_init( struct pinctrl_fake_i2c_device_mcp9808 *therm, u16 addr ) {

	int r;

	struct pinctrl_fake_i2c_chip *ichip;

	ichip = container_of( therm, struct pinctrl_fake_i2c_chip, therm );

	dev_info( & ichip->adapter.dev, "Fake MCP9808 Temperature Sensor, Copyright (C) 2016, Christopher Friedt, initializing\n" );

	if ( addr < I2C_ADDR_MIN_MCP9808 || addr > I2C_ADDR_MAX_MCP9808 ) {
		r = -EINVAL;
		dev_err( & ichip->adapter.dev, "invalid addr 0x%04x\n", addr );
		goto out;
	}

	memcpy( therm->reg, pinctrl_fake_i2c_mcp9808_regs_default, sizeof( therm->reg ) );
	therm->device_address = addr;

#ifdef CONFIG_PINCTRL_FAKE_I2C_MCP9808_WORKER
	r = pinctrl_fake_i2c_mcp9808_worker_init( & therm->worker, I2C_MCP9808_PERIOD_MS_DEFAULT, -1 );
	if ( EXIT_SUCCESS != r ) {
		dev_err( & ichip->adapter.dev, "pinctrl_fake_i2c_mcp9808_worker_init() failed (%d)\n", r );
		goto out;
	}
#endif // CONFIG_PINCTRL_FAKE_I2C_MCP9808_WORKER

	r = EXIT_SUCCESS;

	dev_info( & ichip->adapter.dev, "added MCP9808 at address 0x%04x\n", addr );

out:
	return r;
}

void pinctrl_fake_i2c_mcp9808_fini( struct pinctrl_fake_i2c_device_mcp9808 *therm ) {

	struct pinctrl_fake_i2c_chip *ichip;
	u16 addr;

	ichip = container_of( therm, struct pinctrl_fake_i2c_chip, therm );
	addr = therm->device_address;

#ifdef CONFIG_PINCTRL_FAKE_I2C_MCP9808_WORKER
	pinctrl_fake_i2c_mcp9808_worker_fini( & therm->worker );
#endif // CONFIG_PINCTRL_FAKE_I2C_MCP9808_WORKER

	memset( therm, 0, sizeof( *therm ) );

	dev_info( & ichip->adapter.dev, "removed MCP9808 at address 0x%04x\n", addr );
}
