#ifndef PINCTRL_FAKE_SPI_H_
#define PINCTRL_FAKE_SPI_H_

#include <linux/types.h>
#include <linux/spi/spi.h>
#include <linux/workqueue.h>

#include "spi-fake-at25.h"
#include "spi-fake-max6662.h"

struct pinctrl_fake_spi_chip {
	struct spi_master *master;
	struct pinctrl_fake_spi_device_at25 eeprom;
	struct pinctrl_fake_spi_device_max6662 therm;
};

int pinctrl_fake_spi_init( struct pinctrl_fake *pctrl );
void pinctrl_fake_spi_fini( struct pinctrl_fake *pctrl );

#endif /* PINCTRL_FAKE_SPI_H_ */
