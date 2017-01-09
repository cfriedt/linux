#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/pinctrl-fake.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include "linux/gpio-fake.h"

#include "gpiolib.h"

#ifndef MODULE_DESC
#define MODULE_DESC "Fake Gpio Driver"
#endif

#ifndef _pr_info
#define _pr_info( fmt, args... ) pr_info( MODULE_DESC ": " fmt, ##args )
#endif

#ifndef _pr_err
#define _pr_err( fmt, args... ) pr_err( MODULE_DESC ": " fmt, ##args )
#endif

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif

#ifndef VPUL
#define VPUL (void *) (unsigned long)
#endif

static unsigned gpio_fake_offset_to_pin( struct gpio_chip *chip,
				       unsigned offset)
{
	unsigned pin;
	struct gpio_fake_chip *fchip;

	fchip = container_of( chip, struct gpio_fake_chip, gpiochip );

	dev_dbg( fchip->gpiochip.gpiodev->mockdev, "In %s()\n", __FUNCTION__ );

	BUG_ON( offset >= fchip->npins );

	pin = fchip->pins[ offset ];

	return pin;
}

static int gpio_fake_get(struct gpio_chip *chip, unsigned offset)
{
	struct pinctrl_fake *pctrl;
	struct gpio_fake_chip *fchip;
	int pin;
	int value;

	pctrl = gpiochip_get_data(chip);
	fchip = container_of( chip, struct gpio_fake_chip, gpiochip );

	dev_dbg( fchip->gpiochip.gpiodev->mockdev, "In %s()\n", __FUNCTION__ );

	pin = gpio_fake_offset_to_pin( chip, offset );
	value = fchip->values[ offset ];

	dev_dbg( fchip->gpiochip.gpiodev->mockdev, "get( %u ) = %u\n", pin, value );

	return value;
}

static void gpio_fake_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct pinctrl_fake *pctrl;
	struct gpio_fake_chip *fchip;
	int pin;

	pctrl = gpiochip_get_data(chip);
	fchip = container_of( chip, struct gpio_fake_chip, gpiochip );

	dev_dbg( fchip->gpiochip.gpiodev->mockdev, "In %s()\n", __FUNCTION__ );

	pin = gpio_fake_offset_to_pin( chip, offset );
	fchip->values[ offset ] = value;

	dev_dbg( fchip->gpiochip.gpiodev->mockdev, "set( %u ) = %u\n", pin, value );
}

static int gpio_fake_get_direction(struct gpio_chip *chip, unsigned offset)
{
	struct pinctrl_fake *pctrl;
	struct gpio_fake_chip *fchip;
	int pin;
	int direction;

	pctrl = gpiochip_get_data(chip);
	fchip = container_of( chip, struct gpio_fake_chip, gpiochip );

	dev_dbg( fchip->gpiochip.gpiodev->mockdev, "In %s()\n", __FUNCTION__ );

	pin = gpio_fake_offset_to_pin( chip, offset );
	direction = fchip->directions[ offset ];

	dev_dbg( fchip->gpiochip.gpiodev->mockdev, "set( %u ) = %u\n", pin, direction );

	return direction;
}

static int gpio_fake_direction_input( struct gpio_chip *chip, unsigned offset )
{
	struct pinctrl_fake *pctrl;
	struct gpio_fake_chip *fchip;
	int pin;

	pctrl = gpiochip_get_data(chip);
	fchip = container_of( chip, struct gpio_fake_chip, gpiochip );

	dev_dbg( fchip->gpiochip.gpiodev->mockdev, "In %s()\n", __FUNCTION__ );

	pin = gpio_fake_offset_to_pin( chip, offset );
	fchip->directions[ offset ] = GPIOF_DIR_IN;

#ifdef CONFIG_GPIO_FAKE_WORKER
	gpio_fake_worker_add( fchip, offset );
#endif // CONFIG_GPIO_FAKE_WORKER

	dev_dbg( fchip->gpiochip.gpiodev->mockdev, "direction_input( %u )\n", pin );

	return EXIT_SUCCESS;
}

