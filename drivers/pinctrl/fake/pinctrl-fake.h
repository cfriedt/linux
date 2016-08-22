#ifndef PINCTRL_FAKE_H_
#define PINCTRL_FAKE_H_

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-fake-misc.h"
#include "pinctrl-fake-gpio.h"
#include "pinctrl-fake-gpio-toggler.h"

struct pinctrl_fake {
	struct device *dev;
	struct pinctrl_desc pctldesc;
	struct pinctrl_dev *pctldev;
#ifdef CONFIG_PINCTRL_FAKE_GPIO
    #define PINCTRL_FAKE_N_GPIO_CHIPS 2
	struct pinctrl_fake_gpio_chip *fgpiochip[ PINCTRL_FAKE_N_GPIO_CHIPS ];
#endif // CONFIG_PINCTRL_FAKE_GPIO
//	void __iomem *regs;
//	raw_spinlock_t lock;
//	unsigned intr_lines[16];
//	u32 saved_intmask;
//	struct pinctrl_fake_pin_context *saved_pin_context;
};

#endif // PINCTRL_FAKE_H_
