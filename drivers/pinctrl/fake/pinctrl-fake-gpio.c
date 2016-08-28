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
	struct gpio_chip *chip;
	struct pinctrl_fake_gpio_chip *fchip;
	struct pinctrl_fake *pctrl;
	unsigned offset;
	int pin;
//	irq_flow_handler_t handler;
//	unsigned long flags;
//	u32 intsel, value;

	chip = irq_data_get_irq_chip_data( d );
	fchip = container_of( chip, struct pinctrl_fake_gpio_chip, gpiochip );
	pctrl = gpiochip_get_data( chip );
	offset = irqd_to_hwirq( d );

	pin = pinctrl_fake_gpio_offset_to_pin( chip, offset );

//	raw_spin_lock_irqsave( &pctrl->lock, flags );

	dev_dbg( pctrl->dev, "irq_ack for '%s' pin %u\n", chip->label, pin );

//	raw_spin_unlock_irqrestore( & pctrl->lock, flags );
}

static void pinctrl_fake_gpio_irq_mask_unmask(struct irq_data *d, bool mask)
{
	struct gpio_chip *chip;
	struct pinctrl_fake_gpio_chip *fchip;
	struct pinctrl_fake *pctrl;
	unsigned offset;
	int pin;
//	irq_flow_handler_t handler;
//	unsigned long flags;
//	u32 intsel, value;

	chip = irq_data_get_irq_chip_data( d );
	fchip = container_of( chip, struct pinctrl_fake_gpio_chip, gpiochip );
	pctrl = gpiochip_get_data( chip );
	offset = irqd_to_hwirq( d );

	pin = pinctrl_fake_gpio_offset_to_pin( chip, offset );

//	raw_spin_lock_irqsave( &pctrl->lock, flags );

	dev_dbg( pctrl->dev, "irq_mask_unmask for '%s' pin %u mask %u\n", chip->label, pin, mask );

//	raw_spin_unlock_irqrestore( & pctrl->lock, flags );
}

static void pinctrl_fake_gpio_irq_mask(struct irq_data *d)
{
	pinctrl_fake_gpio_irq_mask_unmask(d, true);
}

static void pinctrl_fake_gpio_irq_unmask(struct irq_data *d)
{
	pinctrl_fake_gpio_irq_mask_unmask(d, false);
}

static unsigned pinctrl_fake_gpio_irq_startup( struct irq_data *d ) {

	struct gpio_chip *chip;
	struct pinctrl_fake_gpio_chip *fchip;
	struct pinctrl_fake *pctrl;
	unsigned offset;
	int pin;
//	irq_flow_handler_t handler;
//	unsigned long flags;
//	u32 intsel, value;

	chip = irq_data_get_irq_chip_data( d );
	fchip = container_of( chip, struct pinctrl_fake_gpio_chip, gpiochip );
	pctrl = gpiochip_get_data( chip );
	offset = irqd_to_hwirq( d );

	pin = pinctrl_fake_gpio_offset_to_pin( chip, offset );

//	raw_spin_lock_irqsave( &pctrl->lock, flags );

	dev_dbg( pctrl->dev, "irq_startup for '%s' pin %u\n", chip->label, pin );

//	raw_spin_unlock_irqrestore( & pctrl->lock, flags );

	pinctrl_fake_gpio_irq_unmask( d );
	return 0;
}

static int pinctrl_fake_gpio_irq_type( struct irq_data *d, unsigned type )
{

	struct gpio_chip *chip;
	struct pinctrl_fake_gpio_chip *fchip;
	struct pinctrl_fake *pctrl;
	unsigned offset;
	int pin;
//	irq_flow_handler_t handler;
//	unsigned long flags;
//	u32 intsel, value;

	chip = irq_data_get_irq_chip_data( d );
	fchip = container_of( chip, struct pinctrl_fake_gpio_chip, gpiochip );
	pctrl = gpiochip_get_data( chip );
	offset = irqd_to_hwirq( d );

	pin = pinctrl_fake_gpio_offset_to_pin( chip, offset );

//	raw_spin_lock_irqsave( &pctrl->lock, flags );

	fchip->irq_types[ offset ] = type;

	dev_dbg( pctrl->dev, "set irq_type of chip '%s' pin %u to = %u\n", chip->label, pin, type );

//	raw_spin_unlock_irqrestore( & pctrl->lock, flags );

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

void pinctrl_fake_gpio_irq_handler(struct irq_desc *desc)
{
	struct irq_data *data;
	struct gpio_chip *chip;
	struct pinctrl_fake *pctrl;
	struct irq_chip *irq_chip;
	struct pinctrl_fake_gpio_chip *fchip;
	unsigned offset;
	int irq;

	data = irq_desc_get_irq_data( desc );
	offset = irqd_to_hwirq( data );
	chip = irq_data_get_irq_chip_data( data );
	fchip = container_of( chip, struct pinctrl_fake_gpio_chip, gpiochip );
	pctrl = gpiochip_get_data( chip );
	irq_chip = irq_desc_get_chip( desc );
	irq = data->irq;

	chained_irq_enter( irq_chip, desc );

	dev_dbg( pctrl->dev, "irq %u handler (%p) for chip '%s' pin %u\n", irq, desc->handle_irq, chip->label, fchip->pins[ offset ] );

	generic_handle_irq( irq );

	chained_irq_exit( irq_chip, desc );
}


int pinctrl_fake_gpio_chip_init( struct pinctrl_fake *pctrl, struct gpio_chip *chip, u16 ngpio, const char *label ) {

	int r;

	struct pinctrl_fake_gpio_chip *fchip;
	int irq;

	memcpy( chip, & pinctrl_fake_gpio_chip_template, sizeof( *chip ) );
	chip->label = label;
	chip->ngpio = ngpio;

	fchip = container_of( chip, struct pinctrl_fake_gpio_chip, gpiochip );

	INIT_LIST_HEAD( & fchip->toggler_head );

	r = gpiochip_add_data( chip, pctrl );
	if ( EXIT_SUCCESS != r ) {
		dev_err( pctrl->dev, "failed to add pinctrl data to %s\n", label );
		goto out;
	}

	chip->parent = pctrl->dev;

	irq = 0;

	r = gpiochip_add_pingroup_range( chip, pctrl->pctldev, 0, fchip->group );
	if ( EXIT_SUCCESS != r ) {
		dev_err( pctrl->dev, "failed to add pingroup range to %s\n", label );
		goto out;
	}

	dev_dbg( pctrl->dev, "adding irq chip to %s\n", label );
	r = gpiochip_irqchip_add( chip, &pinctrl_fake_gpio_irqchip, irq, handle_simple_irq, IRQ_TYPE_NONE );
	if ( EXIT_SUCCESS != r ) {
		dev_err( pctrl->dev, "failed to add IRQ chip\n" );
		goto out;
	}

	dev_dbg( pctrl->dev, "calling gpiochip_set_chained_irqchip()\n" );
	gpiochip_set_chained_irqchip( chip, &pinctrl_fake_gpio_irqchip, irq, pinctrl_fake_gpio_irq_handler );

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