static int gpio_fake_direction_output( struct gpio_chip *chip, unsigned offset, int value )
{
	int r;

	struct pinctrl_fake *pctrl;
	struct gpio_fake_chip *fchip;
	int pin;

	pctrl = gpiochip_get_data(chip);
	fchip = container_of( chip, struct gpio_fake_chip, gpiochip );

	dev_dbg( fchip->gpiochip.gpiodev->mockdev, "In %s()\n", __FUNCTION__ );

	pin = gpio_fake_offset_to_pin( chip, offset );

/*
	if ( fchip->reserved[ offset ] ) {
		r = -EPERM;
		goto out;
	}
*/
	fchip->directions[ offset ] = GPIOF_DIR_OUT;

#ifdef CONFIG_GPIO_FAKE_WORKER
	gpio_fake_worker_remove( fchip, offset );
#endif // CONFIG_GPIO_FAKE_WORKER

	dev_dbg( fchip->gpiochip.gpiodev->mockdev, "direction_output( %u )\n", pin );

	r = EXIT_SUCCESS;

	return r;
}

static const struct gpio_chip gpio_fake_chip_template = {
	.owner = THIS_MODULE,
	.label = NULL,
	.request = gpiochip_generic_request,
	.free = gpiochip_generic_free,
	.get_direction = gpio_fake_get_direction,
	.direction_input = gpio_fake_direction_input,
	.direction_output = gpio_fake_direction_output,
	.get = gpio_fake_get,
	.set = gpio_fake_set,
	.base = -1,
	.ngpio = -1,
};

static void gpio_fake_irq_ack(struct irq_data *d)
{
	struct gpio_chip *chip;
	struct gpio_fake_chip *fchip;
	struct pinctrl_fake *pctrl;
	unsigned offset;
	int pin;
//	irq_flow_handler_t handler;
//	unsigned long flags;
//	u32 intsel, value;

	chip = irq_data_get_irq_chip_data( d );
	fchip = container_of( chip, struct gpio_fake_chip, gpiochip );
	pctrl = gpiochip_get_data( chip );
	offset = irqd_to_hwirq( d );

	pin = gpio_fake_offset_to_pin( chip, offset );

//	raw_spin_lock_irqsave( &pctrl->lock, flags );

	dev_dbg( fchip->gpiochip.gpiodev->mockdev, "irq_ack for '%s' pin %u\n", dev_name( chip->gpiodev->mockdev ), pin );

//	raw_spin_unlock_irqrestore( & pctrl->lock, flags );
}

static void gpio_fake_irq_mask_unmask(struct irq_data *d, bool mask)
{
	struct gpio_chip *chip;
	struct gpio_fake_chip *fchip;
	struct pinctrl_fake *pctrl;
	unsigned offset;
	int pin;
//	irq_flow_handler_t handler;
//	unsigned long flags;
//	u32 intsel, value;

	chip = irq_data_get_irq_chip_data( d );
	fchip = container_of( chip, struct gpio_fake_chip, gpiochip );
	pctrl = gpiochip_get_data( chip );
	offset = irqd_to_hwirq( d );

	pin = gpio_fake_offset_to_pin( chip, offset );

//	raw_spin_lock_irqsave( &pctrl->lock, flags );

	dev_dbg( fchip->gpiochip.gpiodev->mockdev, "irq_mask_unmask for '%s' pin %u mask %u\n", dev_name( chip->gpiodev->mockdev ), pin, mask );

//	raw_spin_unlock_irqrestore( & pctrl->lock, flags );
}

static void gpio_fake_irq_mask(struct irq_data *d)
{
	gpio_fake_irq_mask_unmask(d, true);
}

static void gpio_fake_irq_unmask(struct irq_data *d)
{
	gpio_fake_irq_mask_unmask(d, false);
}

static unsigned gpio_fake_irq_startup( struct irq_data *d ) {

	struct gpio_chip *chip;
	struct gpio_fake_chip *fchip;
	struct pinctrl_fake *pctrl;
	unsigned offset;
	int pin;
//	irq_flow_handler_t handler;
//	unsigned long flags;
//	u32 intsel, value;

	chip = irq_data_get_irq_chip_data( d );
	fchip = container_of( chip, struct gpio_fake_chip, gpiochip );

	dev_dbg( fchip->gpiochip.gpiodev->mockdev, "In %s()\n", __FUNCTION__ );

	pctrl = gpiochip_get_data( chip );
	offset = irqd_to_hwirq( d );

	pin = gpio_fake_offset_to_pin( chip, offset );

//	raw_spin_lock_irqsave( &pctrl->lock, flags );

	dev_dbg( fchip->gpiochip.gpiodev->mockdev, "irq_startup for '%s' pin %u\n", dev_name( chip->gpiodev->mockdev ), pin );

//	raw_spin_unlock_irqrestore( & pctrl->lock, flags );

	gpio_fake_irq_unmask( d );
	return 0;
}

