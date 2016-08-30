#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/device.h>

#include <linux/list.h>
#include <linux/list_sort.h>

#include <linux/workqueue.h>

#include <linux/jiffies.h>

#include <linux/interrupt.h>

#include "pinctrl-fake.h"

/**
 * @eta          Absolute time (in jiffies) when the GPIO should be toggled.
 *               Also, the key for the sorted event queue.
 * @period       Amount of time (in jiffies) to be reloaded into
 *               @time_left_ms once it reaches zero.
 * @gpio_offset  GPIO to toggle, w.r.t. a specific gpio_chip.
 *
 * @ev_head      To hold position within the event queue.
 * @ex_head      To hold position within the expired queue.
 */
struct pinctrl_fake_gpio_toggler_elem {
	unsigned long eta;
	unsigned long period;

	const u16 gpio_offset;

	struct list_head ev_head;
	struct list_head ex_head;
};

static void pinctrl_fake_gpio_toggler_work_func( struct work_struct *work );

static int pinctrl_fake_gpio_toggler_eta_comparator( void *priv, struct list_head *a, struct list_head *b ) {
	int r;

	struct pinctrl_fake_gpio_toggler_elem *aa;
	struct pinctrl_fake_gpio_toggler_elem *bb;

	(void) priv;

	aa = container_of( a, struct pinctrl_fake_gpio_toggler_elem, ev_head );
	bb = container_of( a, struct pinctrl_fake_gpio_toggler_elem, ev_head );

	if ( false ) {
	} else if ( time_before( aa->eta, bb->eta ) ) {
		r = -1;
	} else if ( time_after( aa->eta, bb->eta ) ) {
		r = +1;
	} else if ( aa->eta == bb->eta ) {
		r = 0;
	}

	return r;
}

static void pinctrl_fake_gpio_toggler_update( struct pinctrl_fake_gpio_chip *fchip ) {

	struct pinctrl_fake *pctrl;
	unsigned long then;

	pctrl = gpiochip_get_data( &fchip->gpiochip );

	cancel_delayed_work( & fchip->toggler_dwork );

	if ( list_empty( & fchip->toggler_head ) ) {
		return;
	}

	list_sort( NULL, & fchip->toggler_head, pinctrl_fake_gpio_toggler_eta_comparator );

	then =
		list_first_entry(
			& fchip->toggler_head,
			struct pinctrl_fake_gpio_toggler_elem,
			ev_head
		)->eta;

	schedule_delayed_work( & fchip->toggler_dwork, max( 0L, then - jiffies ) );
}

