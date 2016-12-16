#ifndef PINCTRL_FAKE_SPI_AT25_H_
#define PINCTRL_FAKE_SPI_AT25_H_

#include <linux/types.h>

#define SPI_AT25_MEM_SIZE_MIN ( 1  * 1024 / 8 ) // 128 bytes
#define SPI_AT25_MEM_SIZE_MAX ( 4  * 1024 / 8 ) // 512 bytes
#define SPI_AT25_MEM_SIZE_DEFAULT SPI_AT25_MEM_SIZE_MIN

struct pinctrl_fake_spi_device_at25 {
	u16 mem_address;
	u16 mem_size;
	u8 *mem;
	u8 status_register;
};

int pinctrl_fake_spi_at25_init( struct pinctrl_fake_spi_device_at25 *eeprom, u16 cs, u16 size );
void pinctrl_fake_spi_at25_fini( struct pinctrl_fake_spi_device_at25 *eeprom );
int pinctrl_fake_spi_at25_xfer( struct spi_device *spi, struct spi_transfer *transfer );

#endif /* PINCTRL_FAKE_SPI_AT25_H_ */