static int gpio_fake_irq_type( struct irq_data *d, unsigned type )
{

	struct gpio_chip *chip;
	struct gpio_fake_chip *fchip;
	struct pinctrl_fake *pctrl;
	unsigned offset;
	int pin;
//	irq_flow_handler_t handler;
//	unsigned long flags;
//	u32 intsel, value;

	chip = irq_data_get_irq_chip_data( d );
	fchip = container_of( chip, struct gpio_fake_chip, gpiochip );

	dev_dbg( fchip->gpiochip.gpiodev->mockdev, "In %s()\n", __FUNCTION__ );

	pctrl = gpiochip_get_data( chip );
	offset = irqd_to_hwirq( d );

	pin = gpio_fake_offset_to_pin( chip, offset );

//	raw_spin_lock_irqsave( &pctrl->lock, flags );

	fchip->irq_types[ offset ] = type;

	dev_dbg( fchip->gpiochip.gpiodev->mockdev, "set irq_type of chip '%s' pin %u to = %u\n", dev_name( chip->gpiodev->mockdev ), pin, type );

//	raw_spin_unlock_irqrestore( & pctrl->lock, flags );

	return 0;
}

static struct irq_chip gpio_fake_irqchip = {
	.name = "pinctrl-fake-gpio",
	.irq_startup = gpio_fake_irq_startup,
	.irq_ack = gpio_fake_irq_ack,
	.irq_mask = gpio_fake_irq_mask,
	.irq_unmask = gpio_fake_irq_unmask,
	.irq_set_type = gpio_fake_irq_type,
	.flags = IRQCHIP_SKIP_SET_WAKE,
};

void gpio_fake_irq_handler(struct irq_desc *desc)
{
	struct irq_data *data;
	struct gpio_chip *chip;
	struct pinctrl_fake *pctrl;
	struct irq_chip *irq_chip;
	struct gpio_fake_chip *fchip;
	unsigned offset;
	int irq;

	data = irq_desc_get_irq_data( desc );
	offset = irqd_to_hwirq( data );
	chip = irq_data_get_irq_chip_data( data );
	fchip = container_of( chip, struct gpio_fake_chip, gpiochip );
	pctrl = gpiochip_get_data( chip );
	irq_chip = irq_desc_get_chip( desc );
	irq = data->irq;

	chained_irq_enter( irq_chip, desc );

	dev_dbg( fchip->gpiochip.gpiodev->mockdev, "irq %u handler (%p) for chip '%s' pin %u\n", irq, desc->handle_irq, dev_name( chip->gpiodev->mockdev ), fchip->pins[ offset ] );

	generic_handle_irq( irq );

	chained_irq_exit( irq_chip, desc );
}

#ifdef CONFIG_GPIO_FAKE_WORKER
static void gpio_fake_tasklet( unsigned long data ) {
	struct gpio_fake_chip *fchip;
	struct irq_desc *desc;
	int irq;
	int i;

	fchip = (struct gpio_fake_chip *) data;

	local_irq_disable();
	for( i = 0; i < fchip->npins; i++ ) {
		if ( fchip->pended[ i ] ) {

			irq = fchip->gpiochip.to_irq( & fchip->gpiochip, i );
			desc = irq_to_desc( irq );
			fchip->pended[ i ] = false;

			chained_irq_enter( fchip->gpiochip.irqchip, desc );
			generic_handle_irq( irq );
			chained_irq_exit( fchip->gpiochip.irqchip, desc );
		}
	}
	local_irq_enable();
}
#endif // CONFIG_GPIO_FAKE_WORKER