static void pinctrl_fake_gpio_toggler_work_func( struct work_struct *work ) {

	struct delayed_work *dwork;
	struct pinctrl_fake *pctrl;
	struct pinctrl_fake_gpio_chip *fchip;
	struct list_head *it;
	struct pinctrl_fake_gpio_toggler_elem *toggler;
	unsigned long now;
	unsigned long delta;
	int irq;

	bool should_trigger_interrupt;

	LIST_HEAD( expired );

	dwork = to_delayed_work( work );

	fchip = container_of( dwork, struct pinctrl_fake_gpio_chip, toggler_dwork );

	pctrl = gpiochip_get_data( &fchip->gpiochip );

	// this function should only ever be called if there is actually something in the tree!!!!
	//BUG_ON( list_empty( & fchip->toggler_head ) );

	now = jiffies;
	list_for_each( it, & fchip->toggler_head ) {

		toggler = container_of( it, struct pinctrl_fake_gpio_toggler_elem, ev_head );

		if ( time_after_eq( now, toggler->eta ) ) {

			dev_dbg( pctrl->dev, "GPIO Toggler: toggler for %s pin %u has expired\n", dev_name( fchip->gpiochip.cdev ), fchip->pins[ toggler->gpio_offset ] );

			list_add( & toggler->ex_head, & expired );
			toggler->eta = now;
			toggler->eta += toggler->period;

			dev_dbg( pctrl->dev, "GPIO Toggler: setting eta to now ( %lu ) + period ( %lu ) = %lu\n", now, toggler->period, toggler->eta );

		} else {

			dev_dbg( pctrl->dev, "GPIO Toggler: toggler for %s pin %u has not expired\n", dev_name( fchip->gpiochip.cdev ), fchip->pins[ toggler->gpio_offset ] );

			delta =
				now >= toggler->eta
				? toggler->eta - now
				: now + ( MAX_JIFFY_OFFSET - toggler->eta ) + 1;
			toggler->eta -= delta;

			dev_dbg( pctrl->dev, "GPIO Toggler: old eta (%lu) - new eta (%lu) = delta (%lu)\n", toggler->eta + delta, toggler->eta, delta );
		}

	}

	// this function should only ever be called once a timer expires!!!
	// BUG_ON( list_empty( & expired ) );

	should_trigger_interrupt = false;

	list_for_each( it, & expired ) {
		toggler = container_of( it, struct pinctrl_fake_gpio_toggler_elem, ex_head );

		fchip->values[ toggler->gpio_offset ] ^= 1;

		dev_dbg( pctrl->dev, "GPIO Toggler: pin %u changed: %u -> %u\n", fchip->pins[ toggler->gpio_offset ], ! fchip->values[ toggler->gpio_offset ], fchip->values[ toggler->gpio_offset ] );

		switch ( fchip->irq_types[ toggler->gpio_offset ] ) {

		case IRQ_TYPE_EDGE_RISING:
			if ( fchip->values[ toggler->gpio_offset ] ) {
				fchip->pended[ toggler->gpio_offset ] = true;
				should_trigger_interrupt = true;
				dev_dbg( pctrl->dev, "GPIO Toggler: triggering EDGE_RISING interrupt\n" );
			}
			break;

		case IRQ_TYPE_EDGE_FALLING:
			if ( ! fchip->values[ toggler->gpio_offset ] ) {
				fchip->pended[ toggler->gpio_offset ] = true;
				should_trigger_interrupt = true;
				dev_dbg( pctrl->dev, "GPIO Toggler: triggering EDGE_FALLING interrupt\n" );
			}
			break;

		case IRQ_TYPE_EDGE_BOTH:
			fchip->pended[ toggler->gpio_offset ] = true;
			should_trigger_interrupt = true;
			dev_dbg( pctrl->dev, "GPIO Toggler: triggering EDGE_BOTH interrupt\n" );
			break;

		case IRQ_TYPE_NONE:
		default:
			dev_dbg( pctrl->dev, "GPIO Toggler: not triggering an interrupt\n" );
			break;
		}
	}

	if ( should_trigger_interrupt ) {
		dev_dbg( pctrl->dev, "GPIO Toggler: trigger interrupt %u for %s pin %u", irq, dev_name( fchip->gpiochip.cdev ), fchip->pins[ toggler->gpio_offset ] );
		tasklet_schedule( & fchip->tasklet );
	}

	pinctrl_fake_gpio_toggler_update( fchip );

	// after updating from scheduled work, we should always reschedule and then have delayed work pending
	// only removing toggler pins should unschedule work
	//BUG_ON( ! delayed_work_pending( & fchip->toggler_dwork ) );
}

static struct pinctrl_fake_gpio_toggler_elem *
	pinctrl_fake_gpio_toggler_search_by_offset( struct list_head *head, u16 gpio_offset ) {

	struct pinctrl_fake_gpio_toggler_elem *r;
	struct list_head *it;

	list_for_each( it, head ) {
		r = container_of( it, struct pinctrl_fake_gpio_toggler_elem, ev_head );
		if ( gpio_offset == r->gpio_offset ) {
			break;
		}
		r = NULL;
	}

	return r;
}

void pinctrl_fake_gpio_toggler_init( struct pinctrl_fake_gpio_chip *fchip ) {

	struct pinctrl_fake *pctrl;
	struct gpio_chip *chip;

	chip = & fchip->gpiochip;
	pctrl = gpiochip_get_data( chip );

	INIT_DELAYED_WORK( & fchip->toggler_dwork, pinctrl_fake_gpio_toggler_work_func );

	dev_info( chip->cdev, "GPIO Toggler started\n" );
}

