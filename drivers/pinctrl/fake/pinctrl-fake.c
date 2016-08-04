/*
 // Fake-pinctrl driver
 *
 // Copyright (C) 2016, Christopher Friedt
 // Author: Christopher Friedt <chrisfriedt@gmail.com>
 *
 // This program is free software; you can redistribute it and/or modify
 // it under the terms of the GNU General Public License version 2 as
 // published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/platform_device.h>

#ifndef MODULE_NAME
#define MODULE_NAME "pinctrl-fake"
#endif

#ifndef _pr_info
#define _pr_info( fmt, args... ) pr_info( MODULE_NAME ": " fmt, ##args )
#endif

#ifndef _pr_err
#define _pr_err( fmt, args... ) pr_err( MODULE_NAME ": " fmt, ##args )
#endif

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif // EXIT_SUCCESS

/*
 * Example from [1]
 *
 *        A   B   C   D   E   F   G   H
 *      +---+
 *   8  | o | .   .   .   .   .   .   .
 *      |   |
 *   7  | o | .   .   .   .   .   .   .
 *      |   |
 *   6  | o | .   .   .   .   .   .   .
 *      +---+---+
 *   5  | o | o | .   .   .   .   .   .
 *      +---+---+               +---+
 *   4    .   .   .   .   .   . | o | .
 *                              |   |
 *   3    .   .   .   .   .   . | o | .
 *                              |   |
 *   2    .   .   .   .   .   . | o | .
 *      +-------+-------+-------+---+---+
 *   1  | o   o | o   o | o   o | o | o |
 *      +-------+-------+-------+---+---+
 *
 *   Some additional annotation notes:
 *   o: pin available as gpio and / or other pinmux function
 *   .: pin not available for mux
 *
 *   [1] Documentation/pinctrl.txt
 */

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

// fwd declaration
static struct pinctrl_fake pinctrl_fake;

// we only enumerate the muxable portion of the named pins
static const struct pinctrl_pin_desc pinctrl_fake_pins[] = {
		PINCTRL_PIN(  0, "A8" ),
		PINCTRL_PIN(  8, "A7" ),
		PINCTRL_PIN( 16, "A6" ),
		PINCTRL_PIN( 24, "A5" ),
		PINCTRL_PIN( 25, "B5" ),
		PINCTRL_PIN( 38, "G4" ),
		PINCTRL_PIN( 46, "G3" ),
		PINCTRL_PIN( 54, "G2" ),
		PINCTRL_PIN( 56, "A1" ),
		PINCTRL_PIN( 57, "B1" ),
		PINCTRL_PIN( 58, "C1" ),
		PINCTRL_PIN( 59, "D1" ),
		PINCTRL_PIN( 60, "E1" ),
		PINCTRL_PIN( 61, "F1" ),
		PINCTRL_PIN( 62, "G1" ),
		PINCTRL_PIN( 63, "H1" ),
};

static const unsigned pinctrl_fake_gpiochip_a_pins[] = { 0, 8, 16, 24, 25, };
static const unsigned pinctrl_fake_gpiochip_b_pins[] = { 38, 46, 54, 56, 57, 58, 59, 60, 61, 62, 63, };
static const unsigned pinctrl_fake_spi0_0_pins[]     = { 0, 8, 16, 24, };
static const unsigned pinctrl_fake_spi0_1_pins[]     = { 38, 24, 54, 62, };
static const unsigned pinctrl_fake_i2c0_pins[]       = { 24, 25, };
static const unsigned pinctrl_fake_mmc0_1_pins[]     = { 56, 57, };
static const unsigned pinctrl_fake_mmc0_2_pins[]     = { 58, 59, };
static const unsigned pinctrl_fake_mmc0_3_pins[]     = { 60, 61, 62, 63, };

struct pinctrl_fake_gpio_pinrange {
	const char *name;
	struct gpio_chip *chip;
	unsigned npins;
	unsigned *pins;
};

static const struct pinctrl_fake_gpio_pinrange pinctrl_fake_gpio_pinrange[] = {
	{ .name = "gpiochip_a_grp", .chip = & pinctrl_fake.gpiochip[ 0 ], .npins = ARRAY_SIZE( pinctrl_fake_gpiochip_a_pins ), .pins = pinctrl_fake_gpiochip_a_pins,  },
	{ .name = "gpiochip_b_grp", .chip = & pinctrl_fake.gpiochip[ 1 ], .npins = ARRAY_SIZE( pinctrl_fake_gpiochip_b_pins ), .pins = pinctrl_fake_gpiochip_b_pins,  },
};

struct pinctrl_fake_group {
	const char *name;
	const unsigned int *pins;
	const unsigned npins;
};

#define PIN_GROUP( n )                                  \
{                                                       \
	.name = #n "_grp",                                  \
	.pins = pinctrl_fake_ ## n ## _pins,                \
	.npins = ARRAY_SIZE( pinctrl_fake_ ## n ## _pins ), \
}

static const struct pinctrl_fake_group pinctrl_fake_groups[] = {
	PIN_GROUP( gpiochip_a ),
	PIN_GROUP( gpiochip_b ),
	PIN_GROUP( spi0_0 ),
	PIN_GROUP( spi0_1 ),
	PIN_GROUP( i2c0 ),
	PIN_GROUP( mmc0_1 ),
	PIN_GROUP( mmc0_2 ),
	PIN_GROUP( mmc0_3 ),
};

static int pinctrl_fake_get_groups_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE( pinctrl_fake_groups );
}

