#ifndef PINCTRL_FAKE_GPIO_H_
#define PINCTRL_FAKE_GPIO_H_

#include <linux/types.h>
#include <linux/gpio/driver.h>

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
struct pinctrl_fake_gpio_chip {
	struct gpio_chip gpiochip;
	const char *group;
	unsigned npins;
	const unsigned *pins;
	u8 *values;
	u8 *directions;
	u8 *irq_types;
};

struct pinctrl_fake;

int pinctrl_fake_gpio_chip_init( struct pinctrl_fake *pctrl, struct gpio_chip *ch, u16 ngpio, const char *label );

#endif /* PINCTRL_FAKE_GPIO_H_ */
