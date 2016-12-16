#include "spi-fake-max6662.h"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>

#include <linux/spi/spi.h>

#include <linux/interrupt.h>
#include "../../../include/linux/pinctrl-fake.h"

/*
#define DECL_DEF( x ) \
	[ MAX6662_ ## x ] = MAX6662_ ## x ## _DEFAULT
static const u16 pinctrl_fake_spi_max6662_regs_default[ MAX6662_NREG_ ] = {
	DECL_DEF( RFU ),
	DECL_DEF( CONFIG ),
	DECL_DEF( TUPPER ),
	DECL_DEF( TLOWER ),
	DECL_DEF( TCRIT ),
	DECL_DEF( TA ),
	DECL_DEF( MID ),
	DECL_DEF( DID ),
	DECL_DEF( RES ),
};
#undef DECL_DEF

#define DECL_SIZE( x ) \
	[ MAX6662_ ## x ] = MAX6662_ ## x ## _SIZE_BYTES
static const u8 pinctrl_fake_spi_max6662_regs_size[ MAX6662_NREG_ ] = {
	DECL_SIZE( RFU ),
	DECL_SIZE( CONFIG ),
	DECL_SIZE( TUPPER ),
	DECL_SIZE( TLOWER ),
	DECL_SIZE( TCRIT ),
	DECL_SIZE( TA ),
	DECL_SIZE( MID ),
	DECL_SIZE( DID ),
	DECL_SIZE( RES ),
};
#undef DECL_SIZE

#define DECL_WMASK( x ) \
	[ MAX6662_ ## x ] = MAX6662_ ## x ## _WRITE_MASK
static const u16 pinctrl_fake_spi_max6662_regs_write_mask[ MAX6662_NREG_ ] = {
	DECL_WMASK( RFU ),
	DECL_WMASK( CONFIG ),
	DECL_WMASK( TUPPER ),
	DECL_WMASK( TLOWER ),
	DECL_WMASK( TCRIT ),
	DECL_WMASK( TA ),
	DECL_WMASK( MID ),
	DECL_WMASK( DID ),
	DECL_WMASK( RES ),
};
#undef DECL_WMASK

#define DECL_RMASK( x ) \
	[ MAX6662_ ## x ] = MAX6662_ ## x ## _READ_MASK
static const u16 pinctrl_fake_spi_max6662_regs_read_mask[ MAX6662_NREG_ ] = {
	DECL_RMASK( RFU ),
	DECL_RMASK( CONFIG ),
	DECL_RMASK( TUPPER ),
	DECL_RMASK( TLOWER ),
	DECL_RMASK( TCRIT ),
	DECL_RMASK( TA ),
	DECL_RMASK( MID ),
	DECL_RMASK( DID ),
	DECL_RMASK( RES ),
};
#undef DECL_RMASK


static char spimsg_to_str_buf[ 1024 ];
static char *spimsg_to_str( struct spi_msg *m ) {
	char *p = spimsg_to_str_buf;
	int r;
	int i;
	memset( p, 0, sizeof( spimsg_to_str_buf ) );
	r = sprintf( p, "addr: %04x, flags: %04x, len: %u, buf: [", m->addr, m->flags, m->len );
	p += r;
	for( i = 0; i < m->len; i++ ) {
		if ( 0 == i ) {
			*p = ' ';
			p++;
		}
		r = sprintf( p, "%02x", m->buf[ i ] );
		p += r;
		if ( i < m->len - 1 ) {
			*p = ',';
			p++;
		}
		*p = ' ';
		p++;
	}
	*p = ']';
	p++;

	return spimsg_to_str_buf;
}
*/