static const char *pinctrl_fake_get_group_name(struct pinctrl_dev *pctldev,
				      unsigned group)
{
	const char *r;

	if ( group >= ARRAY_SIZE( pinctrl_fake_groups ) ) {
		r = NULL;
		goto out;
	}

	r = pinctrl_fake_groups[ group ].name;

out:
	return r;
}

static int pinctrl_fake_get_group_pins(struct pinctrl_dev *pctldev, unsigned group,
			      const unsigned **pins, unsigned *npins)
{
	int r;

	if ( group >= ARRAY_SIZE( pinctrl_fake_groups ) ) {
		r = -EINVAL;
		goto out;
	}

	*pins = pinctrl_fake_groups[ group ].pins;
	*npins = pinctrl_fake_groups[ group ].npins;

	r = EXIT_SUCCESS;

out:
	return r;
}

static void pinctrl_fake_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
			     unsigned offset)
{
	/*
	struct pinctrl_fake *pctrl = pinctrl_dev_get_drvdata(pctldev);
	unsigned long flags;
	u32 ctrl0, ctrl1;
	bool locked;

	raw_spin_lock_irqsave(&pctrl->lock, flags);

	ctrl0 = readl(pinctrl_fake_padreg(pctrl, offset, CHV_PADCTRL0));
	ctrl1 = readl(pinctrl_fake_padreg(pctrl, offset, CHV_PADCTRL1));
	locked = pinctrl_fake_pad_locked(pctrl, offset);

	raw_spin_unlock_irqrestore(&pctrl->lock, flags);

	if (ctrl0 & CHV_PADCTRL0_GPIOEN) {
		seq_puts(s, "GPIO ");
	} else {
		u32 mode;

		mode = ctrl0 & CHV_PADCTRL0_PMODE_MASK;
		mode >>= CHV_PADCTRL0_PMODE_SHIFT;

		seq_printf(s, "mode %d ", mode);
	}

	seq_printf(s, "ctrl0 0x%08x ctrl1 0x%08x", ctrl0, ctrl1);

	if (locked)
		seq_puts(s, " [LOCKED]");
	*/
}

static const struct pinctrl_ops pinctrl_fake_ops = {
	.get_groups_count = pinctrl_fake_get_groups_count,
	.get_group_name = pinctrl_fake_get_group_name,
	.get_group_pins = pinctrl_fake_get_group_pins,
	.pin_dbg_show = pinctrl_fake_pin_dbg_show,
};

static const char *pinctrl_fake_gpiochip_a_groups[] = { "gpiochip_a_grp", };
static const char *pinctrl_fake_gpiochip_b_groups[] = { "gpiochip_b_grp", };
static const char *pinctrl_fake_spi0_groups[] = { "spi0_0_grp", "spi0_1_grp", };
static const char *pinctrl_fake_i2c0_groups[] = { "i2c0_grp", };
static const char *pinctrl_fake_mmc0_groups[] = { "mmc0_0_grp", "mmc0_1_grp", "mmc0_1_grp", };

struct pinctrl_fake_pmx_func {
	const char *name;
	const unsigned ngroups;
	const char * const *groups;
};

#define FUNCTION( n )                                       \
{                                                           \
	.name = #n,                                             \
	.ngroups = ARRAY_SIZE( pinctrl_fake_ ## n ## _groups ),	\
	.groups = pinctrl_fake_ ## n ## _groups,                \
}

static const struct pinctrl_fake_pmx_func pinctrl_fake_pmx_funcs[] = {
	FUNCTION( gpiochip_a ),
	FUNCTION( gpiochip_b ),
	FUNCTION( spi0 ),
	FUNCTION( i2c0 ),
	FUNCTION( mmc0 ),
};

static int pinctrl_fake_get_functions_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE( pinctrl_fake_pmx_funcs );
}

static const char *pinctrl_fake_get_function_name(struct pinctrl_dev *pctldev,
					 unsigned function)
{
	const char *r;

	if ( function >= ARRAY_SIZE( pinctrl_fake_pmx_funcs ) ) {
		r = NULL;
		goto out;
	}

	r = pinctrl_fake_pmx_funcs[ function ].name;

out:
	return r;
}

static int pinctrl_fake_get_function_groups(struct pinctrl_dev *pctldev,
				   unsigned function,
				   const char *const **groups,
				   unsigned *const ngroups)
{
	int r;

	if ( function >= ARRAY_SIZE( pinctrl_fake_pmx_funcs ) ) {
		r = -EINVAL;
		goto out;
	}

	*groups = pinctrl_fake_pmx_funcs[ function ].groups;
	*ngroups = pinctrl_fake_pmx_funcs[ function ].ngroups;
	r = EXIT_SUCCESS;

out:
	return r;
}

