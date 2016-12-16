#ifndef PINCTRL_FAKE_SPI_MAX6662_WORKER_H_
#define PINCTRL_FAKE_SPI_MAX6662_WORKER_H_

#include <linux/types.h>
#include <linux/workqueue.h>

#define SPI_MAX6662_PERIOD_MS_MIN 1000
#define SPI_MAX6662_PERIOD_MS_MAX 10000
#define SPI_MAX6662_PERIOD_MS_DEFAULT SPI_MAX6662_PERIOD_MS_MIN

struct pinctrl_fake_gpio_chip;

struct pinctrl_fake_spi_max6662_worker {
	u16 period_ms;
	struct delayed_work dwork;
	struct pinctrl_fake_gpio_chip *fchip;
	int fchip_offset;
};

int pinctrl_fake_spi_max6662_worker_init( struct pinctrl_fake_spi_max6662_worker *worker, unsigned period_ms, struct pinctrl_fake_gpio_chip *fchip, int fchip_offset );
void pinctrl_fake_spi_max6662_worker_fini( struct pinctrl_fake_spi_max6662_worker *worker );

#endif /* PINCTRL_FAKE_SPI_MAX6662_WORKER_H_ */
