/*
 * Fake-pinctrl driver
 *
 * Copyright (C) 2016, Christopher Friedt
 * Author: Christopher Friedt <chrisfriedt@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/platform_device.h>

#include "pinctrl-fake.h"

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

#ifdef CONFIG_PINCTRL_FAKE_GPIO

static const unsigned pinctrl_fake_gpiochip_a_pins[] = { 0, 8, 16, 24, 25, };
static const unsigned pinctrl_fake_gpiochip_b_pins[] = { 38, 46, 54, 56, 57, 58, 59, 60, 61, 62, 63, };

#define FAKE_GPIO_CHIP( n ) \
static u8 pinctrl_fake_ ## n ## _values[ ARRAY_SIZE( pinctrl_fake_ ## n ## _pins ) ]; \
static u8 pinctrl_fake_ ## n ## _directions[ ARRAY_SIZE( pinctrl_fake_ ## n ## _pins ) ]; \
static u8 pinctrl_fake_ ## n ## _irq_types[ ARRAY_SIZE( pinctrl_fake_ ## n ## _pins ) ]; \
static struct pinctrl_fake_gpio_chip pinctrl_fake_ ##n = { \
	.group = #n "_grp",                                    \
	.npins = ARRAY_SIZE( pinctrl_fake_ ## n ## _pins ),    \
	.pins = pinctrl_fake_ ## n ## _pins,                   \
	.values = pinctrl_fake_ ## n ## _values,               \
	.directions = pinctrl_fake_ ## n ## _directions,       \
	.irq_types = pinctrl_fake_ ## n ## _irq_types,         \
};

FAKE_GPIO_CHIP( gpiochip_a );
FAKE_GPIO_CHIP( gpiochip_b );

#endif // CONFIG_PINCTRL_FAKE_GPIO

static const unsigned pinctrl_fake_spi0_0_pins[]     = { 0, 8, 16, 24, };
static const unsigned pinctrl_fake_spi0_1_pins[]     = { 38, 24, 54, 62, };
static const unsigned pinctrl_fake_i2c0_pins[]       = { 24, 25, };
static const unsigned pinctrl_fake_mmc0_1_pins[]     = { 56, 57, };
static const unsigned pinctrl_fake_mmc0_2_pins[]     = { 58, 59, };
static const unsigned pinctrl_fake_mmc0_3_pins[]     = { 60, 61, 62, 63, };

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
#ifdef CONFIG_PINCTRL_FAKE_GPIO
	PIN_GROUP( gpiochip_a ),
	PIN_GROUP( gpiochip_b ),
#endif // CONFIG_PINCTRL_FAKE_GPIO
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

//	struct pinctrl_fake *pctrl = pinctrl_dev_get_drvdata(pctldev);

	const struct pinctrl_fake_pmx_func *func;
	const struct pinctrl_fake_group *grp;
	//unsigned long flags;
	int i;

	BUG_ON( function >= ARRAY_SIZE( pinctrl_fake_pmx_funcs ) );
	BUG_ON( group >= ARRAY_SIZE( pinctrl_fake_groups ) );

	func = & pinctrl_fake_pmx_funcs[ function ];
	grp = & pinctrl_fake_groups[ group ];

//	raw_spin_lock_irqsave( & pctrl->lock, flags );

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
//	raw_spin_unlock_irqrestore( & pctrl->lock, flags );

	r = EXIT_SUCCESS;

//out:
	return r;
}

static const struct pinmux_ops pinctrl_fake_pinmux_ops = {
	.get_functions_count = pinctrl_fake_get_functions_count,
	.get_function_name = pinctrl_fake_get_function_name,
	.get_function_groups = pinctrl_fake_get_function_groups,
	.set_mux = pinctrl_fake_pinmux_set_mux,
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

// the 1 pinctrl_fake device
static struct pinctrl_fake pinctrl_fake = {
#ifdef CONFIG_PINCTRL_FAKE_GPIO
	.fgpiochip = {
		& pinctrl_fake_gpiochip_a,
		& pinctrl_fake_gpiochip_b,
	},
#endif // CONFIG_PINCTRL_FAKE_GPIO
	.pctldesc = {
		.pins = pinctrl_fake_pins,
		.npins = ARRAY_SIZE( pinctrl_fake_pins ),
		.pctlops = &pinctrl_fake_ops,
		.pmxops = &pinctrl_fake_pinmux_ops,
		.confops = &pinctrl_fake_pinconf_ops,
		.owner = THIS_MODULE,
	},
};

#ifdef CONFIG_PINCTRL_FAKE_GPIO

static void pinctrl_fake_gpio_fini( struct pinctrl_fake *pctrl )
{
	struct gpio_chip *chip;
	int i;

	dev_info( pctrl->dev, "pinctrl_fake_gpio_fini()\n" );

	for( i = 0; i < ARRAY_SIZE( pctrl->fgpiochip ); i++ ) {

		chip = & pctrl->fgpiochip[ i ]->gpiochip;

		dev_info( pctrl->dev, "calling gpiochip_remove for chip '%s'\n", chip->label );
		gpiochip_remove( chip );
		memset( chip, 0, sizeof( *chip ) );
	}
}

static int pinctrl_fake_gpio_init( struct pinctrl_fake *pctrl, int irq )
{
	struct pinctrl_fake_gpio_chip *fchip;
	struct gpio_chip *chip;
	int ret, i;

	static const char *label[] = {
		"pinctrl-fake-gpiochip-a",
		"pinctrl-fake-gpiochip-b",
	};

	dev_info( pctrl->dev, "pinctrl_fake_gpio_init()\n" );

	for( i = 0; i < ARRAY_SIZE( pctrl->fgpiochip ); i++ ) {

		fchip = pctrl->fgpiochip[ i ];
		chip = & fchip->gpiochip;

		dev_info( pctrl->dev, "initializing gpio chip %s\n", label[ i ] );

		ret = pinctrl_fake_gpio_chip_init( pctrl, chip, fchip->npins, label[ i ] );
		if ( EXIT_SUCCESS != ret ) {
			dev_err( pctrl->dev, "failed to add gpio chip %s\n", label[ i ] );
			break;
		}
	}

	if ( EXIT_SUCCESS == ret ) {
		dev_info(pctrl->dev, "gpio probe success!\n");
	} else {
		pinctrl_fake_gpio_fini( pctrl );
	}

	return ret;
}
#else
#define pinctrl_fake_gpio_init(...) EXIT_SUCCESS
#define pinctrl_fake_gpio_fini(...)
#endif // CONFIG_PINCTRL_FAKE_GPIO


static int pinctrl_fake_probe(struct platform_device *pdev)
{
	int r;
	int irq = 0;

	struct pinctrl_fake *pctrl;

	pctrl = & pinctrl_fake;

	dev_info( & pdev->dev, "pinctrl_fake_probe()\n" );

	//raw_spin_lock_init( & pctrl->lock );
	pctrl->dev = & pdev->dev;

	pctrl->pctldesc.name = dev_name( & pdev->dev );

	dev_info( & pdev->dev, "calling pinctrl_register()\n" );

	pctrl->pctldev = pinctrl_register( & pctrl->pctldesc, & pdev->dev, pctrl );
	if ( IS_ERR( pctrl->pctldev ) ) {
		r = PTR_ERR( pctrl->pctldev );
		dev_err( & pdev->dev, "failed to register pinctrl driver (%d)\n", r );
		goto out;
	}
	dev_info( & pdev->dev, "calling pinctrl_fake_gpio_init()\n" );

	r = pinctrl_fake_gpio_init( pctrl, irq );
	if ( EXIT_SUCCESS != r ) {
		dev_err( & pdev->dev, "pinctrl_fake_gpio_probe() failed (%d)\n", r );
		goto unregister_pinctrl;
	}

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