static int pinctrl_fake_pinmux_set_mux(struct pinctrl_dev *pctldev, unsigned function,
			      unsigned group)
{
	int r;

	struct pinctrl_fake *pctrl = pinctrl_dev_get_drvdata(pctldev);

	const struct pinctrl_fake_pmx_func *func;
	const struct pinctrl_fake_group *grp;
	unsigned long flags;
	int i;

	if ( function >= ARRAY_SIZE( pinctrl_fake_pmx_funcs ) || group >= ARRAY_SIZE( pinctrl_fake_groups ) ) {
		r = -EINVAL;
		goto out;
	}

	func = & pinctrl_fake_pmx_funcs[ function ];
	grp = & pinctrl_fake_groups[ group ];

	raw_spin_lock_irqsave( & pctrl->lock, flags );

	// Check first that the pad is not locked
/*
	for (i = 0; i < grp->npins; i++) {
		if (pinctrl_fake_pad_locked(pctrl, grp->pins[i])) {
			dev_warn(pctrl->dev, "unable to set mode for locked pin %u\n",
				 grp->pins[i]);
			raw_spin_unlock_irqrestore(&pctrl->lock, flags);
			return -EBUSY;
		}
	}
*/

	for (i = 0; i < grp->npins; i++) {
/*
		const struct pinctrl_fake_alternate_function *altfunc = &grp->altfunc;
		int pin = grp->pins[i];
		void __iomem *reg;
		u32 value;

		// Check if there is pin-specific config
		if (grp->overrides) {
			int j;

			for (j = 0; j < grp->noverrides; j++) {
				if (grp->overrides[j].pin == pin) {
					altfunc = &grp->overrides[j];
					break;
				}
			}
		}

		reg = pinctrl_fake_padreg(pctrl, pin, CHV_PADCTRL0);
		value = readl(reg);
		// Disable GPIO mode
		value &= ~CHV_PADCTRL0_GPIOEN;
		// Set to desired mode
		value &= ~CHV_PADCTRL0_PMODE_MASK;
		value |= altfunc->mode << CHV_PADCTRL0_PMODE_SHIFT;
		pinctrl_fake_writel(value, reg);

		// Update for invert_oe
		reg = pinctrl_fake_padreg(pctrl, pin, CHV_PADCTRL1);
		value = readl(reg) & ~CHV_PADCTRL1_INVRXTX_MASK;
		if (altfunc->invert_oe)
			value |= CHV_PADCTRL1_INVRXTX_TXENABLE;
		pinctrl_fake_writel(value, reg);

		dev_dbg(pctrl->dev, "configured pin %u mode %u OE %sinverted\n",
			pin, altfunc->mode, altfunc->invert_oe ? "" : "not ");
*/
	}

//unlock:
	raw_spin_unlock_irqrestore( & pctrl->lock, flags );

	r = EXIT_SUCCESS;

out:
	return r;
}

static int pinctrl_fake_gpio_request_enable(struct pinctrl_dev *pctldev,
				   struct pinctrl_gpio_range *range,
				   unsigned offset)
{
	/*
	struct pinctrl_fake *pctrl = pinctrl_dev_get_drvdata(pctldev);
	unsigned long flags;
	void __iomem *reg;
	u32 value;

	raw_spin_lock_irqsave(&pctrl->lock, flags);

	if (pinctrl_fake_pad_locked(pctrl, offset)) {
		value = readl(pinctrl_fake_padreg(pctrl, offset, CHV_PADCTRL0));
		if (!(value & CHV_PADCTRL0_GPIOEN)) {
			// Locked so cannot enable
			raw_spin_unlock_irqrestore(&pctrl->lock, flags);
			return -EBUSY;
		}
	} else {
		int i;

		// Reset the interrupt mapping
		for (i = 0; i < ARRAY_SIZE(pctrl->intr_lines); i++) {
			if (pctrl->intr_lines[i] == offset) {
				pctrl->intr_lines[i] = 0;
				break;
			}
		}

		// Disable interrupt generation
		reg = pinctrl_fake_padreg(pctrl, offset, CHV_PADCTRL1);
		value = readl(reg);
		value &= ~CHV_PADCTRL1_INTWAKECFG_MASK;
		value &= ~CHV_PADCTRL1_INVRXTX_MASK;
		pinctrl_fake_writel(value, reg);

		reg = pinctrl_fake_padreg(pctrl, offset, CHV_PADCTRL0);
		value = readl(reg);

		// If the pin is in HiZ mode (both TX and RX buffers are
		// disabled) we turn it to be input now.
		if ((value & CHV_PADCTRL0_GPIOCFG_MASK) ==
		     (CHV_PADCTRL0_GPIOCFG_HIZ << CHV_PADCTRL0_GPIOCFG_SHIFT)) {
			value &= ~CHV_PADCTRL0_GPIOCFG_MASK;
			value |= CHV_PADCTRL0_GPIOCFG_GPI <<
				CHV_PADCTRL0_GPIOCFG_SHIFT;
		}

		// Switch to a GPIO mode
		value |= CHV_PADCTRL0_GPIOEN;
		pinctrl_fake_writel(value, reg);
	}

	raw_spin_unlock_irqrestore(&pctrl->lock, flags);
*/
	return 0;
}

static void pinctrl_fake_gpio_disable_free(struct pinctrl_dev *pctldev,
				  struct pinctrl_gpio_range *range,
				  unsigned offset)
{
	/*
	struct pinctrl_fake *pctrl = pinctrl_dev_get_drvdata(pctldev);
	unsigned long flags;
	void __iomem *reg;
	u32 value;

	raw_spin_lock_irqsave(&pctrl->lock, flags);

	reg = pinctrl_fake_padreg(pctrl, offset, CHV_PADCTRL0);
	value = readl(reg) & ~CHV_PADCTRL0_GPIOEN;
	pinctrl_fake_writel(value, reg);

	raw_spin_unlock_irqrestore(&pctrl->lock, flags);
	*/
}

