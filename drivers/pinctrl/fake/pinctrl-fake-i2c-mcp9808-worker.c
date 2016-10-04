#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>

#include <linux/i2c.h>

#include <linux/interrupt.h>

#include "pinctrl-fake.h"

static u16 abs16( s16 x ) {
	u16 y;
	y = x < 0 ? -x : x;
	return y;
}

static s16 mcp9808_treg_to_temperature( u16 reg ) {
	s16 temperature;

	s16 sign;

	sign = ( reg >> ( 4 + 8 ) ) & 0x0001;
	temperature = ( reg >> 4 ) & 0x00ff;
	temperature = sign ? -temperature : temperature;

	return temperature;
}

static u16 mcp9808_temperature_to_treg( s16 temperature ) {
	u16 reg;

	u16 sign;

	sign = temperature < 0 ? 1 : 0;

	reg = 0;
	reg |= ( sign << 12 ) & 0x1000;
	reg |= ( abs16( temperature ) << 4 ) & 0x0ff0;

	return reg;
}

static s16 mcp9808_temperature_update( s16 temperature ) {

	static const s16 mcp9808_ta_min = -(1 << 7);
	static const s16 mcp9808_ta_max =  (1 << 7);
	static const s16 mcp9808_ta_range = mcp9808_ta_max - mcp9808_ta_min;
	static const s16 delta = mcp9808_ta_range >> 3;

	s16 temperature_new;

	temperature_new = temperature;

	temperature_new += delta;
	if ( temperature_new >= mcp9808_ta_max ) {
		temperature_new -= mcp9808_ta_range;
	}

	return temperature_new;
}

static void pinctrl_fake_i2c_mcp9808_work( struct work_struct *work ) {

	struct delayed_work *dwork;
	struct pinctrl_fake_i2c_mcp9808_worker *worker;
	struct pinctrl_fake_i2c_device_mcp9808 *therm;
	struct pinctrl_fake_i2c_chip *ichip;
	struct pinctrl_fake_gpio_chip *fchip;

	s16 tlower;
	s16 tupper;
	s16 tcrit;
	s16 tambient;
	u16 ta_reg;

	bool ta_vs_tcrit;
	bool ta_vs_tlower;
	bool ta_vs_tupper;

	bool alert_stat;

	u16 config;

	dwork = container_of( work, struct delayed_work, work );
	worker = container_of( dwork, struct pinctrl_fake_i2c_mcp9808_worker, dwork );
	therm = container_of( worker, struct pinctrl_fake_i2c_device_mcp9808, worker );
	ichip = container_of( therm, struct pinctrl_fake_i2c_chip, therm );

	tambient = mcp9808_treg_to_temperature( therm->reg[ MCP9808_TA ] );
	tlower = mcp9808_treg_to_temperature( therm->reg[ MCP9808_TLOWER ] );
	tupper = mcp9808_treg_to_temperature( therm->reg[ MCP9808_TUPPER ] );
	tcrit = mcp9808_treg_to_temperature( therm->reg[ MCP9808_TCRIT ] );

	tambient = mcp9808_temperature_update( tambient );
	ta_reg = mcp9808_temperature_to_treg( tambient );

	config = therm->reg[ MCP9808_CONFIG ];

	dev_dbg( & ichip->adapter.dev, "tambient: %d\n", tambient );
	dev_dbg( & ichip->adapter.dev, "tcrit:    %d\n", tcrit );
	dev_dbg( & ichip->adapter.dev, "tupper:   %d\n", tupper );
	dev_dbg( & ichip->adapter.dev, "tlower:   %d\n", tlower );

	dev_dbg( & ichip->adapter.dev, "config:   %04x\n", config );

	ta_vs_tcrit = tambient >= tcrit;
	ta_reg |=  ( (  ta_vs_tcrit ) << 15 );

	ta_vs_tupper = tambient > tupper;
	ta_reg |=  ( ( ta_vs_tupper ) << 14 );

	ta_vs_tlower = tambient < tlower;
	ta_reg |=  ( ( ta_vs_tlower ) << 13 );

	therm->reg[ MCP9808_TA ] = ta_reg;

	alert_stat = false;
	config &= ~(alert_stat << 4);

	if ( 0 != ( config & ( 1 << 3 ) ) ) {

		if ( ta_vs_tcrit ) {
			dev_info( & ichip->adapter.dev, "ta (%d) >= tcrit (%d) -> interrupt\n", tambient, tcrit );
			alert_stat = true;
		}

		if ( 0 == ( therm->reg[ MCP9808_CONFIG ] & ( 1 << 2 ) ) ) {
			if ( ta_vs_tupper ) {
				dev_info( & ichip->adapter.dev, "ta (%d) > tupper (%d) -> interrupt\n", tambient, tupper );
				alert_stat = true;
			}
			if ( ta_vs_tlower ) {
				dev_info( & ichip->adapter.dev, "ta (%d) < tlower (%d) -> interrupt\n", tambient, tlower );
				alert_stat = true;
			}
		}
	}
	config |= alert_stat << 4;
	if ( alert_stat ) {
		fchip = worker->fchip;
		if ( !( NULL == fchip || worker->fchip_offset < 0 || worker->fchip_offset >= fchip->npins ) ) {
			fchip->pended[ worker->fchip_offset ] = true;
			fchip->values[ worker->fchip_offset ] =
				0 == ( config & 2 )
				? false
				: true;
			//dev_info( & ichip->adapter.dev, "MCP9808 Worker: trigger interrupt %u for %s pin %u (hw pin %u offset %u)", fchip->gpiochip.to_irq( & fchip->gpiochip, worker->fchip_offset ), dev_name( fchip->gpiochip.cdev ), fchip->pins[ worker->fchip_offset ] );
			dev_info( & ichip->adapter.dev, "MCP9808 Worker: trigger interrupt %u for %s pin %u\n", fchip->gpiochip.to_irq( & fchip->gpiochip, worker->fchip_offset ), dev_name( fchip->gpiochip.cdev ), fchip->pins[ worker->fchip_offset ] );
			tasklet_schedule( & fchip->tasklet );
		}
	}

	therm->reg[ MCP9808_CONFIG ] = config;

	schedule_delayed_work( dwork, msecs_to_jiffies( worker->period_ms ) );
}

