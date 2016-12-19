#ifndef CONFIG_GPIO_FAKE_WORKER_H_
#define CONFIG_GPIO_FAKE_WORKER_H_

#include <linux/types.h>

#define CONFIG_GPIO_FAKE_WORKER_PERIOD_MS_MIN 1000
#define CONFIG_GPIO_FAKE_WORKER_PERIOD_MS_MAX 10000

#define CONFIG_GPIO_FAKE_WORKER_PERIOD_MS_DEFAULT CONFIG_GPIO_FAKE_WORKER_PERIOD_MS_MIN

#define CONFIG_GPIO_FAKE_WORKER_ETA_MS_EPSILON 100

struct gpio_fake_chip;

/**
 * gpio_fake_worker_init
 *
 * @brief        Set up work for toggling fake GPIO.
 *
 * @param fchip  The gpio_fake_chip with which the work in
 *               question is associated.
 */
void gpio_fake_worker_init( struct gpio_fake_chip *fchip );

/**
 * gpio_fake_worker_init
 *
 * @brief        Tear down work for toggling fake GPIO.
 *
 * @param fchip  The gpio_fake_chip with which the work in
 *               question is associated.
 */
void gpio_fake_worker_fini( struct gpio_fake_chip *fchip );

/**
 * gpio_fake_worker_add
 *
 * @brief              Start periodically toggling a fake input GPIO.
 *
 * @param fchip        The gpio_fake_chip to which the GPIO in
 *                     question belongs.
 * @param gpio_offset  The offset of the input gpio being toggled.
 * @return             True if the GPIO in question was successfully added,
 *                     otherwise false.
 */
bool gpio_fake_worker_add( struct gpio_fake_chip *fchip, u16 gpio_offset );

/**
 * gpio_fake_worker_remove
 *
 * @brief              Stop periodically toggling a fake input GPIO.
 *
 * @param fchip        The gpio_fake_chip to which the GPIO in
 *                     question belongs.
 * @param gpio_offset  The offset of the input gpio being toggled.
 * @return             True if the GPIO in question was successfully removed,
 *                     otherwise false.
 */
bool gpio_fake_worker_remove( struct gpio_fake_chip *fchip, u16 gpio_offset );

/**
 * gpio_fake_worker_period_ms_get
 *
 * @brief              Get the period, in milliseconds, of a fake GPIO being
 *                     toggled.
 *
 * @param fchip        The gpio_fake_chip to which the gpio in
 *                     question belongs.
 * @param gpio_offset  The offset of the input gpio being toggled.
 * @param period_ms    A memory location to store the period in milliseconds.
 * @return             True if the value has been returned at the location
 *                     pointed to by @period_ms, otherwise false.
 */
bool gpio_fake_worker_period_ms_get( struct gpio_fake_chip *fchip, u16 gpio_offset, unsigned *period_ms );

/**
 * gpio_fake_worker_period_ms_set
 *
 * @brief              Set the period, in milliseconds, of a fake GPIO being
 *                     toggled.
 *
 * @param fchip        The gpio_fake_chip to which the gpio in
 *                     question belongs.
 * @param gpio_offset  The offset of the input gpio being toggled.
 * @param period_ms    A memory location containing the period in milliseconds.
 * @return             True if the value in the location pointed to by
 *                     @period_ms has been used, otherwise false.
 */
bool gpio_fake_worker_period_ms_set( struct gpio_fake_chip *fchip, u16 gpio_offset, unsigned *period_ms );

#endif /* CONFIG_GPIO_FAKE_WORKER_H_ */