static int pinctrl_fake_gpio_set_direction(struct pinctrl_dev *pctldev,
				  struct pinctrl_gpio_range *range,
				  unsigned offset, bool input)
{
	/*
	struct pinctrl_fake *pctrl = pinctrl_dev_get_drvdata(pctldev);
	void __iomem *reg = pinctrl_fake_padreg(pctrl, offset, CHV_PADCTRL0);
	unsigned long flags;
	u32 ctrl0;

	raw_spin_lock_irqsave(&pctrl->lock, flags);

	ctrl0 = readl(reg) & ~CHV_PADCTRL0_GPIOCFG_MASK;
	if (input)
		ctrl0 |= CHV_PADCTRL0_GPIOCFG_GPI << CHV_PADCTRL0_GPIOCFG_SHIFT;
	else
		ctrl0 |= CHV_PADCTRL0_GPIOCFG_GPO << CHV_PADCTRL0_GPIOCFG_SHIFT;
	pinctrl_fake_writel(ctrl0, reg);

	raw_spin_unlock_irqrestore(&pctrl->lock, flags);
*/
	return 0;
}

static const struct pinmux_ops pinctrl_fake_pinmux_ops = {
	.get_functions_count = pinctrl_fake_get_functions_count,
	.get_function_name = pinctrl_fake_get_function_name,
	.get_function_groups = pinctrl_fake_get_function_groups,
	.set_mux = pinctrl_fake_pinmux_set_mux,
	.gpio_request_enable = pinctrl_fake_gpio_request_enable,
	.gpio_disable_free = pinctrl_fake_gpio_disable_free,
	.gpio_set_direction = pinctrl_fake_gpio_set_direction,
};

static int pinctrl_fake_config_get(struct pinctrl_dev *pctldev, unsigned pin,
			  unsigned long *config)
{
	/*
	struct pinctrl_fake *pctrl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	unsigned long flags;
	u32 ctrl0, ctrl1;
	u16 arg = 0;
	u32 term;

	raw_spin_lock_irqsave(&pctrl->lock, flags);
	ctrl0 = readl(pinctrl_fake_padreg(pctrl, pin, CHV_PADCTRL0));
	ctrl1 = readl(pinctrl_fake_padreg(pctrl, pin, CHV_PADCTRL1));
	raw_spin_unlock_irqrestore(&pctrl->lock, flags);

	term = (ctrl0 & CHV_PADCTRL0_TERM_MASK) >> CHV_PADCTRL0_TERM_SHIFT;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		if (term)
			return -EINVAL;
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		if (!(ctrl0 & CHV_PADCTRL0_TERM_UP))
			return -EINVAL;

		switch (term) {
		case CHV_PADCTRL0_TERM_20K:
			arg = 20000;
			break;
		case CHV_PADCTRL0_TERM_5K:
			arg = 5000;
			break;
		case CHV_PADCTRL0_TERM_1K:
			arg = 1000;
			break;
		}

		break;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (!term || (ctrl0 & CHV_PADCTRL0_TERM_UP))
			return -EINVAL;

		switch (term) {
		case CHV_PADCTRL0_TERM_20K:
			arg = 20000;
			break;
		case CHV_PADCTRL0_TERM_5K:
			arg = 5000;
			break;
		}

		break;

	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		if (!(ctrl1 & CHV_PADCTRL1_ODEN))
			return -EINVAL;
		break;

	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE: {
		u32 cfg;

		cfg = ctrl0 & CHV_PADCTRL0_GPIOCFG_MASK;
		cfg >>= CHV_PADCTRL0_GPIOCFG_SHIFT;
		if (cfg != CHV_PADCTRL0_GPIOCFG_HIZ)
			return -EINVAL;

		break;
	}

	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);
	*/
	return 0;
}

static int pinctrl_fake_config_set_pull(struct pinctrl_fake *pctrl, unsigned pin,
			       enum pin_config_param param, u16 arg )
{
	/*
	void __iomem *reg = pinctrl_fake_padreg(pctrl, pin, CHV_PADCTRL0);
	unsigned long flags;
	u32 ctrl0, pull;

	raw_spin_lock_irqsave(&pctrl->lock, flags);
	ctrl0 = readl(reg);

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		ctrl0 &= ~(CHV_PADCTRL0_TERM_MASK | CHV_PADCTRL0_TERM_UP);
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		ctrl0 &= ~(CHV_PADCTRL0_TERM_MASK | CHV_PADCTRL0_TERM_UP);

		switch (arg) {
		case 1000:
			// For 1k there is only pull up
			pull = CHV_PADCTRL0_TERM_1K << CHV_PADCTRL0_TERM_SHIFT;
			break;
		case 5000:
			pull = CHV_PADCTRL0_TERM_5K << CHV_PADCTRL0_TERM_SHIFT;
			break;
		case 20000:
			pull = CHV_PADCTRL0_TERM_20K << CHV_PADCTRL0_TERM_SHIFT;
			break;
		default:
			raw_spin_unlock_irqrestore(&pctrl->lock, flags);
			return -EINVAL;
		}

		ctrl0 |= CHV_PADCTRL0_TERM_UP | pull;
		break;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		ctrl0 &= ~(CHV_PADCTRL0_TERM_MASK | CHV_PADCTRL0_TERM_UP);

		switch (arg) {
		case 5000:
			pull = CHV_PADCTRL0_TERM_5K << CHV_PADCTRL0_TERM_SHIFT;
			break;
		case 20000:
			pull = CHV_PADCTRL0_TERM_20K << CHV_PADCTRL0_TERM_SHIFT;
			break;
		default:
			raw_spin_unlock_irqrestore(&pctrl->lock, flags);
			return -EINVAL;
		}

		ctrl0 |= pull;
		break;

	default:
		raw_spin_unlock_irqrestore(&pctrl->lock, flags);
		return -EINVAL;
	}

	pinctrl_fake_writel(ctrl0, reg);
	raw_spin_unlock_irqrestore(&pctrl->lock, flags);
*/
	return 0;
}