int pinctrl_fake_spi_max6662_xfer( struct spi_device *spi, struct spi_transfer *transfer ) {
/*
	int r;

	int i;
	struct pinctrl_fake_spi_chip *ichip;
	struct pinctrl_fake_spi_device_max6662 *therm;
	struct delayed_work *dwork;
	struct pinctrl_fake_spi_max6662_worker *worker;
	struct pinctrl_fake *pctrl;
	struct spi_msg *msg;
	bool read;
	unsigned register_pointer;
	int nbytes;
	u16 val;
	struct pinctrl_fake_gpio_chip *fchip;
	int fchip_offset;

	pctrl = (struct pinctrl_fake *) adap->algo_data;
	ichip = container_of( adap, struct pinctrl_fake_spi_chip, adapter );
	therm = & ichip->therm;

	BUG_ON( MAX6662_RFU_DEFAULT != therm->reg[ MAX6662_RFU ] );

	r = 0;
	for( i = 0; i < num; i++ ) {
		msg = & msgs[ i ];

		read = 0 != ( msg->flags & SPI_M_RD );

		if ( read ) {

			register_pointer = therm->mem_address;
			nbytes = msg->len;

			if ( 0 == register_pointer || register_pointer >= MAX6662_NREG_ ) {
				dev_err( & ichip->adapter.dev, "offset %u invalid\n", register_pointer );
				r = -EINVAL;
				goto out;
			}

			if ( nbytes < 0 ) {
				dev_err( & ichip->adapter.dev, "nbytes (%d) invalid\n", nbytes );
				r = -EINVAL;
				goto out;
			}

			if ( nbytes != pinctrl_fake_spi_max6662_regs_size[ register_pointer ] ) {
				dev_warn( & ichip->adapter.dev, "nbytes (%u) not correct length (%u) for offset (%u)\n",
					nbytes, pinctrl_fake_spi_max6662_regs_size[ register_pointer ], register_pointer );
			}

			nbytes = min( (unsigned)nbytes, (unsigned)pinctrl_fake_spi_max6662_regs_size[ register_pointer ] );

			if ( nbytes >= 0 ) {

				val = therm->reg[ register_pointer ];
				val &= pinctrl_fake_spi_max6662_regs_read_mask[ register_pointer ];

				if ( sizeof( u16 ) == nbytes ) {
					// protocol specifies that MSB must be transferred first
					val = cpu_to_be16( val );
				}

				memcpy( msg->buf, & val, nbytes );
			}

			if ( MAX6662_CONFIG == register_pointer ) {
				dev_dbg( & ichip->adapter.dev, "MAX6662: spi-read: msg %u: %s\n", i, spimsg_to_str( & msgs[ i ] ) );
			}

			r++;

		} else {

			register_pointer = msg->buf[ 0 ];
			nbytes = msg->len - 1;

			if ( MAX6662_CONFIG == register_pointer ) {
				dev_dbg( & ichip->adapter.dev, "MAX6662: spi-write: msg %u: %s\n", i, spimsg_to_str( & msgs[ i ] ) );
			}

			if ( 0 == register_pointer || register_pointer >= MAX6662_NREG_ ) {
				dev_err( & ichip->adapter.dev, "offset %u invalid\n", register_pointer );
				r = -EINVAL;
				goto out;
			}

			if ( nbytes > pinctrl_fake_spi_max6662_regs_size[ register_pointer ] ) {
				dev_err( & ichip->adapter.dev, "nbytes (%u) invalid for offset (%u)\n", nbytes, register_pointer );
				r = -EINVAL;
				goto out;
			}

			// we set the register pointer even if nbytes is 0
			therm->mem_address = register_pointer;

			if ( nbytes > 0 ) {

				if ( nbytes != pinctrl_fake_spi_max6662_regs_size[ register_pointer ] ) {
					dev_warn( & ichip->adapter.dev, "nbytes (%u) not correct length (%u) for offset (%u)\n",
						nbytes, pinctrl_fake_spi_max6662_regs_size[ register_pointer ], register_pointer );
				}

				if ( sizeof( u16 ) == nbytes ) {
					// protocol specifies that MSB must be transferred first
					val = ( (u16) msg->buf[ 1 ] ) << 8;
					val |= (u16) msg->buf[ 2 ];
					val = be16_to_cpu( val );
				} else {
					val = msg->buf[ 1 ];
				}

				if ( 0 == pinctrl_fake_spi_max6662_regs_write_mask[ register_pointer ] ) {
					dev_warn( & ichip->adapter.dev, "attempt to write value 0x%04x to read-only register at offset (%u)\n", val, register_pointer );
					r++;
					continue;
				}

				val &= pinctrl_fake_spi_max6662_regs_write_mask[ register_pointer];

				switch( register_pointer ) {

				case MAX6662_CONFIG:

					if ( MAX6662_CONFIG == register_pointer ) {

						// XXX: TODO: check Alert Mod. (bit 0)

						// CLEAR INTERRUPT
						if ( val & MAX6662_CONFIG_INT_CLEAR_MASK ) {
							// Int. Clear always reads as 0, so clear it before writing to memory
							val &= ~MAX6662_CONFIG_INT_CLEAR_MASK;

							// Alert Stat. will be 0, after writing to Int. Clear
							val &= ~MAX6662_CONFIG_ALERT_STAT_MASK;

							// de-assert interrupt line
							fchip = therm->worker.fchip;
							fchip_offset = therm->worker.fchip_offset;
							if ( !( NULL == fchip || fchip_offset < 0 || fchip_offset >= fchip->npins ) ) {

								if ( MAX6662_CONFIG_ALERT_POL_ACTIVE_LOW == ( val & MAX6662_CONFIG_ALERT_POL_MASK ) ) {
									// active-low, deasserting interrupt means setting it high again
									fchip->values[ fchip_offset ] = 1;
									dev_dbg( & ichip->adapter.dev, "MAX6662: setting interrupt line back to 1\n" );
								} else {
									// active-high, deasserting interrupt means setting it low again
									dev_dbg( & ichip->adapter.dev, "MAX6662: setting interrupt line back to 0\n" );
									fchip->values[ fchip_offset ] = 0;
								}
								// not sure if it's better to clear pended status here or in the gpio code
								fchip->pended[ fchip_offset ] = 0;
							}
						}

						// ENABLE / DISABLE
#ifdef CONFIG_PINCTRL_FAKE_SPI_MAX6662_WORKER
						if ( ( therm->reg[ MAX6662_CONFIG ] & MAX6662_CONFIG_ALERT_CNT_MASK ) != ( val & MAX6662_CONFIG_ALERT_CNT_MASK ) ) {
							worker = & therm->worker;
							dwork = & worker->dwork;
							if ( MAX6662_CONFIG_ALERT_CNT_DISABLED == ( val & MAX6662_CONFIG_ALERT_CNT_MASK ) ) {
								dev_dbg( & ichip->adapter.dev, "MAX6662: disabling worker\n" );
								cancel_delayed_work( dwork );
							} else {
								dev_dbg( & ichip->adapter.dev, "MAX6662: enabling worker\n" );
								schedule_delayed_work( dwork, msecs_to_jiffies( worker->period_ms ) );
							}
						}
#endif // CONFIG_PINCTRL_FAKE_SPI_MAX6662_WORKER
					}
					// no break

				case MAX6662_TUPPER:
				case MAX6662_TLOWER:
				case MAX6662_TCRIT:
				case MAX6662_TA:
				case MAX6662_MID:
				case MAX6662_DID:
				case MAX6662_RES:

					therm->reg[ register_pointer ] = val;

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
*/
	return -ENOSYS;
}