int gpio_fake_chip_init( struct pinctrl_fake *pctrl, struct gpio_fake_chip *fchip ) {

	int r;

	struct gpio_chip *chip;
	struct device *dev;
	const char *label;
	int irq;

	fchip->pctrl = pctrl;
	chip = & fchip->gpiochip;
	dev = pctrl->dev;

	dev_dbg( dev, "In %s()\n", __FUNCTION__ );

#ifdef CONFIG_GPIO_FAKE_WORKER
	INIT_LIST_HEAD( & fchip->worker_head );
#endif // CONFIG_GPIO_FAKE_WORKER

	r = gpiochip_add_data( chip, pctrl );
	if ( EXIT_SUCCESS != r ) {
		dev_err( dev, "failed to add pinctrl data to %s\n", label );
		goto out;
	}
	dev = fchip->gpiochip.gpiodev->mockdev;

	chip->parent = pctrl->dev;

	irq = 0;

	r = gpiochip_add_pingroup_range( chip, pctrl->pctldev, 0, fchip->group );
	if ( EXIT_SUCCESS != r ) {
		dev_err( dev, "failed to add pingroup range to %s\n", label );
		goto out;
	}

	dev_dbg( dev, "adding irq chip to %s\n", label );
	r = gpiochip_irqchip_add( chip, &gpio_fake_irqchip, irq, handle_simple_irq, IRQ_TYPE_NONE );
	if ( EXIT_SUCCESS != r ) {
		dev_err( dev, "failed to add IRQ chip\n" );
		goto out;
	}

	dev_dbg( dev, "calling gpiochip_set_chained_irqchip()\n" );
	gpiochip_set_chained_irqchip( chip, &gpio_fake_irqchip, irq, gpio_fake_irq_handler );

	tasklet_init( & fchip->tasklet, gpio_fake_tasklet, (unsigned long) fchip );

	dev_info( dev, "added %s (%s)\n", dev_name( chip->gpiodev->mockdev ), chip->label );

#ifdef CONFIG_GPIO_FAKE_WORKER
	gpio_fake_worker_init( fchip );
#endif // CONFIG_GPIO_FAKE_WORKER

	r = EXIT_SUCCESS;

out:
	return r;
}

void gpio_fake_chip_fini( struct gpio_fake_chip *fchip ) {
	struct pinctrl_fake *pctrl;
	struct gpio_chip *chip;

	chip = & fchip->gpiochip;

#ifdef CONFIG_GPIO_FAKE_WORKER
	gpio_fake_worker_fini( fchip );
#endif // CONFIG_GPIO_FAKE_WORKER

	pctrl = gpiochip_get_data( chip );

	dev_info( pctrl->dev, "removed %s (%s)\n", dev_name( chip->gpiodev->mockdev ), chip->label );

	gpiochip_remove( chip );
}


static int gpio_fake_of_parse( struct gpio_fake_chip *fchip ) {

	static const char propname[] = "gpio-ranges";
	static const char group_names_propname[] = "gpio-ranges-group-names";

	int r;

	struct gpio_chip *chip;
	struct device_node *np; // child (gpio controller)
	struct device_node *pnp; // parent (pin controller)
	struct of_phandle_args pinspec;

	int i;
	int j;
	int k;
	const char *name;
	struct property *group_names;
	char pinctrl_fake_pin_group_name_buf[ 64 ];
	u32 value;

	chip = & fchip->gpiochip;
	np = chip->of_node;
	group_names = of_find_property( np, group_names_propname, NULL );

	fchip->npins = 0;

	for( i = 0, j = 0; ; i++ ) {
		if ( i > 0 ) {
			_pr_err( "%s: Currently, only 1 %s may be specified for gpio-fake DT bindings\n", np->full_name, propname );
			break;
		}

		r = of_parse_phandle_with_fixed_args( np, propname, 3, i, &pinspec );
		if ( IS_ERR( VPUL r ) ) {
			_pr_err( "%s: Unable to find property '%s'\n", np->full_name, propname );
			r = -EINVAL;
			goto out;
		}
		pnp = pinspec.np;

		if ( 0 != pinspec.args[ 2 ] ) {
			_pr_err( "%s: Numeric GPIO ranges unsupported for gpio-fake. Please use 'gpio-ranges-group-names' instead.\n", np->full_name );
			r = -EINVAL;
			goto out;
		}

		// npins == 0: special range

		if ( 0 != pinspec.args[ 1 ] ) {
			_pr_err( "%s: Illegal gpio-range format.\n", np->full_name );
			r = -EINVAL;
			goto out;
		}

		if ( NULL == group_names ) {
			_pr_err( "%s: GPIO group range requested but no %s property.\n", np->full_name, group_names_propname );
			r = -EINVAL;
			goto out;
		}

		r = of_property_read_string_index( np, group_names_propname, i, &name );
		if ( IS_ERR( VPUL r ) ) {
			goto out;
		}

		if ( 0 == strlen( name ) ) {
			_pr_err( "%s: Group name of GPIO group range cannot be the empty string.\n", np->full_name );
			r = -EINVAL;
			goto out;
		}

		snprintf( pinctrl_fake_pin_group_name_buf, sizeof( pinctrl_fake_pin_group_name_buf ) - 1, "pinctrl-fake-pin-group-%s", name );

		r = of_property_count_u32_elems( pnp, pinctrl_fake_pin_group_name_buf );
		if ( IS_ERR_OR_NULL( VPUL r ) ) {
			_pr_err( "%s: Cannot find property '%s' (%d).\n", pnp->full_name, pinctrl_fake_pin_group_name_buf, r );
			r = EXIT_SUCCESS == r ? -EINVAL : r;
			goto out;
		}
		fchip->npins = r;
		fchip->gpiochip.ngpio = r;

		fchip->pins = kzalloc( fchip->npins * sizeof( u16 ), GFP_KERNEL );
		fchip->values = kzalloc( fchip->npins * sizeof( u8 ), GFP_KERNEL );
		fchip->directions = kzalloc( fchip->npins * sizeof( u8 ), GFP_KERNEL );
		fchip->irq_types = kzalloc( fchip->npins * sizeof( u8 ), GFP_KERNEL );
		fchip->pended = kzalloc( fchip->npins * sizeof( u8 ), GFP_KERNEL );

		fchip->group = kstrdup( name, GFP_KERNEL );

		if (
			false
			|| NULL == fchip->pins
			|| NULL == fchip->values
			|| NULL == fchip->directions
			|| NULL == fchip->irq_types
			|| NULL == fchip->pended
			|| NULL == fchip->group
		) {
			r = -ENOMEM;
			goto out;
		}

		for( k = 0; k < r; k++, j++ ) {

			r = of_property_read_u32_index( pnp, pinctrl_fake_pin_group_name_buf, k, &value );
			if ( IS_ERR( VPUL r ) ) {
				_pr_err( "%s: Cannot read %uth element of property '%s' (%d).\n", pnp->full_name, k, pinctrl_fake_pin_group_name_buf, r );
				goto out;
			}

			*( (u16 *)( & fchip->pins[ j ] ) ) = (u16) value;
			fchip->values[ j ] = 0;
			fchip->directions[ j ] = GPIOF_DIR_IN;
			fchip->irq_types[ j ] = IRQ_TYPE_NONE;
			fchip->pended[ j ] = false;
		}
	}

	r = EXIT_SUCCESS;

out:
	return r;
}