static int pinctrl_fake_config_set(struct pinctrl_dev *pctldev, unsigned pin,
			  unsigned long *configs, unsigned nconfigs)
{
	/*
	struct pinctrl_fake *pctrl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param;
	int i, ret;
	u16 arg;

	if (pinctrl_fake_pad_locked(pctrl, pin))
		return -EBUSY;

	for (i = 0; i < nconfigs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN:
			ret = pinctrl_fake_config_set_pull(pctrl, pin, param, arg);
			if (ret)
				return ret;
			break;

		default:
			return -ENOTSUPP;
		}

		dev_dbg(pctrl->dev, "pin %d set config %d arg %u\n", pin,
			param, arg);
	}
*/
	return 0;
}

static const struct pinconf_ops pinctrl_fake_pinconf_ops = {
	.is_generic = true,
	.pin_config_set = pinctrl_fake_config_set,
	.pin_config_get = pinctrl_fake_config_get,
};

static unsigned pinctrl_fake_gpio_offset_to_pin(struct pinctrl_fake *pctrl,
				       unsigned offset)
{
//	return pctrl->pins[ offset ].number;
	return 0;
}

static int pinctrl_fake_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct pinctrl_fake *pctrl = gpiochip_get_data(chip);
	//int pin = pinctrl_fake_gpio_offset_to_pin(pctrl, offset);

	dev_info( pctrl->dev, "get()\n" );

	/*
	unsigned long flags;
	u32 ctrl0, cfg;

	raw_spin_lock_irqsave(&pctrl->lock, flags);
	ctrl0 = readl(pinctrl_fake_padreg(pctrl, pin, CHV_PADCTRL0));
	raw_spin_unlock_irqrestore(&pctrl->lock, flags);

	cfg = ctrl0 & CHV_PADCTRL0_GPIOCFG_MASK;
	cfg >>= CHV_PADCTRL0_GPIOCFG_SHIFT;

	if (cfg == CHV_PADCTRL0_GPIOCFG_GPO)
		return !!(ctrl0 & CHV_PADCTRL0_GPIOTXSTATE);
	return !!(ctrl0 & CHV_PADCTRL0_GPIORXSTATE);
	*/
	return 0;
}

static void pinctrl_fake_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct pinctrl_fake *pctrl = gpiochip_get_data(chip);
	//unsigned pin = pinctrl_fake_gpio_offset_to_pin(pctrl, offset);

	dev_info( pctrl->dev, "set()\n" );

	/*
	unsigned long flags;
	void __iomem *reg;
	u32 ctrl0;

	raw_spin_lock_irqsave(&pctrl->lock, flags);

	reg = pinctrl_fake_padreg(pctrl, pin, CHV_PADCTRL0);
	ctrl0 = readl(reg);

	if (value)
		ctrl0 |= CHV_PADCTRL0_GPIOTXSTATE;
	else
		ctrl0 &= ~CHV_PADCTRL0_GPIOTXSTATE;

	pinctrl_fake_writel(ctrl0, reg);

	raw_spin_unlock_irqrestore(&pctrl->lock, flags);
	*/
}

static int pinctrl_fake_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	struct pinctrl_fake *pctrl = gpiochip_get_data(chip);
//	unsigned pin = pinctrl_fake_gpio_offset_to_pin(pctrl, offset);

	dev_info( pctrl->dev, "get_direction()\n" );

	/*
	struct pinctrl_fake *pctrl = gpiochip_get_data(chip);
	unsigned pin = pinctrl_fake_gpio_offset_to_pin(pctrl, offset);
	u32 ctrl0, direction;
	unsigned long flags;

	raw_spin_lock_irqsave(&pctrl->lock, flags);
	ctrl0 = readl(pinctrl_fake_padreg(pctrl, pin, CHV_PADCTRL0));
	raw_spin_unlock_irqrestore(&pctrl->lock, flags);

	direction = ctrl0 & CHV_PADCTRL0_GPIOCFG_MASK;
	direction >>= CHV_PADCTRL0_GPIOCFG_SHIFT;

	return direction != CHV_PADCTRL0_GPIOCFG_GPO;
	*/
	return 0;
}

static int pinctrl_fake_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct pinctrl_fake *pctrl = gpiochip_get_data(chip);

	dev_info( pctrl->dev, "direction_input()\n" );

//	return pinctrl_gpio_direction_input(chip->base + offset);
	return 0;
}

static int pinctrl_fake_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
				     int value)
{
	struct pinctrl_fake *pctrl = gpiochip_get_data(chip);

	dev_info( pctrl->dev, "direction_output()\n" );

//	pinctrl_fake_gpio_set(chip, offset, value);
//	return pinctrl_gpio_direction_output(chip->base + offset);
	return 0;
}

