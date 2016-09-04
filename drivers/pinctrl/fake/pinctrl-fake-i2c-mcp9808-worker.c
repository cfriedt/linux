#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>

#include <linux/i2c.h>

#include <linux/interrupt.h>

#include "pinctrl-fake.h"

static void pinctrl_fake_i2c_mcp9808_work( struct work_struct *work ) {

	struct delayed_work *dwork;
	struct pinctrl_fake_i2c_mcp9808_worker *worker;
	struct pinctrl_fake_i2c_device_mcp9808 *therm;
	struct pinctrl_fake_i2c_chip *ichip;

	dwork = container_of( work, struct delayed_work, work );
	worker = container_of( dwork, struct pinctrl_fake_i2c_mcp9808_worker, dwork );
	therm = container_of( worker, struct pinctrl_fake_i2c_device_mcp9808, worker );
	ichip = container_of( therm, struct pinctrl_fake_i2c_chip, therm );

	schedule_delayed_work( dwork, msecs_to_jiffies( worker->period_ms ) );
}

int pinctrl_fake_i2c_mcp9808_worker_init( struct pinctrl_fake_i2c_mcp9808_worker *worker, unsigned period_ms, unsigned interrupt_line ) {
	int r;

	struct pinctrl_fake_i2c_device_mcp9808 *therm;
	struct pinctrl_fake_i2c_chip *ichip;

	therm = container_of( worker, struct pinctrl_fake_i2c_device_mcp9808, worker );
	ichip = container_of( therm, struct pinctrl_fake_i2c_chip, therm );

	if ( period_ms < I2C_MCP9808_PERIOD_MS_MIN || period_ms > I2C_MCP9808_PERIOD_MS_MAX ) {
		r = -EINVAL;
		dev_err( & ichip->adapter.dev, "invalid period_ms %u\n", period_ms );
		goto out;
	}
	therm->worker.period_ms = period_ms;

	// XXX: request GPIO
//	r = gpio_request( interrupt_line, "mcp9808-alert" );
//	if ( EXIT_SUCCESS != r ) {
//		dev_err( & ichip->adapter.dev, "unable to request gpio %u\n", interrupt_line );
//		if ( r > 0 ) {
//			r = -ENXIO;
//		}
//		goto out;
//	}
	therm->worker.interrupt_line = interrupt_line;

	INIT_DELAYED_WORK( & therm->worker.dwork, pinctrl_fake_i2c_mcp9808_work );
	schedule_delayed_work( & therm->worker.dwork, msecs_to_jiffies( therm->worker.period_ms ) );

	dev_info( & ichip->adapter.dev, "MPC9808 Worker started\n" );

	r = EXIT_SUCCESS;

out:
	return r;
}

void pinctrl_fake_i2c_mcp9808_worker_fini( struct pinctrl_fake_i2c_mcp9808_worker *worker ) {

	struct pinctrl_fake_i2c_device_mcp9808 *therm;
	struct pinctrl_fake_i2c_chip *ichip;

	therm = container_of( worker, struct pinctrl_fake_i2c_device_mcp9808, worker );
	ichip = container_of( therm, struct pinctrl_fake_i2c_chip, therm );

	cancel_delayed_work( & therm->worker.dwork );

	dev_info( & ichip->adapter.dev, "MPC9808 Worker stopped\n" );

//	gpio_free( therm->worker.interrupt_line );
}