/*
 * Device Probing
 */

static struct of_device_id gpio_fake_dt_ids[] = {
	{
		.compatible = "gpio-fake",
	},
	{
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, gpio_fake_dt_ids);

static struct gpio_fake_chip gpio_fake_list_head;

static void gpio_fake_free( struct gpio_fake_chip *fchip );

static struct gpio_fake_chip *gpio_fake_allocate_from_dt( struct device *dev, struct device_node *np ) {

	struct gpio_fake_chip *r;

	int rr;
	struct pinctrl_fake *pctrl;
	struct device_node *pnp; // parent, np is child
	struct gpio_chip *chip;

    if ( ! of_match_device( gpio_fake_dt_ids, dev ) ) {
		rr = -ENODEV;
		goto out;
    }

	dev_dbg( dev, "In %s()\n", __FUNCTION__ );

    dev_dbg( dev, "checking parent device\n" );

	if (
		! (
			true
			&& dev->parent
			&& ( pctrl = dev_get_drvdata( dev->parent ) )
			&& pinctrl_fake_valid_instance( pctrl )
		)
	) {
		dev_err( dev, "No valid pinctrl-fake instance @ %p\n", pctrl );
		rr = -ENODEV;
		goto out;
	}

	dev_dbg( dev, "checking node pointer / parent node pointer\n" );

	pnp = dev->parent->of_node;

	dev_dbg( dev, "dev: %p, parent: %p, pctrl: %p\n", dev, dev->parent, pctrl );

	//ngpio = of_property_count_u32_elems( pnp,  );

	dev_dbg( dev, "allocating fchip\n" );

	r = kzalloc( sizeof( struct gpio_fake_chip ), GFP_KERNEL );
	if ( NULL == r ) {
		dev_err( dev, "unable to allocate 'struct gpio_fake'\n" );
		rr = -ENOMEM;
		goto out;
	}
	chip = & r->gpiochip;

	dev_dbg( dev, "copying template\n" );

	memcpy( chip, & gpio_fake_chip_template, sizeof( *chip ) );
	chip->of_node = np;
	chip->label = np->full_name;

	dev_dbg( dev, "parsing..\n" );

