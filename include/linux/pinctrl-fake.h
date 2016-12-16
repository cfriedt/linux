#ifndef PINCTRL_FAKE_H_
#define PINCTRL_FAKE_H_

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pinctrl/pinctrl.h>

#include "gpio-fake.h"
/*
#include "pinctrl-fake-i2c.h"
#include "pinctrl-fake-spi.h"
*/

struct pinctrl_fake_group {
	char *name;
	unsigned npins;
	unsigned *pins;
};

struct pinctrl_fake_pmx_func {
	char *name;
	unsigned ngroups;
	char **groups;
};

struct pinctrl_fake {
	struct device *dev;
	struct pinctrl_desc pctldesc;
	struct pinctrl_dev *pctldev;
	unsigned ngroups;
	struct pinctrl_fake_group *groups;
	unsigned nmuxes;
	struct pinctrl_fake_pmx_func *muxes;

	//	void __iomem *regs;
	//  raw_spinlock_t lock;
	//	unsigned intr_lines[16];
	//	u32 saved_intmask;
	//	struct pinctrl_fake_pin_context *saved_pin_context;
	struct list_head head;

#ifdef CONFIG_FAKE_GPIO
	struct fake_gpio_chip fgpiochip;
#endif // CONFIG_PINCTRL_FAKE_GPIO
/*
#ifdef CONFIG_PINCTRL_FAKE_I2C
	#define PINCTRL_FAKE_N_I2C_CHIPS_MAX 26
    #define PINCTRL_FAKE_N_I2C_CHIPS 1
	#if PINCTRL_FAKE_N_I2C_CHIPS > PINCTRL_FAKE_N_I2C_CHIPS_MAX
		#error Too many Fake I2C Chips!
	#endif
	struct pinctrl_fake_i2c_chip *fi2cchip[ PINCTRL_FAKE_N_I2C_CHIPS ];
#endif // CONFIG_PINCTRL_FAKE_I2C
#ifdef CONFIG_PINCTRL_FAKE_SPI
	#define PINCTRL_FAKE_N_SPI_CHIPS_MAX 26
    #define PINCTRL_FAKE_N_SPI_CHIPS 1
	#if PINCTRL_FAKE_N_SPI_CHIPS > PINCTRL_FAKE_N_SPI_CHIPS_MAX
		#error Too many Fake SPI Chips!
	#endif
	struct pinctrl_fake_spi_chip *fspichip[ PINCTRL_FAKE_N_SPI_CHIPS ];
#endif // CONFIG_PINCTRL_FAKE_SPI
*/
};

void pinctrl_fake_hello( void );

#endif // PINCTRL_FAKE_H_
