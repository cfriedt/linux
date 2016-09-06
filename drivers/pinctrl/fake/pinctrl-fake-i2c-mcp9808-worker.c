#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>

#include <linux/i2c.h>

#include <linux/interrupt.h>

#include "pinctrl-fake.h"

static void pinctrl_fake_i2c_mcp9808_work( struct work_struct *work ) {

	static const int16_t mcp9808_ta_min = -(1 << 7);
	static const int16_t mcp9808_ta_max =  (1 << 7);
	static const int16_t mcp9808_ta_range = mcp9808_ta_max - mcp9808_ta_min;
	static const int16_t delta = mcp9808_ta_range >> 3;

	struct delayed_work *dwork;
	struct pinctrl_fake_i2c_mcp9808_worker *worker;
	struct pinctrl_fake_i2c_device_mcp9808 *therm;
	struct pinctrl_fake_i2c_chip *ichip;

	int16_t sign;
	int16_t temperature;
	int16_t old_temperature;
	int16_t new_temperature;

	dwork = container_of( work, struct delayed_work, work );
	worker = container_of( dwork, struct pinctrl_fake_i2c_mcp9808_worker, dwork );
	therm = container_of( worker, struct pinctrl_fake_i2c_device_mcp9808, worker );
	ichip = container_of( therm, struct pinctrl_fake_i2c_chip, therm );

	sign = ( therm->reg[ MCP9808_TA ] >> ( 4 + 8 ) ) & 0x1;
	temperature = ( therm->reg[ MCP9808_TA ] >> 4 ) & 0xff;
	temperature = sign ? -temperature : temperature;

	old_temperature = temperature;

	temperature += delta;
	if ( temperature < 0 ) {
		sign = 1;
		temperature = -temperature;
	} else if ( temperature > 0 ) {
		if ( temperature >= mcp9808_ta_max ) {
			sign = 1;
			temperature = mcp9808_ta_min + ( temperature - mcp9808_ta_max );
			temperature = -temperature;
		} else {
			sign = 0;
		}
	}
	new_temperature = sign ? -temperature : temperature;

	therm->reg[ MCP9808_TA ] &= ~( 0x1ff << 4 );
	therm->reg[ MCP9808_TA ] |= sign << ( 4 + 8 );
	therm->reg[ MCP9808_TA ] |= ( temperature & 0xff ) << 4;

	dev_info( & ichip->adapter.dev, "old_temperature: %d, new_temperature: %d\n", old_temperature, new_temperature );

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