int pinctrl_fake_i2c_mcp9808_worker_init( struct pinctrl_fake_i2c_mcp9808_worker *worker, unsigned period_ms, struct pinctrl_fake_gpio_chip *fchip, int fchip_offset ) {
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

	if ( !( NULL == fchip || fchip_offset < 0 || fchip_offset >= fchip->npins ) ) {

		dev_info( & ichip->adapter.dev, "MCP9808 Worker reserving %s pin %d (hw pin %u, offset %u) for notifications\n", dev_name( fchip->gpiochip.cdev ), fchip->gpiochip.base + fchip_offset, fchip->pins[ fchip_offset ], fchip_offset );

		therm->reg[ MCP9808_CONFIG ] &= ~2; // ensure active low by (default)
		fchip->values[ fchip_offset ] = true;

		fchip->reserved[ fchip_offset ] = true;
		fchip->directions[ fchip_offset ] = GPIOF_DIR_IN;

		worker->fchip = fchip;
		worker->fchip_offset = fchip_offset;
	}

	INIT_DELAYED_WORK( & therm->worker.dwork, pinctrl_fake_i2c_mcp9808_work );
	schedule_delayed_work( & therm->worker.dwork, msecs_to_jiffies( therm->worker.period_ms ) );

	dev_info( & ichip->adapter.dev, "MCP9808 Worker started\n" );

	r = EXIT_SUCCESS;

out:
	return r;
}

void pinctrl_fake_i2c_mcp9808_worker_fini( struct pinctrl_fake_i2c_mcp9808_worker *worker ) {

	struct pinctrl_fake_i2c_device_mcp9808 *therm;
	struct pinctrl_fake_i2c_chip *ichip;
	struct pinctrl_fake_gpio_chip *fchip;

	therm = container_of( worker, struct pinctrl_fake_i2c_device_mcp9808, worker );
	ichip = container_of( therm, struct pinctrl_fake_i2c_chip, therm );

	fchip = worker->fchip;
	if ( !( NULL == fchip || worker->fchip_offset < 0 || worker->fchip_offset >= fchip->npins ) ) {

		dev_info( & ichip->adapter.dev, "MCP9808 Worker un-reserving %s pin %d for notifications\n", dev_name( fchip->gpiochip.cdev ), fchip->pins[ worker->fchip_offset ] );

		fchip->reserved[ worker->fchip_offset ] = false;
		fchip->pended[ worker->fchip_offset ] = false;

		worker->fchip = NULL;
		worker->fchip_offset = -1;
	}

	cancel_delayed_work( & therm->worker.dwork );

	dev_info( & ichip->adapter.dev, "MCP9808 Worker stopped\n" );

//	gpio_free( therm->worker.interrupt_line );
}