static const struct gpio_chip pinctrl_fake_gpio_chip = {
	.owner = THIS_MODULE,
	.request = gpiochip_generic_request,
	.free = gpiochip_generic_free,
	.get_direction = pinctrl_fake_gpio_get_direction,
	.direction_input = pinctrl_fake_gpio_direction_input,
	.direction_output = pinctrl_fake_gpio_direction_output,
	.get = pinctrl_fake_gpio_get,
	.set = pinctrl_fake_gpio_set,
};

static void pinctrl_fake_gpio_irq_ack(struct irq_data *d)
{
	/*
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pinctrl_fake *pctrl = gpiochip_get_data(gc);
	int pin = pinctrl_fake_gpio_offset_to_pin(pctrl, irqd_to_hwirq(d));
	u32 intr_line;

	raw_spin_lock(&pctrl->lock);

	intr_line = readl(pinctrl_fake_padreg(pctrl, pin, CHV_PADCTRL0));
	intr_line &= CHV_PADCTRL0_INTSEL_MASK;
	intr_line >>= CHV_PADCTRL0_INTSEL_SHIFT;
	pinctrl_fake_writel(BIT(intr_line), pctrl->regs + CHV_INTSTAT);

	raw_spin_unlock(&pctrl->lock);
	*/
}

static void pinctrl_fake_gpio_irq_mask_unmask(struct irq_data *d, bool mask)
{
	/*
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pinctrl_fake *pctrl = gpiochip_get_data(gc);
	int pin = pinctrl_fake_gpio_offset_to_pin(pctrl, irqd_to_hwirq(d));
	u32 value, intr_line;
	unsigned long flags;

	raw_spin_lock_irqsave(&pctrl->lock, flags);

	intr_line = readl(pinctrl_fake_padreg(pctrl, pin, CHV_PADCTRL0));
	intr_line &= CHV_PADCTRL0_INTSEL_MASK;
	intr_line >>= CHV_PADCTRL0_INTSEL_SHIFT;

	value = readl(pctrl->regs + CHV_INTMASK);
	if (mask)
		value &= ~BIT(intr_line);
	else
		value |= BIT(intr_line);
	pinctrl_fake_writel(value, pctrl->regs + CHV_INTMASK);

	raw_spin_unlock_irqrestore(&pctrl->lock, flags);
	*/
}

static void pinctrl_fake_gpio_irq_mask(struct irq_data *d)
{
	pinctrl_fake_gpio_irq_mask_unmask(d, true);
}

static void pinctrl_fake_gpio_irq_unmask(struct irq_data *d)
{
	pinctrl_fake_gpio_irq_mask_unmask(d, false);
}

static unsigned pinctrl_fake_gpio_irq_startup(struct irq_data *d)
{
	/*
	 // Check if the interrupt has been requested with 0 as triggering
	 // type. In that case it is assumed that the current values
	 // programmed to the hardware are used (e.g BIOS configured
	 // defaults).
	 //
	 // In that case ->irq_set_type() will never be called so we need to
	 // read back the values from hardware now, set correct flow handler
	 // and update mappings before the interrupt is being used.
	if (irqd_get_trigger_type(d) == IRQ_TYPE_NONE) {
		struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
		struct pinctrl_fake *pctrl = gpiochip_get_data(gc);
		unsigned offset = irqd_to_hwirq(d);
		int pin = pinctrl_fake_gpio_offset_to_pin(pctrl, offset);
		irq_flow_handler_t handler;
		unsigned long flags;
		u32 intsel, value;

		raw_spin_lock_irqsave(&pctrl->lock, flags);
		intsel = readl(pinctrl_fake_padreg(pctrl, pin, CHV_PADCTRL0));
		intsel &= CHV_PADCTRL0_INTSEL_MASK;
		intsel >>= CHV_PADCTRL0_INTSEL_SHIFT;

		value = readl(pinctrl_fake_padreg(pctrl, pin, CHV_PADCTRL1));
		if (value & CHV_PADCTRL1_INTWAKECFG_LEVEL)
			handler = handle_level_irq;
		else
			handler = handle_edge_irq;

		if (!pctrl->intr_lines[intsel]) {
			irq_set_handler_locked(d, handler);
			pctrl->intr_lines[intsel] = offset;
		}
		raw_spin_unlock_irqrestore(&pctrl->lock, flags);
	}

	pinctrl_fake_gpio_irq_unmask(d);
	*/
	return 0;
}

