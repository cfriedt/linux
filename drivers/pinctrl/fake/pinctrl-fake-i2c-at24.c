#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>

#include <linux/i2c.h>

#include <linux/interrupt.h>

#include "pinctrl-fake.h"

int pinctrl_fake_i2c_at24_xfer( struct i2c_adapter *adap, struct i2c_msg *msgs, int num ) {

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

		dev_dbg( & adap->dev, "AT24 processing msg %u / %u (%s)\n", i + 1, num, read ? "read" : "write" );

		if ( read ) {

			offset = ichip->eeprom.mem_address;
			nbytes = msg->len;

			// XXX: handle address roll-over
			if ( offset >= ichip->eeprom.mem_size || offset + nbytes >= ichip->eeprom.mem_size ) {
				dev_err( & adap->dev, "offset (%04x) and nbytes (%u) combination invalid\n", offset, nbytes );
				r = -EINVAL;
				goto out;
			}

			if ( nbytes >= 0 ) {
				memcpy( msg->buf, & ichip->eeprom.mem[ offset ], nbytes );
				dev_dbg( & adap->dev, "read %u bytes from eeprom at offset 0x%04x\n", nbytes, offset );
				offset += nbytes;
				offset %= ichip->eeprom.mem_size;
				ichip->eeprom.mem_address = offset;
			}

			r++;

		} else {

			nbytes = msg->len;
			dev_dbg( & adap->dev, "pinctrl_fake_i2c_at24_xfer(): (write) nbytes %d\n", nbytes );
			offset = ichip->eeprom.mem_address;

			if ( msg == & msg[ 0 ] ) {
				dev_dbg( & adap->dev, "pinctrl_fake_i2c_at24_xfer(): (write) msg == & msg[ 0 ]\n" );
				if ( msg->len < 2 ) {
					dev_err( & adap->dev, "invalid msg len (%u)\n", msg->len );
					r = -EINVAL;
					goto out;
				}

				offset = ( msg->buf[ 0 ] << 8 ) | msg->buf[ 1 ];
				nbytes -= 2;
				dev_dbg( & adap->dev, "pinctrl_fake_i2c_at24_xfer(): (write) offset %04x\n", offset );
				dev_dbg( & adap->dev, "pinctrl_fake_i2c_at24_xfer(): (write) nbytes %d\n", nbytes );
			}

			// XXX: handle address roll-over
			if ( offset + nbytes >= ichip->eeprom.mem_size ) {
				dev_err( & adap->dev, "offset (%04x) and nbytes (%u) combination invalid\n", offset, nbytes );
				r = -EINVAL;
				goto out;
			}

			if ( nbytes > 0 ) {
				memcpy( & ichip->eeprom.mem[ offset ], & msg->buf[ 2 ], nbytes );
				dev_dbg( & adap->dev, "wrote %u bytes to eeprom at offset 0x%04x\n", nbytes, offset );
				offset += nbytes;
				offset %= ichip->eeprom.mem_size;
				ichip->eeprom.mem_address = offset;
			} else {
				dev_dbg( & adap->dev, "dummy write set address pointer to offset 0x%04x\n", offset );
			}

			r++;
		}
	}

out:
	dev_dbg( & adap->dev, "pinctrl_fake_i2c_at24_xfer(): returning %d\n", r );
	return r;
}

int pinctrl_fake_i2c_at24_init( struct pinctrl_fake_i2c_device_at24 *eeprom, u16 addr, u16 size ) {
	int r;

	struct pinctrl_fake_i2c_chip *ichip;

	ichip = container_of( eeprom, struct pinctrl_fake_i2c_chip, eeprom );

	dev_info( & ichip->adapter.dev, "Fake AT24 EEPROM, Copyright (C) 2016, Christopher Friedt, initializing\n" );

	if ( size < I2C_AT24_MEM_SIZE_MIN || size > I2C_AT24_MEM_SIZE_MAX ) {
		dev_err( & ichip->adapter.dev, "EEPROM size %u invalid\n", size );
		r = -EINVAL;
		goto out;
	}

	eeprom->mem = kzalloc( size, GFP_KERNEL );
	if ( NULL == eeprom->mem ) {
		dev_err( & ichip->adapter.dev, "unable to allocate EEPROM memory of size %u\n", size );
		r = -ENOMEM;
		goto out;
	}
	eeprom->mem_size = size;
	eeprom->device_address = addr;
	eeprom->mem_address = 0;

	dev_info( & ichip->adapter.dev, "added AT24 at address 0x%04x\n", addr );

	r = EXIT_SUCCESS;

out:
	return r;
}

void pinctrl_fake_i2c_at24_fini( struct pinctrl_fake_i2c_device_at24 *eeprom ) {

	struct pinctrl_fake_i2c_chip *ichip;
	u16 addr;

	ichip = container_of( eeprom, struct pinctrl_fake_i2c_chip, eeprom );

	addr = eeprom->device_address;

	if ( NULL != eeprom->mem ) {
		kfree( eeprom->mem );
		eeprom->mem = NULL;
	}
	eeprom->device_address = -1;
	eeprom->mem_size = 0;
	eeprom->mem_address = 0;

	dev_info( & ichip->adapter.dev, "removed AT24 at address 0x%04x\n", addr );
}