	rr = gpio_fake_of_parse( r );
	if ( IS_ERR( VPUL rr ) ) {
		dev_err( dev, "failed to parse devicetree\n" );
		goto out;
	}

	rr = EXIT_SUCCESS;

out:
	if ( EXIT_SUCCESS != rr ) {
		gpio_fake_free( r );
		r = ERR_PTR( rr );
	}
	return r;
}

static void gpio_fake_free( struct gpio_fake_chip *fchip ) {
	if ( NULL != fchip ) {
		kfree( fchip->group );
		kfree( fchip->pins );
		kfree( fchip->values );
		kfree( fchip->directions );
		kfree( fchip->irq_types );
		kfree( fchip->pended );
		kfree( fchip );
	}
}

static int gpio_fake_probe(struct platform_device *pdev)
{
	int r;

	struct pinctrl_fake *pctrl;
	struct gpio_fake_chip *fchip;

	struct device *dev;

	dev = &pdev->dev;

	dev_dbg( dev, "In %s()\n", __FUNCTION__ );

	fchip = gpio_fake_allocate_from_dt( dev, dev->of_node );
	if ( IS_ERR_OR_NULL( fchip ) ) {
		r = PTR_ERR( fchip );
		r = EXIT_SUCCESS == r ? -ENOMEM : r;
		goto out;
	}

	dev_dbg( dev, "adding to list..\n" );

	INIT_LIST_HEAD( & fchip->head );
	list_add( & fchip->head, & gpio_fake_list_head.head );

	pctrl = dev_get_drvdata( dev->parent );

	dev_dbg( dev, "initializing..\n" );
	r = gpio_fake_chip_init( pctrl, fchip );
	if ( IS_ERR( VPUL r ) ) {
		goto out;
	}

	dev_info( dev, "Added gpio-fake @ %p, pdev @ %p, dev @ %p\n", fchip, pdev, dev );

out:
	return r;
}

static int gpio_fake_remove( struct platform_device *pdev )
{
	struct gpio_fake_chip *fchip;
	struct device *dev;

	dev = & pdev->dev;

	for( ; ! list_empty( & gpio_fake_list_head.head ) ; ) {
		fchip = list_first_entry( & gpio_fake_list_head.head, struct gpio_fake_chip, head );

		dev_info( dev, "removing gpio_fake_chip @ %p\n", fchip );

		list_del( & fchip->head );

		dev_info( dev, "removed from list\n" );

		gpio_fake_chip_fini( fchip );

		gpio_fake_free( fchip );
	}

	return EXIT_SUCCESS;
}

#ifdef CONFIG_PM_SLEEP
static int gpio_fake_suspend(struct device *dev)
{
	return 0;
}

static int gpio_fake_resume(struct device *dev)
{
	return 0;
}
#endif

static const struct dev_pm_ops gpio_fake_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(gpio_fake_suspend, gpio_fake_resume)
};

static struct platform_driver gpio_fake_driver = {
	.probe = gpio_fake_probe,
	.remove = gpio_fake_remove,
	.driver = {
		.name = "gpio-fake",
		.owner = THIS_MODULE,
		.pm = &gpio_fake_pm_ops,
		.of_match_table = gpio_fake_dt_ids,
	},
};

static int __init gpio_fake_init( void )
{
	int r;

	INIT_LIST_HEAD( & gpio_fake_list_head.head );

	_pr_info( "Copyright (c) 2016, Christopher Friedt\n" );

	r = platform_driver_register( & gpio_fake_driver );
	if ( EXIT_SUCCESS != r ) {
		_pr_err( "platform_driver_register() failed (%d)\n", r );
		goto out;
	}

out:
	return r;
}
module_init( gpio_fake_init );

static void __exit gpio_fake_exit( void )
{
	struct gpio_fake_chip *fchip;
	struct platform_device *pdev;

	for( ; ! list_empty( & gpio_fake_list_head.head ); ) {
		fchip = list_first_entry( & gpio_fake_list_head.head, struct gpio_fake_chip, head );

		pdev = container_of( & fchip->gpiochip.gpiodev->dev, struct platform_device, dev );

		gpio_fake_remove( pdev );
	}

	platform_driver_unregister( & gpio_fake_driver );

	_pr_info( "Unloading..\n" );
}
module_exit( gpio_fake_exit );

MODULE_AUTHOR( "Christopher Friedt <chrisfriedt@gmail.com>" );
MODULE_DESCRIPTION( MODULE_DESC );
MODULE_LICENSE( "GPL v2" );

