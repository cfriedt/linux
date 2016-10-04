#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>

#include <linux/i2c.h>

#include <linux/interrupt.h>

#include "pinctrl-fake.h"

static const u16 pinctrl_fake_i2c_mcp9808_regs_default[ MCP9808_NREG_ ] = {
	[ MCP9808_MID ] = ( MCP9808_MANUFACTURER_ID_MSB << 8 ) | MCP9808_MANUFACTURER_ID_LSB,
	[ MCP9808_DID ] = ( MCP9808_DEVICE_ID << 8 ) | MCP9808_DEVICE_REVISION,
	[ MCP9808_RES ] = 0x0003,
};

static const u8 pinctrl_fake_i2c_mcp9808_regs_size[ MCP9808_NREG_ ] = {
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

static const u16 pinctrl_fake_i2c_mcp9808_regs_write_mask[ MCP9808_NREG_ ] = {
	[ MCP9808_RFU ]    = 0x0000,
	[ MCP9808_CONFIG ] = 0x07ef, // Alert Stat. is RO
	[ MCP9808_TUPPER ] = 0x1ff6,
	[ MCP9808_TLOWER ] = 0x1ff6,
	[ MCP9808_TCRIT ]  = 0x1ff6,
	[ MCP9808_TA ]     = 0x0000,
	[ MCP9808_MID ]    = 0x0000,
	[ MCP9808_DID ]    = 0x0000,
	[ MCP9808_RES ]    = 0x0003,
};

static const u16 pinctrl_fake_i2c_mcp9808_regs_read_mask[ MCP9808_NREG_ ] = {
	[ MCP9808_RFU ]    = 0x0000,
	[ MCP9808_CONFIG ] = 0x07df, // Int. Clear is RAZ
	[ MCP9808_TUPPER ] = 0x1ff6,
	[ MCP9808_TLOWER ] = 0x1ff6,
	[ MCP9808_TCRIT ]  = 0x1ff6,
	[ MCP9808_TA ]     = 0xffff,
	[ MCP9808_MID ]    = 0xffff,
	[ MCP9808_DID ]    = 0xffff,
	[ MCP9808_RES ]    = 0x0003,
};

int pinctrl_fake_i2c_mcp9808_xfer( struct i2c_adapter *adap, struct i2c_msg *msgs, int num ) {

	int r;

	int i;
	struct pinctrl_fake_i2c_chip *ichip;
	struct pinctrl_fake *pctrl;
	struct i2c_msg *msg;
	bool read;
	unsigned offset;
	int nbytes;
	u16 val;
	struct pinctrl_fake_gpio_chip *fchip;
	int fchip_offset;

	pctrl = (struct pinctrl_fake *) adap->algo_data;
	ichip = container_of( adap, struct pinctrl_fake_i2c_chip, adapter );

	r = 0;
	for( i = 0; i < num; i++ ) {
		msg = & msgs[ i ];

		read = 0 != ( msg->flags & I2C_M_RD );

		if ( read ) {

			offset = ichip->therm.mem_address;
			nbytes = msg->len;

			if ( 0 == offset || offset >= MCP9808_NREG_ ) {
				dev_err( & ichip->adapter.dev, "offset %u invalid\n", offset );
				r = -EINVAL;
				goto out;
			}

			if ( nbytes < 0 ) {
				dev_err( & ichip->adapter.dev, "nbytes (%d) invalid\n", nbytes );
				r = -EINVAL;
				goto out;
			}

			if ( nbytes != pinctrl_fake_i2c_mcp9808_regs_size[ offset ] ) {
				dev_warn( & ichip->adapter.dev, "nbytes (%u) not correct length (%u) for offset (%u)\n",
					nbytes, pinctrl_fake_i2c_mcp9808_regs_size[ offset ], offset );
			}

			nbytes = min( nbytes, pinctrl_fake_i2c_mcp9808_regs_size[ offset ] );

			if ( nbytes >= 0 ) {

				val = ichip->therm.reg[ offset ];
				val &= pinctrl_fake_i2c_mcp9808_regs_read_mask[ offset ];

				if ( MCP9808_CONFIG == offset ) {
					dev_info( & ichip->adapter.dev, "reading %u-byte value %04x from offset %u\n", nbytes, val, offset );
				}

				if ( sizeof( u16 ) == nbytes ) {
					// protocol specifies that MSB must be transferred first
					val = cpu_to_be16( val );
				}

				memcpy( msg->buf, & val, nbytes );
			}

			r++;

		} else {

			offset = msg->buf[ 0 ];
			nbytes = msg->len - 1;

			if ( 0 == offset || offset >= MCP9808_NREG_ ) {
				dev_err( & ichip->adapter.dev, "offset %u invalid\n", offset );
				r = -EINVAL;
				goto out;
			}

			if ( nbytes > pinctrl_fake_i2c_mcp9808_regs_size[ offset ] ) {
				dev_err( & ichip->adapter.dev, "nbytes (%u) invalid for offset (%u)\n", nbytes, offset );
				r = -EINVAL;
				goto out;
			}

			// we set the register pointer even if nbytes is 0
			ichip->therm.mem_address = offset;

			if ( nbytes > 0 ) {

				if ( nbytes != pinctrl_fake_i2c_mcp9808_regs_size[ offset ] ) {
					dev_warn( & ichip->adapter.dev, "nbytes (%u) not correct length (%u) for offset (%u)\n",
						nbytes, pinctrl_fake_i2c_mcp9808_regs_size[ offset ], offset );
				}

				if ( sizeof( u16 ) == nbytes ) {
					val = *( (uint16_t *) msg->buf );
					// protocol specifies that MSB must be transferred first
					val = be16_to_cpu( val );
				} else {
					val = msg->buf[ i ];
				}

				if ( MCP9808_CONFIG == offset ) {
					dev_info( & ichip->adapter.dev, "writing %u-byte value %04x to offset %u\n", nbytes, val, offset );
				}

				if ( 0 == pinctrl_fake_i2c_mcp9808_regs_write_mask[ offset ] ) {
					dev_warn( & ichip->adapter.dev, "attempt to write value 0x%04x to read-only register at offset (%u)\n", val, offset );
					r++;
					continue;
				}

				val &= pinctrl_fake_i2c_mcp9808_regs_write_mask[ offset ];

				if ( MCP9808_CONFIG == offset ) {
					dev_info( & ichip->adapter.dev, "writing %u-byte value %04x to offset %u\n", nbytes, val, offset );
				}

				switch( offset ) {

				case MCP9808_CONFIG:

					if ( MCP9808_CONFIG == offset ) {

//						// Disallow active-high for now
//						if ( 0 != ( val & (1 << 1) ) ) {
//							val &= ~(1 << 1);
//						}
//
						// XXX: TODO: check Alert Mod. (bit 0)

						// CLEAR INTERRUPT
						if ( 0 != ( val & (1 << 5) ) ) {
							// Int. Clear always reads as 0, so clear it before writing to memory
							val &= ~(1 << 5);

							// Alert Stat. will be 0, after writing to Int. Clear
							val &= ~(1 << 4);

							// de-assert interrupt line
							fchip = ichip->therm.worker.fchip;
							fchip_offset = ichip->therm.worker.fchip_offset;
							if ( !( NULL == fchip || fchip_offset < 0 || fchip_offset >= fchip->npins ) ) {

								if ( 0 == ( val & (1 << 1) ) ) {
									// active-low, deasserting interrupt means setting it high again
									fchip->values[ fchip_offset ] = 1;
									dev_info( & ichip->adapter.dev, "MCP9808: setting interrupt line back to 1\n" );
								} else {
									// active-high, deasserting interrupt means setting it low again
									dev_info( & ichip->adapter.dev, "MCP9808: setting interrupt line back to 0\n" );
									fchip->values[ fchip_offset ] = 0;
								}
								// not sure if it's better to clear pended status here or in the gpio code
								fchip->pended[ fchip_offset ] = 0;
							}
						}
					}
					/* no break */

				case MCP9808_TUPPER:
				case MCP9808_TLOWER:
				case MCP9808_TCRIT:
				case MCP9808_TA:
				case MCP9808_MID:
				case MCP9808_DID:
				case MCP9808_RES:

					//dev_info( & ichip->adapter.dev, "writing %u-byte value %04x to offset %u\n", nbytes, val, offset );
					memcpy( & ichip->therm.reg[ offset ], & val, nbytes );

					break;
				default:
					break;
				}
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
	struct pinctrl_fake *pctrl;
	struct pinctrl_fake_gpio_chip *fchip;

	ichip = container_of( therm, struct pinctrl_fake_i2c_chip, therm );
	pctrl = (struct pinctrl_fake *) ichip->adapter.algo_data;
	fchip = pctrl->fgpiochip[ 1 ];

	dev_info( & ichip->adapter.dev, "Fake MCP9808 Temperature Sensor, Copyright (C) 2016, Christopher Friedt, initializing\n" );

	if ( addr < I2C_ADDR_MCP9808_MIN || addr > I2C_ADDR_MCP9808_MAX ) {
		r = -EINVAL;
		dev_err( & ichip->adapter.dev, "invalid addr 0x%04x\n", addr );
		goto out;
	}

	memcpy( therm->reg, pinctrl_fake_i2c_mcp9808_regs_default, sizeof( therm->reg ) );
	therm->device_address = addr;

#ifdef CONFIG_PINCTRL_FAKE_I2C_MCP9808_WORKER
	r = pinctrl_fake_i2c_mcp9808_worker_init( & therm->worker, I2C_MCP9808_PERIOD_MS_DEFAULT, fchip, 0 );
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
