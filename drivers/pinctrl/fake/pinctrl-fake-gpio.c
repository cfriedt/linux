#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/gpio/driver.h>

#include "pinctrl-fake.h"

static unsigned pinctrl_fake_gpio_offset_to_pin( struct gpio_chip *chip,
				       unsigned offset)
{
	unsigned pin;
	struct pinctrl_fake_gpio_chip *fchip;

	fchip = container_of( chip, struct pinctrl_fake_gpio_chip, gpiochip );

	BUG_ON( offset >= fchip->npins );

	pin = fchip->pins[ offset ];

	return pin;
}

static int pinctrl_fake_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct pinctrl_fake *pctrl;
	struct pinctrl_fake_gpio_chip *fchip;
	int pin;
	int value;

	pctrl = gpiochip_get_data(chip);
	fchip = container_of( chip, struct pinctrl_fake_gpio_chip, gpiochip );
	pin = pinctrl_fake_gpio_offset_to_pin( chip, offset );
	value = fchip->values[ offset ];

	dev_dbg( pctrl->dev, "get( %u ) = %u\n", pin, value );

	return value;
}

static void pinctrl_fake_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct pinctrl_fake *pctrl;
	struct pinctrl_fake_gpio_chip *fchip;
	int pin;

	pctrl = gpiochip_get_data(chip);
	fchip = container_of( chip, struct pinctrl_fake_gpio_chip, gpiochip );
	pin = pinctrl_fake_gpio_offset_to_pin( chip, offset );
	fchip->values[ offset ] = value;

	dev_dbg( pctrl->dev, "set( %u ) = %u\n", pin, value );
}

static int pinctrl_fake_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	struct pinctrl_fake *pctrl;
	struct pinctrl_fake_gpio_chip *fchip;
	int pin;
	int direction;

	pctrl = gpiochip_get_data(chip);
	fchip = container_of( chip, struct pinctrl_fake_gpio_chip, gpiochip );
	pin = pinctrl_fake_gpio_offset_to_pin( chip, offset );
	direction = fchip->directions[ offset ];

	dev_dbg( pctrl->dev, "set( %u ) = %u\n", pin, direction );

	return direction;
}

static int pinctrl_fake_gpio_direction_input( struct gpio_chip *chip, unsigned offset )
{
	struct pinctrl_fake *pctrl;
	struct pinctrl_fake_gpio_chip *fchip;
	int pin;

	pctrl = gpiochip_get_data(chip);
	fchip = container_of( chip, struct pinctrl_fake_gpio_chip, gpiochip );
	pin = pinctrl_fake_gpio_offset_to_pin( chip, offset );
	fchip->directions[ offset ] = GPIOF_DIR_IN;

#ifdef CONFIG_PINCTRL_FAKE_GPIO_TOGGLER
	pinctrl_fake_gpio_toggler_add( fchip, offset );
#endif // CONFIG_PINCTRL_FAKE_GPIO_TOGGLER

	dev_dbg( pctrl->dev, "direction_input( %u )\n", pin );

	return EXIT_SUCCESS;
}

static int pinctrl_fake_gpio_direction_output( struct gpio_chip *chip, unsigned offset, int value )
{
	struct pinctrl_fake *pctrl;
	struct pinctrl_fake_gpio_chip *fchip;
	int pin;

	pctrl = gpiochip_get_data(chip);
	fchip = container_of( chip, struct pinctrl_fake_gpio_chip, gpiochip );
	pin = pinctrl_fake_gpio_offset_to_pin( chip, offset );
	fchip->directions[ offset ] = GPIOF_DIR_OUT;

#ifdef CONFIG_PINCTRL_FAKE_GPIO_TOGGLER
	pinctrl_fake_gpio_toggler_remove( fchip, offset );
#endif // CONFIG_PINCTRL_FAKE_GPIO_TOGGLER

	dev_dbg( pctrl->dev, "direction_output( %u )\n", pin );

	return EXIT_SUCCESS;
}

static const struct gpio_chip pinctrl_fake_gpio_chip_template = {
	.owner = THIS_MODULE,
	.label = NULL,
	.request = gpiochip_generic_request,
	.free = gpiochip_generic_free,
	.get_direction = pinctrl_fake_gpio_get_direction,
	.direction_input = pinctrl_fake_gpio_direction_input,
	.direction_output = pinctrl_fake_gpio_direction_output,
	.get = pinctrl_fake_gpio_get,
	.set = pinctrl_fake_gpio_set,
	.base = -1,
	.ngpio = -1,
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
	.name = "pinctrl-fake-gpio",
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

	dev_info( pctrl->dev, "irq_handler()\n" );

	generic_handle_irq( 0 );
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


int pinctrl_fake_gpio_chip_init( struct pinctrl_fake *pctrl, struct gpio_chip *chip, u16 ngpio, const char *label ) {

	int r;

	struct pinctrl_fake_gpio_chip *fchip;

	memcpy( chip, & pinctrl_fake_gpio_chip_template, sizeof( *chip ) );
	chip->label = label;
	chip->ngpio = ngpio;

	fchip = container_of( chip, struct pinctrl_fake_gpio_chip, gpiochip );

	INIT_LIST_HEAD( & fchip->toggler_head );

	dev_info( pctrl->dev, "calling gpiochip_add_data()\n" );
	r = gpiochip_add_data( chip, pctrl );
	if ( EXIT_SUCCESS != r ) {
		dev_err( pctrl->dev, "failed to add pinctrl data to %s\n", label );
		goto out;
	}

	chip->parent = pctrl->dev;

	r = gpiochip_add_pingroup_range( chip, pctrl->pctldev, 0, fchip->group );
	if ( EXIT_SUCCESS != r ) {
		dev_err( pctrl->dev, "failed to add pingroup range to %s\n", label );
		goto out;
	}

	dev_info( pctrl->dev, "adding irq chip to %s\n", label );
	r = gpiochip_irqchip_add( chip, &pinctrl_fake_gpio_irqchip, 0, handle_simple_irq, IRQ_TYPE_NONE );
	if ( EXIT_SUCCESS != r ) {
		dev_err( pctrl->dev, "failed to add IRQ chip\n" );
		goto out;
	}

	dev_info( pctrl->dev, "calling gpiochip_set_chained_irqchip()\n" );
	gpiochip_set_chained_irqchip( chip, &pinctrl_fake_gpio_irqchip, 0, pinctrl_fake_gpio_irq_handler );

#ifdef CONFIG_PINCTRL_FAKE_GPIO_TOGGLER
	pinctrl_fake_gpio_toggler_init( fchip );
#endif // CONFIG_PINCTRL_FAKE_GPIO_TOGGLER

	r = EXIT_SUCCESS;

out:
	return r;
}

void pinctrl_fake_gpio_chip_fini( struct gpio_chip *chip ) {
#ifdef CONFIG_PINCTRL_FAKE_GPIO_TOGGLER
	struct pinctrl_fake_gpio_chip *fchip;
	fchip = container_of( chip, struct pinctrl_fake_gpio_chip, gpiochip );
	pinctrl_fake_gpio_toggler_fini( fchip );
#endif // CONFIG_PINCTRL_FAKE_GPIO_TOGGLER
}
