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

#include "linux/pinctrl-fake.h"

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
struct gpio_fake_worker_elem {
	unsigned long eta;
	unsigned long period;

	const u16 gpio_offset;

	struct list_head ev_head;
	struct list_head ex_head;
};

static void gpio_fake_worker_work_func( struct work_struct *work );

static int gpio_fake_worker_eta_comparator( void *priv, struct list_head *a, struct list_head *b ) {
	int r;

	struct gpio_fake_worker_elem *aa;
	struct gpio_fake_worker_elem *bb;

	(void) priv;

	aa = container_of( a, struct gpio_fake_worker_elem, ev_head );
	bb = container_of( a, struct gpio_fake_worker_elem, ev_head );

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

static void gpio_fake_worker_update( struct gpio_fake_chip *fchip ) {

	struct pinctrl_fake *pctrl;
	unsigned long then;

	pctrl = gpiochip_get_data( &fchip->gpiochip );

	cancel_delayed_work( & fchip->worker_dwork );

	if ( list_empty( & fchip->worker_head ) ) {
		return;
	}

	list_sort( NULL, & fchip->worker_head, gpio_fake_worker_eta_comparator );

	then =
		list_first_entry(
			& fchip->worker_head,
			struct gpio_fake_worker_elem,
			ev_head
		)->eta;

	schedule_delayed_work( & fchip->worker_dwork, max( 0L, then - jiffies ) );
}

static void gpio_fake_worker_work_func( struct work_struct *work ) {

	struct delayed_work *dwork;
	struct pinctrl_fake *pctrl;
	struct gpio_fake_chip *fchip;
	struct list_head *it;
	struct gpio_fake_worker_elem *worker;
	unsigned long now;
	unsigned long delta;
	int irq;

	bool should_trigger_interrupt;

	LIST_HEAD( expired );

	dwork = to_delayed_work( work );

	fchip = container_of( dwork, struct gpio_fake_chip, worker_dwork );

	pctrl = gpiochip_get_data( &fchip->gpiochip );

	// this function should only ever be called if there is actually something in the tree!!!!
	//BUG_ON( list_empty( & fchip->worker_head ) );

	now = jiffies;
	list_for_each( it, & fchip->worker_head ) {

		worker = container_of( it, struct gpio_fake_worker_elem, ev_head );

		if ( time_after_eq( now, worker->eta ) ) {

//			dev_dbg( pctrl->dev, "GPIO Worker: worker for %s pin %u has expired\n", dev_name( fchip->gpiochip.cdev ), fchip->pins[ worker->gpio_offset ] );

			list_add( & worker->ex_head, & expired );
			worker->eta = now;
			worker->eta += worker->period;

			dev_dbg( pctrl->dev, "GPIO Worker: setting eta to now ( %lu ) + period ( %lu ) = %lu\n", now, worker->period, worker->eta );

		} else {

//			dev_dbg( pctrl->dev, "GPIO Worker: worker for %s pin %u has not expired\n", dev_name( fchip->gpiochip.cdev ), fchip->pins[ worker->gpio_offset ] );

			delta =
				now >= worker->eta
				? worker->eta - now
				: now + ( MAX_JIFFY_OFFSET - worker->eta ) + 1;
			worker->eta -= delta;

			dev_dbg( pctrl->dev, "GPIO Worker: old eta (%lu) - new eta (%lu) = delta (%lu)\n", worker->eta + delta, worker->eta, delta );
		}

	}

	// this function should only ever be called once a timer expires!!!
	// BUG_ON( list_empty( & expired ) );

	should_trigger_interrupt = false;

	list_for_each( it, & expired ) {
		worker = container_of( it, struct gpio_fake_worker_elem, ex_head );

		if ( fchip->reserved[ worker->gpio_offset ] ) {
			dev_info( pctrl->dev, "GPIO Worker: pin %u unchanged due to reservation\n", fchip->pins[ worker->gpio_offset ] );
			continue;
		}

		fchip->values[ worker->gpio_offset ] ^= 1;

		dev_dbg( pctrl->dev, "GPIO Worker: pin %u changed: %u -> %u\n", fchip->pins[ worker->gpio_offset ], ! fchip->values[ worker->gpio_offset ], fchip->values[ worker->gpio_offset ] );

		switch ( fchip->irq_types[ worker->gpio_offset ] ) {

		case IRQ_TYPE_EDGE_RISING:
			if ( fchip->values[ worker->gpio_offset ] ) {
				fchip->pended[ worker->gpio_offset ] = true;
				should_trigger_interrupt = true;
				dev_dbg( pctrl->dev, "GPIO Worker: triggering EDGE_RISING interrupt\n" );
			}
			break;

		case IRQ_TYPE_EDGE_FALLING:
			if ( ! fchip->values[ worker->gpio_offset ] ) {
				fchip->pended[ worker->gpio_offset ] = true;
				should_trigger_interrupt = true;
				dev_dbg( pctrl->dev, "GPIO Worker: triggering EDGE_FALLING interrupt\n" );
			}
			break;

		case IRQ_TYPE_EDGE_BOTH:
			fchip->pended[ worker->gpio_offset ] = true;
			should_trigger_interrupt = true;
			dev_dbg( pctrl->dev, "GPIO Worker: triggering EDGE_BOTH interrupt\n" );
			break;

		case IRQ_TYPE_NONE:
		default:
			dev_dbg( pctrl->dev, "GPIO Worker: not triggering an interrupt\n" );
			break;
		}
	}

	if ( should_trigger_interrupt ) {
//		dev_dbg( pctrl->dev, "GPIO Worker: trigger interrupt %u for %s pin %u", irq, dev_name( fchip->gpiochip.cdev ), fchip->pins[ worker->gpio_offset ] );
		tasklet_schedule( & fchip->tasklet );
	}

	gpio_fake_worker_update( fchip );

	// after updating from scheduled work, we should always reschedule and then have delayed work pending
	// only removing worker pins should unschedule work
	//BUG_ON( ! delayed_work_pending( & fchip->worker_dwork ) );
}

static struct gpio_fake_worker_elem *
	gpio_fake_worker_search_by_offset( struct list_head *head, u16 gpio_offset ) {

	struct gpio_fake_worker_elem *r;
	struct list_head *it;

	list_for_each( it, head ) {
		r = container_of( it, struct gpio_fake_worker_elem, ev_head );
		if ( gpio_offset == r->gpio_offset ) {
			break;
		}
		r = NULL;
	}

	return r;
}

void gpio_fake_worker_init( struct gpio_fake_chip *fchip ) {

	struct pinctrl_fake *pctrl;
	struct gpio_chip *chip;

	chip = & fchip->gpiochip;
	pctrl = gpiochip_get_data( chip );

	INIT_DELAYED_WORK( & fchip->worker_dwork, gpio_fake_worker_work_func );

//	dev_info( chip->cdev, "GPIO Worker started\n" );
}

void gpio_fake_worker_fini( struct gpio_fake_chip *fchip ) {

	struct gpio_fake_worker_elem *r;

	struct pinctrl_fake *pctrl;
	struct gpio_chip *chip;

	chip = & fchip->gpiochip;
	pctrl = gpiochip_get_data( &fchip->gpiochip );

	cancel_delayed_work( & fchip->worker_dwork );

	while( ! list_empty( & fchip->worker_head )  ) {
		r = list_first_entry(
			& fchip->worker_head,
			struct gpio_fake_worker_elem,
			ev_head
		);

//		dev_info( chip->cdev, "GPIO Worker: disabled on pin %u\n", fchip->pins[ r->gpio_offset ] );

		list_del( & r->ev_head );
		kfree( r );
	}

//	dev_info( chip->cdev, "GPIO Worker stopped\n" );
}

bool gpio_fake_worker_add( struct gpio_fake_chip *fchip, u16 gpio_offset ) {

	bool r;

	struct pinctrl_fake *pctrl;
	struct gpio_chip *chip;
	struct gpio_fake_worker_elem *elem;

	chip = & fchip->gpiochip;
	pctrl = gpiochip_get_data( &fchip->gpiochip );

	if ( gpio_offset >= fchip->npins ) {
		r = false;
//		dev_err( chip->cdev, "GPIO Worker: invalid gpio_offset %u\n", gpio_offset );
		goto out;
	}

	if ( GPIOF_DIR_IN != fchip->directions[ gpio_offset ] ) {
		r = false;
//		dev_err( chip->cdev, "GPIO Worker: pin %u not an input\n", fchip->pins[ gpio_offset ] );
		goto out;
	}

	if ( NULL != gpio_fake_worker_search_by_offset( & fchip->worker_head, gpio_offset ) ) {
		r = false;
//		dev_err( chip->cdev, "GPIO Worker: pin %u already toggling\n", fchip->pins[ gpio_offset ] );
		goto out;
	}

	elem = kzalloc( sizeof( *elem ), GFP_KERNEL );
	if ( NULL == elem ) {
		r = false;
//		dev_err( chip->cdev, "GPIO Worker: no memory to toggle pin %u\n", fchip->pins[ gpio_offset ] );
		goto out;
	}

	elem->period = msecs_to_jiffies( CONFIG_GPIO_FAKE_WORKER_PERIOD_MS_DEFAULT );
	elem->eta = jiffies + elem->period;
	*( (u16 *) & elem->gpio_offset ) = gpio_offset;
	INIT_LIST_HEAD( & elem->ev_head );

//	dev_info( chip->cdev, "GPIO Worker: enabled on pin %u period %u eta %lu\n", fchip->pins[ gpio_offset ], jiffies_to_msecs( elem->period ), elem->eta );

	list_add_tail( & elem->ev_head, & fchip->worker_head );

	gpio_fake_worker_update( fchip );

	r = true;

out:
	return r;
}

bool gpio_fake_worker_remove( struct gpio_fake_chip *fchip, u16 gpio_offset ) {

	bool r;

	struct pinctrl_fake *pctrl;
	struct gpio_chip *chip;
	struct gpio_fake_worker_elem *elem;

	chip = & fchip->gpiochip;
	pctrl = gpiochip_get_data( &fchip->gpiochip );

	if ( gpio_offset >= fchip->npins ) {
		r = false;
//		dev_err( chip->cdev, "GPIO Worker: invalid gpio_offset %u\n", gpio_offset );
		goto out;
	}

	elem = gpio_fake_worker_search_by_offset( & fchip->worker_head, gpio_offset );
	if ( NULL == elem ) {
		r = false;
//		dev_err( chip->cdev, "GPIO Worker: pin %u already removed\n", fchip->pins[ gpio_offset ] );
		goto out;
	}

	list_del( & elem->ev_head );
	kfree( elem );

//	dev_info( chip->cdev, "GPIO Worker: disabled on pin %u\n", fchip->pins[ gpio_offset ] );

	gpio_fake_worker_update( fchip );

	r = true;

out:
	return r;
}
