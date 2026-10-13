/*
 * Copyright (c) Arduino s.r.l. and/or its affiliated companies
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/fatal.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/atomic.h>

#include "error_led.h"

/*
 * Error indicator LED. Boards that have a red LED point the "error-led" alias at
 * it in their variant overlay; otherwise we fall back to "led0" (any color).
 * The blink patterns, not the color, distinguish the failure cause.
 */
#if DT_NODE_EXISTS(DT_ALIAS(error_led))
#define ERROR_LED_NODE DT_ALIAS(error_led)
#elif DT_NODE_EXISTS(DT_ALIAS(led0))
#define ERROR_LED_NODE DT_ALIAS(led0)
#endif

#ifdef ERROR_LED_NODE
static const struct gpio_dt_spec error_led = GPIO_DT_SPEC_GET(ERROR_LED_NODE, gpios);
#endif

/* All board LEDs, switched off so only the error LED is visible (e.g. on RGB LEDs). */
#define LED_GPIO_SPEC(node_id) GPIO_DT_SPEC_GET(node_id, gpios),
static const struct gpio_dt_spec board_leds[] = {
#if DT_HAS_COMPAT_STATUS_OKAY(gpio_leds)
	DT_FOREACH_CHILD_STATUS_OKAY(DT_INST(0, gpio_leds), LED_GPIO_SPEC)
#endif
};

/* A blink pattern is a cyclic list of LED states, each held for "ms". */
struct blink_step {
	int level;
	uint32_t ms;
};

/* Sketch crash: mbed_die() rhythm, 4 fast pulses + 4 slow pulses. */
static const struct blink_step crash_pattern[] = {
	{1, 150}, {0, 150}, {1, 150}, {0, 150}, {1, 150}, {0, 150}, {1, 150}, {0, 150},
	{1, 400}, {0, 400}, {1, 400}, {0, 400}, {1, 400}, {0, 400}, {1, 400}, {0, 400},
};

/* Recoverable loader failure: 3 short pulses + long pause. */
static const struct blink_step recoverable_pattern[] = {
	{1, 200}, {0, 200}, {1, 200}, {0, 200}, {1, 200}, {0, 1200},
};

static const struct gpio_dt_spec *error_led_setup(void) {
	for (size_t i = 0; i < ARRAY_SIZE(board_leds); i++) {
		if (gpio_is_ready_dt(&board_leds[i])) {
			gpio_pin_configure_dt(&board_leds[i], GPIO_OUTPUT_INACTIVE);
		}
	}

#ifdef ERROR_LED_NODE
	if (gpio_is_ready_dt(&error_led)) {
		gpio_pin_configure_dt(&error_led, GPIO_OUTPUT_INACTIVE);
		return &error_led;
	}
#endif
	return NULL;
}

/*
 * Hard die: for fatal / ISR context (sketch crashes). Locks interrupts and uses
 * busy waits because the scheduler (and therefore workqueues/timers) can no
 * longer be relied upon. Plays crash_pattern in place, forever.
 */
FUNC_NORETURN static void fatal_error_led(void) {
	(void)irq_lock();

	const struct gpio_dt_spec *led = error_led_setup();

	while (1) {
		for (size_t i = 0; i < ARRAY_SIZE(crash_pattern); i++) {
			if (led) {
				gpio_pin_set_dt(led, crash_pattern[i].level);
			}
			k_busy_wait(crash_pattern[i].ms * 1000U);
		}
	}
}

/*
 * Soft die: for recoverable loader failures (the sketch could not be loaded or
 * started). Plays recoverable_pattern from the system workqueue so the caller
 * returns immediately and the debug console / USB stay alive for inspection.
 */
static const struct gpio_dt_spec *recoverable_led;
static atomic_t recoverable_blink_started = ATOMIC_INIT(0);

static void recoverable_blink_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(recoverable_blink_work, recoverable_blink_work_handler);

static void recoverable_blink_work_handler(struct k_work *work) {
	ARG_UNUSED(work);
	static size_t step;

	gpio_pin_set_dt(recoverable_led, recoverable_pattern[step].level);
	uint32_t delay = recoverable_pattern[step].ms;
	step = (step + 1) % ARRAY_SIZE(recoverable_pattern);

	k_work_reschedule(&recoverable_blink_work, K_MSEC(delay));
}

void recoverable_error_led(void) {
	if (!atomic_cas(&recoverable_blink_started, 0, 1)) {
		return;
	}

	recoverable_led = error_led_setup();
	if (!recoverable_led) {
		atomic_set(&recoverable_blink_started, 0);
		return;
	}

	k_work_reschedule(&recoverable_blink_work, K_NO_WAIT);
}

/*
 * Catch sketch crashes (CPU faults, panics, asserts, ...). Overrides the weak
 * Zephyr default so a crashing sketch blinks the error LED instead of silently
 * halting.
 */
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf) {
	ARG_UNUSED(esf);
	LOG_PANIC();
	printk("Sketch crashed, fatal error reason %u\n", reason);
	fatal_error_led();
}
