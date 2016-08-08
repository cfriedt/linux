#ifndef PINCTRL_FAKE_H_
#define PINCTRL_FAKE_H_

#define PINCTRL_FAKE_N_GPIO_CHIPS 2

struct pinctrl_fake {
	struct device *dev;
	struct pinctrl_desc pctldesc;
	struct pinctrl_dev *pctldev;
	struct gpio_chip gpiochip[ PINCTRL_FAKE_N_GPIO_CHIPS ];
	//void __iomem *regs;
	raw_spinlock_t lock;
	//unsigned intr_lines[16];
	//const struct chv_community *community;
	//u32 saved_intmask;
	//struct chv_pin_context *saved_pin_context;
};

struct pinctrl_fake_gpio_pinrange {
	const char *name;
	struct gpio_chip *chip;
	unsigned npins;
	unsigned *pins;
	u8 *values;
	u8 *directions;
	u8 *irq_types;
};

#endif // PINCTRL_FAKE_H_
