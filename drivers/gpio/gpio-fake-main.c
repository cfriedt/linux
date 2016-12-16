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
#endif // EXIT_SUCCESS

static unsigned gpio_fake_offset_to_pin( struct gpio_chip *chip,
				       unsigned offset)
{
	unsigned pin;
	struct gpio_fake_chip *fchip;

	fchip = container_of( chip, struct gpio_fake_chip, gpiochip );

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
	pin = gpio_fake_offset_to_pin( chip, offset );

	if ( fchip->reserved[ offset ] ) {
		r = -EPERM;
		goto out;
	}
	fchip->directions[ offset ] = GPIOF_DIR_OUT;

#ifdef CONFIG_GPIO_FAKE_WORKER
	gpio_fake_worker_remove( fchip, offset );
#endif // CONFIG_GPIO_FAKE_WORKER

	dev_dbg( fchip->gpiochip.gpiodev->mockdev, "direction_output( %u )\n", pin );

	r = EXIT_SUCCESS;

out:
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

int gpio_fake_chip_init( struct pinctrl_fake *pctrl, struct gpio_chip *chip, u16 ngpio, const char *label ) {

	int r;

	struct gpio_fake_chip *fchip;
	int irq;

	memcpy( chip, & gpio_fake_chip_template, sizeof( *chip ) );
	chip->label = label;
	chip->ngpio = ngpio;

	fchip = container_of( chip, struct gpio_fake_chip, gpiochip );

#ifdef CONFIG_CONFIG_GPIO_FAKE_WORKER
	INIT_LIST_HEAD( & fchip->worker_head );
#endif // CONFIG_CONFIG_GPIO_FAKE_WORKER

	r = gpiochip_add_data( chip, pctrl );
	if ( EXIT_SUCCESS != r ) {
		dev_err( fchip->gpiochip.gpiodev->mockdev, "failed to add pinctrl data to %s\n", label );
		goto out;
	}

	chip->parent = pctrl->dev;

	irq = 0;

	r = gpiochip_add_pingroup_range( chip, pctrl->pctldev, 0, fchip->group );
	if ( EXIT_SUCCESS != r ) {
		dev_err( fchip->gpiochip.gpiodev->mockdev, "failed to add pingroup range to %s\n", label );
		goto out;
	}

	dev_dbg( fchip->gpiochip.gpiodev->mockdev, "adding irq chip to %s\n", label );
	r = gpiochip_irqchip_add( chip, &gpio_fake_irqchip, irq, handle_simple_irq, IRQ_TYPE_NONE );
	if ( EXIT_SUCCESS != r ) {
		dev_err( fchip->gpiochip.gpiodev->mockdev, "failed to add IRQ chip\n" );
		goto out;
	}

	dev_dbg( fchip->gpiochip.gpiodev->mockdev, "calling gpiochip_set_chained_irqchip()\n" );
	gpiochip_set_chained_irqchip( chip, &gpio_fake_irqchip, irq, gpio_fake_irq_handler );

	tasklet_init( & fchip->tasklet, gpio_fake_tasklet, (unsigned long) fchip );

	dev_info( pctrl->dev, "added %s (%s)\n", dev_name( chip->gpiodev->mockdev ), chip->label );

#ifdef CONFIG_GPIO_FAKE_WORKER
	gpio_fake_worker_init( fchip );
#endif // CONFIG_GPIO_FAKE_WORKER

	r = EXIT_SUCCESS;

out:
	return r;
}

void gpio_fake_chip_fini( struct gpio_chip *chip ) {
	struct pinctrl_fake *pctrl;

#ifdef CONFIG_GPIO_FAKE_WORKER
	struct gpio_fake_chip *fchip;
	fchip = container_of( chip, struct gpio_fake_chip, gpiochip );
	gpio_fake_worker_fini( fchip );
#endif // CONFIG_GPIO_FAKE_WORKER

	pctrl = gpiochip_get_data( chip );

	dev_info( pctrl->dev, "removed %s (%s)\n", dev_name( chip->gpiodev->mockdev ), chip->label );
}

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

static int gpio_fake_probe(struct platform_device *pdev)
{
	int r;

	struct pinctrl_fake *pctrl;
	struct gpio_fake_chip *fchip;

	struct device *dev;

	struct device_node *np; // child
	u32 pin_number;
	int i;

	dev = &pdev->dev;

	dev_info( dev, "in gpio_fake_probe()\n" );

    if ( NULL == of_match_device( gpio_fake_dt_ids, dev ) ) {
		r = -ENODEV;
		goto out;
    }

	np = pdev->dev.of_node;

//	pctrl = dev_get_drvdata( dev->parent );
	dev_info( dev, "dev: %p, parent: %p, pctrl: %p\n", dev, dev->parent, pctrl );

	r = EXIT_SUCCESS;
	goto out;

out:
	return r;

//	fchip = kzalloc( sizeof( struct gpio_fake ), GFP_KERNEL );
//	if ( NULL == fchip ) {
//		dev_err( dev, "unable to allocate 'struct gpio_fake'\n" );
//		r = -ENOMEM;
//		goto out;
//	}
//
//	pctrl->pctldesc.npins = of_property_count_u32_elems(  np, "pin-numbers" );
//	if ( pctrl->pctldesc.npins < 0 ) {
//		dev_err( dev, "unable to determine length of 'pin-numbers' array\n" );
//		r = pctrl->pctldesc.npins;
//		goto free_pctrl;
//	}
//
//	pctrl->pctldesc.pins = kzalloc( sizeof( struct pinctrl_pin_desc ), GFP_KERNEL );
//	if ( NULL == pctrl->pctldesc.pins ) {
//		dev_err( dev, "unable to allocate array of 'struct pinctrl_pin_desc'\n" );
//		r = -ENOMEM;
//		goto free_pctrl;
//	}
//
//	for( i = 0; i < pctrl->pctldesc.npins; i++ ) {
//		r = of_property_read_u32_index( np, "pin-numbers", i, & pin_number );
//		if ( EXIT_SUCCESS != r ) {
//			dev_err( dev, "unable to read 'pin-numbers' at index %d\n", i );
//			goto free_pins;
//		}
//		*((unsigned *) & pctrl->pctldesc.pins[ i ].number) = pin_number;
//		r = of_property_read_string_index( np, "pin-names", i, (const char **)& pctrl->pctldesc.pins[ i ].name );
//		if ( EXIT_SUCCESS != r ) {
//			dev_err( dev, "unable to read 'pin-numbers' at index %d\n", i );
//			goto free_pins;
//		}
//		*((void **) & pctrl->pctldesc.pins[ i ].drv_data ) = pctrl;
//	}
//
//	//raw_spin_lock_init( & pctrl->lock );
//	pctrl->dev = dev;
//	pctrl->pctldesc.name = dev_name( dev );
//	pctrl->pctldesc.pctlops = &gpio_fake_ops,
//	pctrl->pctldesc.pmxops = &gpio_fake_pinmux_ops,
//	pctrl->pctldesc.confops = &gpio_fake_pinconf_ops,
//	pctrl->pctldesc.owner = THIS_MODULE,
//
//	pctrl->pctldev = pinctrl_register( & pctrl->pctldesc, & pdev->dev, pctrl );
//	if ( IS_ERR( pctrl->pctldev ) ) {
//		r = PTR_ERR( pctrl->pctldev );
//		dev_err( dev, "failed to register pinctrl driver (%d)\n", r );
//		goto out;
//	}
//
//	platform_set_drvdata( pdev, pctrl );
//
//	INIT_LIST_HEAD( & pctrl->head );
//	list_add( & pctrl->head, & gpio_fake_list_head.head );
//	dev_info( dev, "added gpio_fake @ %p\n", pctrl );
//
//	r = EXIT_SUCCESS;
//
//	goto out;
//
//free_pins:
//	kfree( pctrl->pctldesc.pins );
//	pctrl->pctldesc.pins = NULL;
//
//free_pctrl:
//	kfree( pctrl );
//	pctrl = NULL;
//
//out:
//	return r;
}

static int gpio_fake_remove( struct platform_device *pdev )
{
//	struct gpio_fake *pctrl;
	struct device *dev;

//	dev = & pdev->dev;

//	for( ; ! list_empty( & gpio_fake_list_head.head ) ; ) {
//		pctrl = list_first_entry( & gpio_fake_list_head.head, struct gpio_fake, head );
//
//		dev_info( dev, "removing gpio_fake @ %p\n", pctrl );
//
//		list_del( & pctrl->head );
//
//		dev_info( dev, "removed from list\n" );
//
//		pinctrl_unregister( pctrl->pctldev );
//		pctrl->pctldev = NULL;
//		dev_info( dev, "unregistered\n" );
//
//		memset( pctrl->pctldesc.pins, 0, pctrl->pctldesc.npins * sizeof(  struct pinctrl_pin_desc ) );
//		kfree( pctrl->pctldesc.pins );
//		pctrl->pctldesc.pins = NULL;
//		pctrl->pctldesc.npins = 0;
//		dev_info( dev, "freed pins\n" );
//
//		memset( pctrl, 0, sizeof( *pctrl ) );
//		kfree( pctrl );
//	}

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

	pinctrl_fake_hello();

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
	for( ; ! list_empty( & gpio_fake_list_head.head ); ) {

	}

	platform_driver_unregister( & gpio_fake_driver );

	_pr_info( "Unloading..\n" );
}
module_exit( gpio_fake_exit );

MODULE_AUTHOR( "Christopher Friedt <chrisfriedt@gmail.com>" );
MODULE_DESCRIPTION( MODULE_DESC );
MODULE_LICENSE( "GPL v2" );