int pinctrl_fake_spi_max6662_init( struct pinctrl_fake_spi_device_max6662 *eeprom, u16 cs, u16 size ) {
/*
	int r;

	struct pinctrl_fake_spi_chip *ichip;
	struct pinctrl_fake *pctrl;
	struct pinctrl_fake_gpio_chip *fchip;

	ichip = container_of( therm, struct pinctrl_fake_spi_chip, therm );
	pctrl = (struct pinctrl_fake *) ichip->adapter.algo_data;
	fchip = pctrl->fgpiochip[ 1 ];

	dev_info( & ichip->adapter.dev, "Fake MAX6662 Temperature Sensor, Copyright (C) 2016, Christopher Friedt\n" );

	memcpy( therm->reg, pinctrl_fake_spi_max6662_regs_default, sizeof( therm->reg ) );
	therm->device_address = addr;

#ifdef CONFIG_PINCTRL_FAKE_SPI_MAX6662_WORKER
	r = pinctrl_fake_spi_max6662_worker_init( & therm->worker, SPI_MAX6662_PERIOD_MS_DEFAULT, fchip, 0 );
	if ( EXIT_SUCCESS != r ) {
		dev_err( & ichip->adapter.dev, "pinctrl_fake_spi_max6662_worker_init() failed (%d)\n", r );
		goto out;
	}
#endif // CONFIG_PINCTRL_FAKE_SPI_MAX6662_WORKER

	r = EXIT_SUCCESS;

	dev_info( & ichip->adapter.dev, "added MAX6662 at address 0x%04x\n", addr );

out:
	return r;
*/
	return -ENOSYS;
}

void pinctrl_fake_spi_max6662_fini( struct pinctrl_fake_spi_device_max6662 *therm ) {
/*
	struct pinctrl_fake_spi_chip *ichip;
	u16 addr;

	ichip = container_of( therm, struct pinctrl_fake_spi_chip, therm );
	addr = therm->device_address;

#ifdef CONFIG_PINCTRL_FAKE_SPI_MAX6662_WORKER
	pinctrl_fake_spi_max6662_worker_fini( & therm->worker );
#endif // CONFIG_PINCTRL_FAKE_SPI_MAX6662_WORKER

	memset( therm, 0, sizeof( *therm ) );

	dev_info( & ichip->adapter.dev, "removed MAX6662 at address 0x%04x\n", addr );
*/
}