void pinctrl_fake_gpio_toggler_fini( struct pinctrl_fake_gpio_chip *fchip ) {

	struct pinctrl_fake_gpio_toggler_elem *r;

	struct pinctrl_fake *pctrl;
	struct gpio_chip *chip;

	chip = & fchip->gpiochip;
	pctrl = gpiochip_get_data( &fchip->gpiochip );

	cancel_delayed_work( & fchip->toggler_dwork );

	while( ! list_empty( & fchip->toggler_head )  ) {
		r = list_first_entry(
			& fchip->toggler_head,
			struct pinctrl_fake_gpio_toggler_elem,
			ev_head
		);

		dev_info( chip->cdev, "GPIO Toggler: disabled on pin %u\n", fchip->pins[ r->gpio_offset ] );

		list_del( & r->ev_head );
		kfree( r );
	}

	dev_info( chip->cdev, "GPIO Toggler stopped\n" );
}

bool pinctrl_fake_gpio_toggler_add( struct pinctrl_fake_gpio_chip *fchip, u16 gpio_offset ) {

	bool r;

	struct pinctrl_fake *pctrl;
	struct gpio_chip *chip;
	struct pinctrl_fake_gpio_toggler_elem *elem;

	chip = & fchip->gpiochip;
	pctrl = gpiochip_get_data( &fchip->gpiochip );

	if ( gpio_offset >= fchip->npins ) {
		r = false;
		dev_err( chip->cdev, "GPIO Toggler: invalid gpio_offset %u\n", gpio_offset );
		goto out;
	}

	if ( GPIOF_DIR_IN != fchip->directions[ gpio_offset ] ) {
		r = false;
		dev_err( chip->cdev, "GPIO Toggler: pin %u not an input\n", fchip->pins[ gpio_offset ] );
		goto out;
	}

	if ( NULL != pinctrl_fake_gpio_toggler_search_by_offset( & fchip->toggler_head, gpio_offset ) ) {
		r = false;
		dev_err( chip->cdev, "GPIO Toggler: pin %u already toggling\n", fchip->pins[ gpio_offset ] );
		goto out;
	}

	elem = kzalloc( sizeof( *elem ), GFP_KERNEL );
	if ( NULL == elem ) {
		r = false;
		dev_err( chip->cdev, "GPIO Toggler: no memory to toggle pin %u\n", fchip->pins[ gpio_offset ] );
		goto out;
	}

	elem->period = msecs_to_jiffies( PINCTRL_FAKE_GPIO_TOGGLER_PERIOD_MS_DEFAULT );
	elem->eta = jiffies + elem->period;
	*( (u16 *) & elem->gpio_offset ) = gpio_offset;
	INIT_LIST_HEAD( & elem->ev_head );

	dev_info( chip->cdev, "GPIO Toggler: enabled on pin %u period %u eta %lu\n", fchip->pins[ gpio_offset ], jiffies_to_msecs( elem->period ), elem->eta );

	list_add_tail( & elem->ev_head, & fchip->toggler_head );

	pinctrl_fake_gpio_toggler_update( fchip );

	r = true;

out:
	return r;
}

bool pinctrl_fake_gpio_toggler_remove( struct pinctrl_fake_gpio_chip *fchip, u16 gpio_offset ) {

	bool r;

	struct pinctrl_fake *pctrl;
	struct gpio_chip *chip;
	struct pinctrl_fake_gpio_toggler_elem *elem;

	chip = & fchip->gpiochip;
	pctrl = gpiochip_get_data( &fchip->gpiochip );

	if ( gpio_offset >= fchip->npins ) {
		r = false;
		dev_err( chip->cdev, "GPIO Toggler: invalid gpio_offset %u\n", gpio_offset );
		goto out;
	}

	elem = pinctrl_fake_gpio_toggler_search_by_offset( & fchip->toggler_head, gpio_offset );
	if ( NULL == elem ) {
		r = false;
		dev_err( chip->cdev, "GPIO Toggler: pin %u already removed\n", fchip->pins[ gpio_offset ] );
		goto out;
	}

	list_del( & elem->ev_head );
	kfree( elem );

	dev_info( chip->cdev, "GPIO Toggler: disabled on pin %u\n", fchip->pins[ gpio_offset ] );

	pinctrl_fake_gpio_toggler_update( fchip );

	r = true;

out:
	return r;
}
