#ifndef PINCTRL_FAKE_SPI_MAX6662_H_
#define PINCTRL_FAKE_SPI_MAX6662_H_

#include <linux/types.h>
#include <linux/workqueue.h>
//#include "max6662-regs.h"

/*
enum {
	MAX6662_CONFIG_ALERT_MOD_COMPARATOR,
	MAX6662_CONFIG_ALERT_MOD_INTERRUPT,
};
enum {
	MAX6662_CONFIG_ALERT_POL_ACTIVE_LOW,
	MAX6662_CONFIG_ALERT_POL_ACTIVE_HIGH,
};
enum {
	MAX6662_CONFIG_ALERT_SEL_TUPPER_TLOWER_TCRIT,
	MAX6662_CONFIG_ALERT_SEL_TCRIT,
};
enum {
	MAX6662_CONFIG_ALERT_CNT_DISABLED,
	MAX6662_CONFIG_ALERT_CNT_ENABLED,
};
enum {
	MAX6662_CONFIG_ALERT_STAT_NOT_ASSERTED,
	MAX6662_CONFIG_ALERT_STAT_ASSERTED,
};
enum {
	MAX6662_CONFIG_INT_CLEAR = 1,
};
enum {
	MAX6662_CONFIG_WIN_LOCK_UNLOCKED,
	MAX6662_CONFIG_WIN_LOCK_LOCKED,
};
enum {
	MAX6662_CONFIG_CRIT_LOCK_UNLOCKED,
	MAX6662_CONFIG_CRIT_LOCK_LOCKED,
};
enum {
	MAX6662_CONFIG_SHDN_ON,
	MAX6662_CONFIG_SHDN_OFF,
};
enum {
	MAX6662_CONFIG_THYST_0P0_DEGC,
	MAX6662_CONFIG_THYST_1P5_DEGC,
	MAX6662_CONFIG_THYST_3P0_DEGC,
	MAX6662_CONFIG_THYST_6P0_DEGC,
};

enum {
	MAX6662_RES_0P5000_DEGREES_C,
	MAX6662_RES_0P2500_DEGREES_C,
	MAX6662_RES_0P1250_DEGREES_C,
	MAX6662_RES_0P0625_DEGREES_C,
};
*/

#include "pinctrl-fake-spi-max6662-worker.h"

/*
#define MAX6662_MANUFACTURER_ID_MSB 0x00
#define MAX6662_MANUFACTURER_ID_LSB 0x54

#define MAX6662_DEVICE_ID       0x04
#define MAX6662_DEVICE_REVISION 0x00

#define SPI_ADDR_MAX6662_MIN 0x18
#define SPI_ADDR_MAX6662_MAX 0x1f
*/

struct pinctrl_fake_spi_device_max6662 {
//	u16 device_address;
	u8 mem_address;
	// some registers are 16-bit and some are 8-bit, so must use u16
//	u16 reg[ MAX6662_NREG_ ];
#ifdef CONFIG_PINCTRL_FAKE_SPI_MAX6662_WORKER
	struct pinctrl_fake_spi_max6662_worker worker;
#endif // CONFIG_PINCTRL_FAKE_SPI_MAX6662_WORKER
};

int pinctrl_fake_spi_max6662_init( struct pinctrl_fake_spi_device_max6662 *eeprom, u16 cs, u16 size );
void pinctrl_fake_spi_max6662_fini( struct pinctrl_fake_spi_device_max6662 *eeprom );
int pinctrl_fake_spi_max6662_xfer( struct spi_device *spi, struct spi_transfer *transfer );

#endif /* PINCTRL_FAKE_SPI_MAX6662_H_ */
