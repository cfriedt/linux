#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>

#include <linux/spi/spi.h>

#include <linux/interrupt.h>

#include "pinctrl-fake.h"

static int pinctrl_fake_spi_transfer_one( struct spi_master *master, struct spi_device *spi, struct spi_transfer *transfer ) {
	return -ENOSYS;
}
/*
static int pinctrl_fake_spi_adapter_algo_master_xfer( struct spi_adapter *adap, struct spi_msg *msgs, int num ) {
	int r;
	u16 addr;

	if ( num <= 0 ) {
		r = 0;
		goto out;
	}

	addr = msgs[ 0 ].addr;

	r = -ENODEV;

	if ( addr >= SPI_ADDR_MIN_AT25 && addr <= SPI_ADDR_MAX_AT25 ) {
		r = pinctrl_fake_spi_at25_xfer( adap, msgs, num );
	}


	if ( addr >= SPI_ADDR_MCP9808_MIN && addr <= SPI_ADDR_MCP9808_MAX ) {
		r = pinctrl_fake_spi_mcp9808_xfer( adap, msgs, num );
	}

out:
	return r;
}

static u32 pinctrl_fake_spi_adapter_algo_functionality( struct spi_adapter *adap ) {
	u32 r;

	r = SPI_FUNC_SPI;

	return r;
}

*/

static struct pinctrl_fake_spi_chip pinctrl_fake_spi_master_template = {
};

int pinctrl_fake_spi_init( struct pinctrl_fake *pctrl ) {
	int r;

	struct pinctrl_fake_spi_chip *ichip;
	int i;
	char name_base[] = "a";

	dev_info( pctrl->dev, "Fake SPI Bus, Copyright (C) 2016, Christopher Friedt\n" );

	for( i = 0; i < ARRAY_SIZE( pctrl->fspichip ); i++  ) {
		ichip = kzalloc( sizeof( *ichip ), GFP_KERNEL );
		if ( NULL == ichip ) {
			dev_err( pctrl->dev, "unable to allocate memory for fake spi chip\n" );
			r = -ENOMEM;
			goto do_remove;
		}
		memcpy( ichip, & pinctrl_fake_spi_master_template, sizeof( *ichip ) );

		ichip->master = spi_alloc_master( pctrl->dev, sizeof( *( ichip->master ) ) );
		if ( NULL == ichip->master ) {
			dev_err( pctrl->dev, "unable to allocate memory for fake spi chip\n" );
			r = -ENOMEM;
		}

//		strcat( ichip->master->, "-" );
//		strcat( ichip->master->name, name_base );
//		name_base[ 0 ]++;

		r = spi_register_master( ichip->master );
		if ( EXIT_SUCCESS != r ) {
			dev_err( pctrl->dev, "spi_register_master() failed (%d)\n", r );
			goto do_remove;
		}
//		ichip->adapter.algo_data = pctrl;
//		dev_info( pctrl->dev, "added spi adapter\n" );

		pctrl->fspichip[ i ] = ichip;

#ifdef CONFIG_PINCTRL_FAKE_SPI_AT25
		r = pinctrl_fake_spi_at25_init( & ichip->eeprom, -1, SPI_AT25_MEM_SIZE_DEFAULT );
		if ( EXIT_SUCCESS != r ) {
			dev_err( pctrl->dev, "failed to add eeprom (%d)\n", r );
			goto do_remove;
		}
#endif // CONFIG_PINCTRL_FAKE_SPI_AT25

//#ifdef CONFIG_PINCTRL_FAKE_SPI_MCP9808
//		r = pinctrl_fake_spi_mcp9808_init( & ichip->therm, SPI_ADDR_MCP9808_MIN );
//		if ( EXIT_SUCCESS != r ) {
//			dev_err( pctrl->dev, "failed to add temperature sensor (%d)\n", r );
//			goto do_remove;
//		}
//#endif // CONFIG_PINCTRL_FAKE_SPI_MCP9808
	}

	r = EXIT_SUCCESS;
	goto out;

do_remove:
	for( ; i >= 0; i-- ) {
		ichip = pctrl->fspichip[ i ];
		if ( NULL != ichip ) {

//#ifdef CONFIG_PINCTRL_FAKE_SPI_MCP9808
//			pinctrl_fake_spi_mcp9808_fini( & ichip->therm );
//#endif // CONFIG_PINCTRL_FAKE_SPI_MCP9808

#ifdef CONFIG_PINCTRL_FAKE_SPI_AT25
			pinctrl_fake_spi_at25_fini( & ichip->eeprom );
#endif // CONFIG_PINCTRL_FAKE_SPI_AT25

			if ( NULL != ichip->master ) {
				spi_unregister_master( ichip->master );
			}
			kfree( ichip );
			pctrl->fspichip[ i ] = NULL;
		}
	}

out:
	return r;
}

void pinctrl_fake_spi_fini( struct pinctrl_fake *pctrl ) {

	struct pinctrl_fake_spi_chip *ichip;
	int i;

	for( i = 0; i < ARRAY_SIZE( pctrl->fspichip ); i++  ) {
		ichip = pctrl->fspichip[ i ];
		if ( NULL != ichip ) {

//			dev_info( & ichip->master->dev, "removing adapter\n" );

//#ifdef CONFIG_PINCTRL_FAKE_SPI_MCP9808
//			pinctrl_fake_spi_mcp9808_fini( & ichip->therm );
//#endif // CONFIG_PINCTRL_FAKE_SPI_MCP9808

#ifdef CONFIG_PINCTRL_FAKE_SPI_AT25
			pinctrl_fake_spi_at25_fini( & ichip->eeprom );
#endif // CONFIG_PINCTRL_FAKE_SPI_AT25

			if ( NULL != ichip->master ) {
				spi_unregister_master( ichip->master );
			}

			kfree( ichip );
			pctrl->fspichip[ i ] = NULL;
		}
	}

	dev_info( pctrl->dev, "Fake SPI Bus Unloading..\n" );
}