static int pinctrl_fake_gpio_irq_type(struct irq_data *d, unsigned type)
{
	/*
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pinctrl_fake *pctrl = gpiochip_get_data(gc);
	unsigned offset = irqd_to_hwirq(d);
	int pin = pinctrl_fake_gpio_offset_to_pin(pctrl, offset);
	unsigned long flags;
	u32 value;

	raw_spin_lock_irqsave(&pctrl->lock, flags);

	// Pins which can be used as shared interrupt are configured in
	// BIOS. Driver trusts BIOS configurations and assigns different
	// handler according to the irq type.
	//
	// Driver needs to save the mapping between each pin and
	// its interrupt line.
	// 1. If the pin cfg is locked in BIOS:
	//	Trust BIOS has programmed IntWakeCfg bits correctly,
	//	driver just needs to save the mapping.
	// 2. If the pin cfg is not locked in BIOS:
	//	Driver programs the IntWakeCfg bits and save the mapping.
	if (!pinctrl_fake_pad_locked(pctrl, pin)) {
		void __iomem *reg = pinctrl_fake_padreg(pctrl, pin, CHV_PADCTRL1);

		value = readl(reg);
		value &= ~CHV_PADCTRL1_INTWAKECFG_MASK;
		value &= ~CHV_PADCTRL1_INVRXTX_MASK;

		if (type & IRQ_TYPE_EDGE_BOTH) {
			if ((type & IRQ_TYPE_EDGE_BOTH) == IRQ_TYPE_EDGE_BOTH)
				value |= CHV_PADCTRL1_INTWAKECFG_BOTH;
			else if (type & IRQ_TYPE_EDGE_RISING)
				value |= CHV_PADCTRL1_INTWAKECFG_RISING;
			else if (type & IRQ_TYPE_EDGE_FALLING)
				value |= CHV_PADCTRL1_INTWAKECFG_FALLING;
		} else if (type & IRQ_TYPE_LEVEL_MASK) {
			value |= CHV_PADCTRL1_INTWAKECFG_LEVEL;
			if (type & IRQ_TYPE_LEVEL_LOW)
				value |= CHV_PADCTRL1_INVRXTX_RXDATA;
		}

		pinctrl_fake_writel(value, reg);
	}

	value = readl(pinctrl_fake_padreg(pctrl, pin, CHV_PADCTRL0));
	value &= CHV_PADCTRL0_INTSEL_MASK;
	value >>= CHV_PADCTRL0_INTSEL_SHIFT;

	pctrl->intr_lines[value] = offset;

	if (type & IRQ_TYPE_EDGE_BOTH)
		irq_set_handler_locked(d, handle_edge_irq);
	else if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_handler_locked(d, handle_level_irq);

	raw_spin_unlock_irqrestore(&pctrl->lock, flags);
*/
	return 0;
}

static struct irq_chip pinctrl_fake_gpio_irqchip = {
	.name = "chv-gpio",
	.irq_startup = pinctrl_fake_gpio_irq_startup,
	.irq_ack = pinctrl_fake_gpio_irq_ack,
	.irq_mask = pinctrl_fake_gpio_irq_mask,
	.irq_unmask = pinctrl_fake_gpio_irq_unmask,
	.irq_set_type = pinctrl_fake_gpio_irq_type,
	.flags = IRQCHIP_SKIP_SET_WAKE,
};

static void pinctrl_fake_gpio_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data( desc );
	struct pinctrl_fake *pctrl = gpiochip_get_data( gc );
	struct irq_chip *chip = irq_desc_get_chip( desc );

	//dev_info( pctrl->dev, "irq_handler()\n" );
/*
	unsigned long pending;
	u32 intr_line;

	chained_irq_enter( chip, desc );

	pending = readl( pctrl->regs + CHV_INTSTAT );
	for_each_set_bit( intr_line, &pending, 16 ) {
		unsigned irq, offset;

		offset = pctrl->intr_lines[ intr_line ];
		irq = irq_find_mapping( gc->irqdomain, offset );
		generic_handle_irq( irq );
	}

	chained_irq_exit( chip, desc );
	*/
}

static void pinctrl_fake_gpio_fini( struct pinctrl_fake *pctrl )
{
	struct gpio_chip *chip;
	int i;

	dev_info( pctrl->dev, "pinctrl_fake_gpio_probe()\n" );

	for( i = 0; i < ARRAY_SIZE( pctrl->gpiochip ); i++ ) {

		chip = & pctrl->gpiochip[ i ];

		dev_info( pctrl->dev, "calling gpiochip_remove for chip '%s'\n", chip->label );
		gpiochip_remove( chip );
		memset( chip, 0, sizeof( *chip ) );
	}
}

static int pinctrl_fake_gpio_init( struct pinctrl_fake *pctrl, int irq )
{
	const struct pinctrl_fake_gpio_pinrange *range;
	struct gpio_chip *chip;
	int ret, i;

	dev_info( pctrl->dev, "pinctrl_fake_gpio_probe()\n" );

	for( i = 0; i < ARRAY_SIZE( pctrl->gpiochip ); i++ ) {

		chip = & pctrl->gpiochip[ i ];
		range = & pinctrl_fake_gpio_pinrange[ i ];

		*chip = pinctrl_fake_gpio_chip;
		chip->ngpio = range->npins;
		chip->label = dev_name( pctrl->dev );
		chip->parent = pctrl->dev;
		chip->base = -1;
		dev_info( pctrl->dev, "adding gpiochip with label '%s'\n", chip->label );

		dev_info( pctrl->dev, "calling gpiochip_add_data()\n" );
		ret = gpiochip_add_data(chip, pctrl);
		if ( ret ) {
			dev_err( pctrl->dev, "failed to register %s\n", chip->label );
			return ret;
		}

		ret = gpiochip_add_pingroup_range( chip, pctrl->pctldev, 0, range->name );
		if ( ret ) {
			dev_err( pctrl->dev, "failed to add GPIO pin range %s\n", range->name );
			goto fail;
		}

		dev_info( pctrl->dev, "calling gpiochip_irqchip_add()\n" );
		ret = gpiochip_irqchip_add( chip, &pinctrl_fake_gpio_irqchip, 0,
				handle_simple_irq, IRQ_TYPE_NONE );
		if ( ret ) {
			dev_err( pctrl->dev, "failed to add IRQ chip\n" );
			goto fail;
		}

		dev_info( pctrl->dev, "calling gpiochip_set_chained_irqchip()\n" );

		gpiochip_set_chained_irqchip( chip, &pinctrl_fake_gpio_irqchip, irq,
					     pinctrl_fake_gpio_irq_handler );
	}

	dev_info(pctrl->dev, "gpio probe success!\n");

	ret = EXIT_SUCCESS;
	goto out;

fail:
	pinctrl_fake_gpio_fini( pctrl );

out:
	return ret;
}

