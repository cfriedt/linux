#ifndef GPIO_FAKE_H_
#define GPIO_FAKE_H_

#include <linux/types.h>
#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/workqueue.h>
#include <linux/list.h>
#include <linux/bitops.h>

#include <linux/interrupt.h>

#include "gpio-fake-worker.h"

/**
 * @gpiochip   - gpio chip, for api purposes
 * @name       - name of the pinctrl group associated with this gpio chip's
 *               pinrange
 * @npins      - number of pins in this gpio chip's pinrange
 * @pins       - array of pin numbers, length @npins
 * @values     - array of pin value, length @npins
 * @directions - array of pin directions, length @npins. E.g. GPIOF_DIR_IN or GPIOF_DIR_OUT
 * @irq_types  - array of pin irq types, of length @npins
 */
struct gpio_fake_chip {
	struct gpio_chip gpiochip;
	const char *group;
	u16 npins;
	const u16 *pins;
	u8 *values;
	u8 *directions;
	u8 *irq_types;
	u8 *pended;
	u8 *reserved;
	struct tasklet_struct tasklet;
#ifdef CONFIG_GPIO_FAKE_WORKER
	struct delayed_work worker_dwork;
	struct list_head worker_head;
#endif // CONFIG_CONFIG_GPIO_FAKE_WORKER
	struct list_head head;
};

struct pinctrl_fake;

int gpio_fake_chip_init( struct pinctrl_fake *pctrl, struct gpio_chip *chip, u16 ngpio, const char *label );
void gpio_fake_chip_fini( struct gpio_chip *chip );

void gpio_fake_irq_handler( struct irq_desc *irq_desc );

#endif /* GPIO_FAKE_H_ */
