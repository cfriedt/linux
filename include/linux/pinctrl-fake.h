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

	// pinctrl interface
	struct pinctrl_desc pctldesc;
	struct pinctrl_dev *pctldev;
	unsigned ngroups;
	struct pinctrl_fake_group *groups;
	unsigned nmuxes;
	struct pinctrl_fake_pmx_func *muxes;
	unsigned nmappings;
	struct pinctrl_map *mappings;
//	void __iomem *regs;
	//  raw_spinlock_t lock;
	//	unsigned intr_lines[16];
	//	u32 saved_intmask;
	//	struct pinctrl_fake_pin_context *saved_pin_context;

	// fake gpio interface
#ifdef CONFIG_GPIO_FAKE
	unsigned ngpiochips;
	struct gpio_fake_chip *fgpiochip;
#endif // CONFIG_GPIO_FAKE

/*
#ifdef CONFIG_PINCTRL_FAKE_I2C
	struct i2c_fake_chip *fi2cchip;
#endif // CONFIG_PINCTRL_FAKE_I2C
*/

/*
#ifdef CONFIG_PINCTRL_FAKE_SPI
	struct pinctrl_fake_spi_chip *fspichip;
#endif // CONFIG_PINCTRL_FAKE_SPI
*/

// mmc

// uart

// a2d

// dac

	struct list_head head;
};

static inline void pinctrl_pin_desc_attach_pinctrl_fake( struct pinctrl_pin_desc *pin, struct pinctrl_fake *pctrl ) {
	*((struct pinctrl_fake **) & pin->drv_data ) = pctrl;
}
static inline struct pinctrl_fake *pinctrl_pin_desc_to_pinctrl_fake( struct pinctrl_pin_desc *pin ) {
	struct pinctrl_fake *r;
	r = (struct pinctrl_fake *) pin->drv_data;
	return r;
}

bool pinctrl_fake_valid_instance( struct pinctrl_fake *pctrl );

#endif // PINCTRL_FAKE_H_