static int pinctrl_fake_probe(struct platform_device *pdev)
{
	int r;
	int irq = 0;

	struct pinctrl_fake *pctrl;

	pctrl = & pinctrl_fake;

	dev_info( & pdev->dev, "pinctrl_fake_probe()\n" );

	raw_spin_lock_init( & pctrl->lock );
	pctrl->dev = & pdev->dev;

	pctrl->pctldesc.name = dev_name( & pdev->dev );

	dev_info( & pdev->dev, "calling pinctrl_register()\n" );

	pctrl->pctldev = pinctrl_register( & pctrl->pctldesc, & pdev->dev, pctrl );
	if ( IS_ERR( pctrl->pctldev ) ) {
		r = PTR_ERR( pctrl->pctldev );
		dev_err( & pdev->dev, "failed to register pinctrl driver (%d)\n", r );
		goto out;
	}
	dev_info( & pdev->dev, "calling pinctrl_fake_gpio_probe()\n" );

	r = pinctrl_fake_gpio_init( pctrl, irq );
	if ( EXIT_SUCCESS != r ) {
		dev_err( & pdev->dev, "pinctrl_fake_gpio_probe() failed (%d)\n", r );
		goto unregister_pinctrl;
	}
	r = EXIT_SUCCESS;

	platform_set_drvdata(pdev, pctrl);

	goto out;

unregister_pinctrl:
	pinctrl_unregister( pctrl->pctldev );

out:
	return r;
}

static int pinctrl_fake_remove(struct platform_device *pdev)
{
	struct pinctrl_fake *pctrl = platform_get_drvdata( pdev );

	dev_info( & pdev->dev, "remove()\n" );

	pinctrl_fake_gpio_fini( pctrl );

	dev_info( & pdev->dev, "unregistering pinctrl device\n" );
	pinctrl_unregister( pctrl->pctldev );

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int pinctrl_fake_suspend(struct device *dev)
{
	return 0;
}

static int pinctrl_fake_resume(struct device *dev)
{
	return 0;
}
#endif

static const struct dev_pm_ops pinctrl_fake_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(pinctrl_fake_suspend, pinctrl_fake_resume)
};

static struct platform_driver pinctrl_fake_driver = {
	.probe = pinctrl_fake_probe,
	.remove = pinctrl_fake_remove,
	.driver = {
		.name = "pinctrl-fake",
		.pm = &pinctrl_fake_pm_ops,
	},
};

// the 1 pinctrl_fake device
static struct pinctrl_fake pinctrl_fake = {
	.pctldesc = {
		.pins = pinctrl_fake_pins,
		.npins = ARRAY_SIZE( pinctrl_fake_pins ),
		.pctlops = &pinctrl_fake_ops,
		.pmxops = &pinctrl_fake_pinmux_ops,
		.confops = &pinctrl_fake_pinconf_ops,
		.owner = THIS_MODULE,
	},
};

static void	pinctrl_fake_platform_device_release( struct device *dev ) {
	dev_info( dev, "dev->release()\n" );
}

static struct platform_device pinctrl_fake_platform_device = {
	.name		= "pinctrl-fake",
	.id		= 0,
	.dev		= {
		.platform_data	= & pinctrl_fake,
		.release = pinctrl_fake_platform_device_release,
	},
};

static struct platform_device *pinctrl_fake_platform_devices[] = {
	& pinctrl_fake_platform_device,
};

static int __init pinctrl_fake_init( void )
{
	int r;

	_pr_info( "pinctrl_fake_init()\n" );

	r = platform_add_devices( pinctrl_fake_platform_devices, ARRAY_SIZE( pinctrl_fake_platform_devices ) );
	if ( EXIT_SUCCESS != r ) {
		_pr_err( "platform_add_devices() failed (%d)\n", r );
		goto out;
	}

	r = platform_driver_probe( & pinctrl_fake_driver, pinctrl_fake_probe );
	if ( EXIT_SUCCESS != r ) {
		_pr_err( "platform_driver_probe() failed (%d)\n", r );
		goto out;
	}

	_pr_info( "success!\n" );

out:
	return r;
}
module_init( pinctrl_fake_init );

static void __exit pinctrl_fake_exit( void )
{
	int i;
	struct platform_device *pdev;

	_pr_info( "exit()\n" );

	for( i = 0; i < ARRAY_SIZE( pinctrl_fake_platform_devices ); i++ ) {
		pdev = pinctrl_fake_platform_devices[ i ];
		dev_info( & pdev->dev, "unregistering platform device\n" );
		platform_device_unregister( pdev );
	}

	_pr_info( "unregistering platform driver\n" );
	platform_driver_unregister( & pinctrl_fake_driver );
}
module_exit( pinctrl_fake_exit );

MODULE_AUTHOR( "Christopher Friedt <chrisfriedt@gmail.com>" );
MODULE_DESCRIPTION( "Fake Pinctrl driver" );
MODULE_LICENSE( "GPL v2" );
