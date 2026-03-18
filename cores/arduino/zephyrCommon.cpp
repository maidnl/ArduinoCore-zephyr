/*
 * Copyright (c) 2022 Dhruva Gole
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Arduino.h>
#include <cstdint>
#include "zephyrInternal.h"
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/devicetree.h>

/* Helper macro to extract individual elements from a Devicetree array property */
#define EXTRACT_PINCTRL_IDX(node_id, prop, idx) DT_PROP_BY_IDX(node_id, prop, idx),
#ifdef CONFIG_PWM
#if DT_NODE_HAS_PROP(DT_PATH(zephyr_user), pwm_pinctrl_idx)
/* Automatically generate the PWM pinctrl mapping from the Devicetree */
const uint8_t arduino_pwm_pinctrl_idx[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), pwm_pinctrl_idx, EXTRACT_PINCTRL_IDX)};
#endif
#endif

#ifdef CONFIG_ADC
#if DT_NODE_HAS_PROP(DT_PATH(zephyr_user), adc_pinctrl_idx)
/* Automatically generate the ADC pinctrl mapping from the Devicetree */
const uint8_t arduino_adc_pinctrl_idx[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), adc_pinctrl_idx, EXTRACT_PINCTRL_IDX)};
#endif
#endif

#if defined(ARDUINO)
/*
 * The global ARDUINO macro is numeric (e.g. 10607) in Arduino builds.
 * Temporarily hide it so pinctrl token concatenation can use the literal
 * custom state name "ARDUINO" from devicetree pinctrl-names.
 * Otherwise, the generated pinctrl state identifiers would be like PINCTRL_STATE_10607 instead of
 * PINCTRL_STATE_ARDUINO.
 */
#pragma push_macro("ARDUINO")
#undef ARDUINO
#endif

/* COND_CODE_1 insert the code defined in the second argument if the first
 * argument is true (otherwise it uses the third argument)  */

#define PINCTRL_DEFINE_IF_PRESENT(node_id)                                                         \
	COND_CODE_1(DT_NODE_HAS_PROP(node_id, pinctrl_0), (PINCTRL_DT_DEFINE(node_id);), ())

/* Invoque the argument macro for each node which has status okay in the DT
 * Since the macro argument uses COND_CODE_1 this means that the invocation is
 * further "restricted" only for nodes which have pinctrl-0 defined
 * So for each node which is okay and has pinctrl-0 it is called
 * PINCTRL_DT_DEFINE which defines and initialize the pin control configuration
 * for the device at node id
 * */
DT_FOREACH_STATUS_OKAY_NODE(PINCTRL_DEFINE_IF_PRESENT)

/* structure that holds device and pinctrl configuration */
struct pinctrl_map_entry {
	const struct device *dev;
	const struct pinctrl_dev_config *pcfg;
};

/* Macros to safely extract devices depending on how they are defined in DT */
#define MAP_ENTRY_PWM(n, p, i)                                                                     \
	{DEVICE_DT_GET(DT_PWMS_CTLR_BY_IDX(n, i)),                                                     \
	 PINCTRL_DT_DEV_CONFIG_GET(DT_PWMS_CTLR_BY_IDX(n, i))},
#define MAP_ENTRY_ADC(n, p, i)                                                                     \
	{DEVICE_DT_GET(DT_IO_CHANNELS_CTLR_BY_IDX(n, i)),                                              \
	 PINCTRL_DT_DEV_CONFIG_GET(DT_IO_CHANNELS_CTLR_BY_IDX(n, i))},
#define MAP_ENTRY_PHANDLE(n, p, i)                                                                 \
	{DEVICE_DT_GET(DT_PHANDLE_BY_IDX(n, p, i)),                                                    \
	 PINCTRL_DT_DEV_CONFIG_GET(DT_PHANDLE_BY_IDX(n, p, i))},

static const struct pinctrl_map_entry pinctrl_map[] = {
/* Only map PWMs if enabled */
#ifdef CONFIG_PWM
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), pwms, MAP_ENTRY_PWM)
#endif

/* Only map ADCs if enabled */
#ifdef CONFIG_ADC
		DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels, MAP_ENTRY_ADC)
#endif

/* Only map I2C/SPI/UART if defined in zephyr,user */
#if defined(CONFIG_I2C) && DT_NODE_HAS_PROP(DT_PATH(zephyr_user), i2cs)
			DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), i2cs, MAP_ENTRY_PHANDLE)
#endif

#if defined(CONFIG_SPI) && DT_NODE_HAS_PROP(DT_PATH(zephyr_user), spis)
				DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), spis, MAP_ENTRY_PHANDLE)
#endif

#if defined(CONFIG_SERIAL) && DT_NODE_HAS_PROP(DT_PATH(zephyr_user), uarts)
					DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), uarts, MAP_ENTRY_PHANDLE)
#endif

#if defined(CONFIG_DAC) && DT_NODE_HAS_PROP(DT_PATH(zephyr_user), dac)
						{DEVICE_DT_GET(DT_PHANDLE(DT_PATH(zephyr_user), dac)),
						 PINCTRL_DT_DEV_CONFIG_GET(DT_PHANDLE(DT_PATH(zephyr_user), dac))},
#endif

	{NULL, NULL} /* Terminate the array */
};
#if defined(ARDUINO)
#pragma pop_macro("ARDUINO")
#endif
/* --- 3. The Auto-Detect Function (KEEP THIS!) --- */
static const struct pinctrl_dev_config *get_known_pcfg(const struct device *dev) {
	for (size_t i = 0; i < ARRAY_SIZE(pinctrl_map); i++) {
		if (pinctrl_map[i].dev == dev) {
			return pinctrl_map[i].pcfg;
		}
	}
	return nullptr;
}

/**
 * @brief Apply a single pin from a custom pinctrl state.
 */
int arduino_pinctrl_pin(const struct pinctrl_dev_config *pcfg, uint8_t pin_sub_idx,
						uint8_t state_id = PINCTRL_STATE_ARDUINO) {
	if (pcfg == nullptr) {
		return -EINVAL;
	}

	const struct pinctrl_state *state;

	/* Look up the requested state */
	int err = pinctrl_lookup_state(pcfg, state_id, &state);
	if (err < 0) {
		return err; /* Fails if the state is not defined in pinctrl-names */
	}

	/* bounds check */
	if (pin_sub_idx >= state->pin_cnt) {
		return -EINVAL;
	}

	/* extract register securely */
#ifdef CONFIG_PINCTRL_STORE_REG
	uintptr_t reg = pcfg->reg;
#else
	uintptr_t reg = PINCTRL_REG_NONE;
#endif

	/* apply only this specific pin */
	return pinctrl_configure_pins(&state->pins[pin_sub_idx], 1, reg);
}

/**
 * @brief Initialize a device (if needed) and apply a pinctrl configuration
 * either on a single pin (for "devices" that can be addressed as single pin
 * like ADC or PWM) or on all the pin used by the Peripheral (like I2C or SPI)
 */
bool begin_device(const struct device *dev, int16_t pin_sub_idx) {

	if (dev == nullptr) {
		return false;
	}

	const struct pinctrl_dev_config *pcfg = nullptr;

	/* find pinctrl configuration using device and look-up table pinctrl_map
	 * which associates a device to its pinctrl */
	pcfg = get_known_pcfg(dev);

	/* Initialize the device if not already is */
	if (!device_is_ready(dev)) {
		if (device_init(dev) < 0) {
			return false;
		}
	} else {
#ifdef CONFIG_PM_DEVICE
		int err = pm_device_action_run(dev, PM_DEVICE_ACTION_RESUME);
		if (err < 0 && err != -EALREADY && err != -ENOSYS && err != -ENOTSUP) {
			return false;
		}
#endif
	}
	/* apply pinctrl configuration */
	if (pcfg != nullptr) {
		if (pin_sub_idx >= 0) {
			/* on single pin */
			if (arduino_pinctrl_pin(pcfg, (uint8_t)pin_sub_idx, PINCTRL_STATE_ARDUINO)) {
				return false;
			}
		} else {
			/* on full peripheral pins */
			/* TODO: here we use ARDUINO custom state however for "complex"
			 * peripherals we can avoid defining arduino state and use default */
			if (pinctrl_apply_state(pcfg, PINCTRL_STATE_ARDUINO)) {
				return false;
			}
		}
	}
	return true;
}

/**
 * @brief de-initialize a device by putting its pins into sleep state
 * to be used only for "complex" peripherals and not single pin (because
 * historically single pin hardware does not have the concept of "end")
 */
void end_device(const struct device *dev) {
	if (dev == nullptr) {
		return;
	}

	const struct pinctrl_dev_config *pcfg = nullptr;
	pcfg = get_known_pcfg(dev);

	/* Suspends the hardware clock and applies the sleep pinctrl state */
#ifdef CONFIG_PM_DEVICE
	int err = pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND);
	/* if pinctrl is not supported put the pin in sleep state */
	if (err == -ENOSYS || err == -ENOTSUP) {
		if (pcfg != nullptr) {
			pinctrl_apply_state(pcfg, PINCTRL_STATE_SLEEP);
		}
	}
#else
	/* power management is not supporte -> apply pinctrl sleep state*/
	if (pcfg != nullptr) {
		pinctrl_apply_state(pcfg, PINCTRL_STATE_SLEEP);
	}

#endif
}

static const struct gpio_dt_spec arduino_pins[] = {
	DT_FOREACH_PROP_ELEM_SEP(
	DT_PATH(zephyr_user), digital_pin_gpios, GPIO_DT_SPEC_GET_BY_IDX, (, ))};

namespace {

#if DT_PROP_LEN(DT_PATH(zephyr_user), digital_pin_gpios) > 0

/*
 * Calculate GPIO ports/pins number statically from devicetree configuration
 */

template <class N, class Head> constexpr N sum_of_list(const N sum, const Head &head) {
	return sum + head;
}

template <class N, class Head, class... Tail>
constexpr N sum_of_list(const N sum, const Head &head, const Tail &...tail) {
	return sum_of_list(sum + head, tail...);
}

template <class N, class Head> constexpr N max_in_list(const N max, const Head &head) {
	return (max >= head) ? max : head;
}

template <class N, class Head, class... Tail>
constexpr N max_in_list(const N max, const Head &head, const Tail &...tail) {
	return max_in_list((max >= head) ? max : head, tail...);
}

template <class Query, class Head>
constexpr size_t is_first_appearance(const size_t &idx, const size_t &at, const size_t &found,
									 const Query &query, const Head &head) {
	return ((found == ((size_t)-1)) && (query == head) && (idx == at)) ? 1 : 0;
}

template <class Query, class Head, class... Tail>
constexpr size_t is_first_appearance(const size_t &idx, const size_t &at, const size_t &found,
									 const Query &query, const Head &head, const Tail &...tail) {
	return ((found == ((size_t)-1)) && (query == head) && (idx == at)) ?
			   1 :
			   is_first_appearance(idx + 1, at, (query == head ? idx : found), query, tail...);
}

#define GET_DEVICE_VARGS(n, p, i, _) DEVICE_DT_GET(DT_GPIO_CTLR_BY_IDX(n, p, i))
#define FIRST_APPEARANCE(n, p, i)                                                                  \
	is_first_appearance(0, i, ((size_t) - 1), DEVICE_DT_GET(DT_GPIO_CTLR_BY_IDX(n, p, i)),         \
						DT_FOREACH_PROP_ELEM_SEP_VARGS(n, p, GET_DEVICE_VARGS, (, ), 0))
const int port_num = sum_of_list(
	0, DT_FOREACH_PROP_ELEM_SEP(DT_PATH(zephyr_user), digital_pin_gpios,
            FIRST_APPEARANCE, (, )));

#define GPIO_NGPIOS(n, p, i) DT_PROP(DT_GPIO_CTLR_BY_IDX(n, p, i), ngpios)
const int max_ngpios = max_in_list(
	0, DT_FOREACH_PROP_ELEM_SEP(DT_PATH(zephyr_user), digital_pin_gpios, GPIO_NGPIOS, (, )));

#else

const int port_num = 1;
const int max_ngpios = 0;

#endif

/*
 * GPIO callback implementation
 */

struct arduino_callback {
	voidFuncPtr handler;
	bool enabled;
};

struct gpio_port_callback {
	struct gpio_callback callback;
	struct arduino_callback handlers[max_ngpios];
	gpio_port_pins_t pins;
	const struct device *dev;
} port_callback[port_num] = {0};

struct gpio_port_callback *find_gpio_port_callback(const struct device *dev) {
	for (size_t i = 0; i < ARRAY_SIZE(port_callback); i++) {
		if (port_callback[i].dev == dev) {
			return &port_callback[i];
		}
		if (port_callback[i].dev == nullptr) {
			port_callback[i].dev = dev;
			return &port_callback[i];
		}
	}

	return nullptr;
}

void setInterruptHandler(pin_size_t pinNumber, voidFuncPtr func) {
	struct gpio_port_callback *pcb = find_gpio_port_callback(arduino_pins[pinNumber].port);

	if (pcb) {
		pcb->handlers[arduino_pins[pinNumber].pin].handler = func;
	}
}

void handleGpioCallback(const struct device *port, struct gpio_callback *cb, uint32_t pins) {
	(void)port; // unused
	struct gpio_port_callback *pcb = (struct gpio_port_callback *)cb;

	for (uint32_t i = 0; i < max_ngpios; i++) {
		if (pins & BIT(i) && pcb->handlers[i].enabled) {
			pcb->handlers[i].handler();
		}
	}
}

#ifdef CONFIG_PWM

#define PWM_DT_SPEC(n, p, i) PWM_DT_SPEC_GET_BY_IDX(n, i),
#define PWM_PINS(n, p, i)                                                                          \
	DIGITAL_PIN_GPIOS_FIND_PIN(DT_REG_ADDR(DT_PHANDLE_BY_IDX(DT_PATH(zephyr_user), p, i)),         \
							   DT_PHA_BY_IDX(DT_PATH(zephyr_user), p, i, pin)),

const struct pwm_dt_spec arduino_pwm[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), pwms, PWM_DT_SPEC)};

/* pwm-pins node provides a mapping digital pin numbers to pwm channels */
const pin_size_t arduino_pwm_pins[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), pwm_pin_gpios, PWM_PINS)};

size_t pwm_pin_index(pin_size_t pinNumber) {
	for (size_t i = 0; i < ARRAY_SIZE(arduino_pwm_pins); i++) {
		if (arduino_pwm_pins[i] == pinNumber) {
			return i;
		}
	}
	return (size_t)-1;
}

#endif // CONFIG_PWM

#ifdef CONFIG_ADC

#define ADC_DT_SPEC(n, p, i) ADC_DT_SPEC_GET_BY_IDX(n, i),
#define ADC_PINS(n, p, i)                                                                          \
	DIGITAL_PIN_GPIOS_FIND_PIN(DT_REG_ADDR(DT_PHANDLE_BY_IDX(DT_PATH(zephyr_user), p, i)),         \
							   DT_PHA_BY_IDX(DT_PATH(zephyr_user), p, i, pin)),
#define ADC_CH_CFG(n, p, i) arduino_adc[i].channel_cfg,

static const struct adc_dt_spec arduino_adc[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels, ADC_DT_SPEC)};

/* io-channel-pins node provides a mapping digital pin numbers to adc channels */
const pin_size_t arduino_analog_pins[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), adc_pin_gpios, ADC_PINS)};

struct adc_channel_cfg channel_cfg[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels, ADC_CH_CFG)};

size_t analog_pin_index(pin_size_t pinNumber) {
	for (size_t i = 0; i < ARRAY_SIZE(arduino_analog_pins); i++) {
		if (arduino_analog_pins[i] == pinNumber) {
			return i;
		}
	}
	return (size_t)-1;
}

#endif // CONFIG_ADC
#ifdef CONFIG_DAC

#if (DT_NODE_HAS_PROP(DT_PATH(zephyr_user), dac))

#define DAC_NODE       DT_PHANDLE(DT_PATH(zephyr_user), dac)
#define DAC_RESOLUTION DT_PROP(DT_PATH(zephyr_user), dac_resolution)
static const struct device *const dac_dev = DEVICE_DT_GET(DAC_NODE);

#define DAC_CHANNEL_DEFINE(n, p, i)                                                                \
	{                                                                                              \
		.channel_id = DT_PROP_BY_IDX(n, p, i),                                                     \
		.resolution = DAC_RESOLUTION,                                                              \
		.buffered = true,                                                                          \
	},

static const struct dac_channel_cfg dac_ch_cfg[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), dac_channels, DAC_CHANNEL_DEFINE)};

#endif

#endif // CONFIG_DAC

static unsigned int irq_key;
static bool interrupts_disabled = false;
} // namespace

void yield(void) {
	k_yield();
}

/*
 *  The ACTIVE_HIGH flag is set so that A low physical
 *  level on the pin will be interpreted as value 0.
 *  A high physical level will be interpreted as value 1
 */
void pinMode(pin_size_t pinNumber, PinMode pinMode) {
	if (pinMode == INPUT) { // input mode
		gpio_pin_configure_dt(&arduino_pins[pinNumber], GPIO_INPUT | GPIO_ACTIVE_HIGH);
	} else if (pinMode == INPUT_PULLUP) { // input with internal pull-up
		gpio_pin_configure_dt(&arduino_pins[pinNumber],
							  GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_HIGH);
	} else if (pinMode == INPUT_PULLDOWN) { // input with internal pull-down
		gpio_pin_configure_dt(&arduino_pins[pinNumber],
							  GPIO_INPUT | GPIO_PULL_DOWN | GPIO_ACTIVE_HIGH);
	} else if (pinMode == OUTPUT) { // output mode
		gpio_pin_configure_dt(&arduino_pins[pinNumber], GPIO_OUTPUT_LOW | GPIO_ACTIVE_HIGH);
	}
}

void digitalWrite(pin_size_t pinNumber, PinStatus status) {
	gpio_pin_set_dt(&arduino_pins[pinNumber], status);
}

PinStatus digitalRead(pin_size_t pinNumber) {
	return (gpio_pin_get_dt(&arduino_pins[pinNumber]) == 1) ? HIGH : LOW;
}

struct k_timer arduino_pin_timers[ARRAY_SIZE(arduino_pins)];
struct k_timer arduino_pin_timers_timeout[ARRAY_SIZE(arduino_pins)];

void tone_expiry_cb(struct k_timer *timer) {
	const struct gpio_dt_spec *spec = (gpio_dt_spec *)k_timer_user_data_get(timer);
	gpio_pin_toggle_dt(spec);
}

void tone_timeout_cb(struct k_timer *timer) {
	pin_size_t pinNumber = (pin_size_t)(uintptr_t)k_timer_user_data_get(timer);
	noTone(pinNumber);
}

void tone(pin_size_t pinNumber, unsigned int frequency, unsigned long duration) {
	struct k_timer *timer = &arduino_pin_timers[pinNumber];
	const struct gpio_dt_spec *spec = &arduino_pins[pinNumber];
	k_timeout_t timeout;

	pinMode(pinNumber, OUTPUT);

	if (frequency == 0) {
		gpio_pin_set_dt(spec, 0);
		return;
	}

	timeout = K_NSEC(NSEC_PER_SEC / (2 * frequency));

	k_timer_init(timer, tone_expiry_cb, NULL);
	k_timer_user_data_set(timer, (void *)spec);
	gpio_pin_set_dt(spec, 1);
	k_timer_start(timer, timeout, timeout);

	if (duration > 0) {
		timer = &arduino_pin_timers_timeout[pinNumber];
		k_timer_init(timer, tone_timeout_cb, NULL);
		k_timer_user_data_set(timer, (void *)(uintptr_t)pinNumber);
		k_timer_start(timer, K_MSEC(duration), K_NO_WAIT);
	}
}

void noTone(pin_size_t pinNumber) {
	k_timer_stop(&arduino_pin_timers[pinNumber]);
	gpio_pin_set_dt(&arduino_pins[pinNumber], 0);
}

unsigned long micros(void) {
#ifdef CONFIG_TIMER_HAS_64BIT_CYCLE_COUNTER
	return k_cyc_to_us_floor32(k_cycle_get_64());
#else
	return k_cyc_to_us_floor32(k_cycle_get_32());
#endif
}

unsigned long millis(void) {
	return k_uptime_get_32();
}

#if defined(CONFIG_DAC) || defined(CONFIG_PWM)
static int _analog_write_resolution = 8;

void analogWriteResolution(int bits) {
	_analog_write_resolution = bits;
}

int analogWriteResolution() {
	return _analog_write_resolution;
}
#endif

#ifdef CONFIG_PWM

void analogWrite(pin_size_t pinNumber, int value) {
	size_t idx = pwm_pin_index(pinNumber);

	if (idx >= ARRAY_SIZE(arduino_pwm)) {
		return;
	}

	if (!begin_device(arduino_pwm[idx].dev, arduino_pwm_pinctrl_idx[idx])) {
		return;
	}

	/* 3. Clamp value and set pulse */
	value = map(value, 0, 1 << _analog_write_resolution, 0, arduino_pwm[idx].period);

	if (((uint32_t)value) > arduino_pwm[idx].period) {
		value = arduino_pwm[idx].period;
	} else if (value < 0) {
		value = 0;
	}

	(void)pwm_set_pulse_dt(&arduino_pwm[idx], value);
}
#endif

#ifdef CONFIG_DAC

void analogWrite(enum dacPins dacName, int value) {
	if (dacName >= NUM_OF_DACS) {
		return;
	}

	if (!begin_device(dac_dev, (int16_t)dacName)) {
		return;
	}

	/* 3. Setup channel and write value */
	dac_channel_setup(dac_dev, &dac_ch_cfg[dacName]);

	const int max_dac_value = 1U << dac_ch_cfg[dacName].resolution;
	dac_write_value(dac_dev, dac_ch_cfg[dacName].channel_id,
					map(value, 0, 1 << _analog_write_resolution, 0, max_dac_value));
}
#endif

#ifdef CONFIG_ADC

void __attribute__((weak)) analogReference(uint8_t mode) {
	/*
	 * The Arduino API not clearly defined what means of
	 * the mode argument of analogReference().
	 * Treat the value as equivalent to zephyr's adc_reference.
	 */
	for (size_t i = 0; i < ARRAY_SIZE(channel_cfg); i++) {
		channel_cfg[i].reference = static_cast<adc_reference>(mode);
	}
}

// Note: We can not update the arduino_adc structure as it is read only...
static int read_resolution = 10;

void analogReadResolution(int bits) {
	read_resolution = bits;
}

int analogReadResolution() {
	return read_resolution;
}

int analogRead(pin_size_t pinNumber) {
	int err;
	uint16_t buf;
	struct adc_sequence seq = {.buffer = &buf, .buffer_size = sizeof(buf)};
	size_t idx = analog_pin_index(pinNumber);

	if (idx >= ARRAY_SIZE(arduino_adc)) {
		return -EINVAL;
	}

	/* start adc on single pin */
	if (!begin_device(arduino_adc[idx].dev, arduino_adc_pinctrl_idx[idx])) {
		return -EIO;
	}
	/* configure channel */
	err = adc_channel_setup(arduino_adc[idx].dev, &arduino_adc[idx].channel_cfg);
	if (err < 0) {
		return err;
	}

	seq.channels = BIT(arduino_adc[idx].channel_id);
	seq.resolution = arduino_adc[idx].resolution;
	seq.oversampling = arduino_adc[idx].oversampling;

	err = adc_read(arduino_adc[idx].dev, &seq);
	if (err < 0) {
		return err;
	}

	/* Map the return value to the requested resolution */
	if (read_resolution == seq.resolution) {
		return buf;
	}
	if (read_resolution < seq.resolution) {
		return buf >> (seq.resolution - read_resolution);
	}
	return buf << (read_resolution - seq.resolution);
}
#endif

void attachInterrupt(pin_size_t pinNumber, voidFuncPtr callback, PinStatus pinStatus) {
	struct gpio_port_callback *pcb;
	gpio_flags_t intmode = 0;

	if (!callback) {
		return;
	}

	if (pinStatus == LOW) {
		intmode |= GPIO_INT_LEVEL_LOW;
	} else if (pinStatus == HIGH) {
		intmode |= GPIO_INT_LEVEL_HIGH;
	} else if (pinStatus == CHANGE) {
		intmode |= GPIO_INT_EDGE_BOTH;
	} else if (pinStatus == FALLING) {
		intmode |= GPIO_INT_EDGE_FALLING;
	} else if (pinStatus == RISING) {
		intmode |= GPIO_INT_EDGE_RISING;
	} else {
		return;
	}

	pcb = find_gpio_port_callback(arduino_pins[pinNumber].port);
	__ASSERT(pcb != nullptr, "gpio_port_callback not found");

	pcb->pins |= BIT(arduino_pins[pinNumber].pin);
	setInterruptHandler(pinNumber, callback);
	enableInterrupt(pinNumber);

	gpio_pin_interrupt_configure(arduino_pins[pinNumber].port, arduino_pins[pinNumber].pin,
								 intmode);
	gpio_init_callback(&pcb->callback, handleGpioCallback, pcb->pins);
	gpio_add_callback(arduino_pins[pinNumber].port, &pcb->callback);
}

void detachInterrupt(pin_size_t pinNumber) {
	setInterruptHandler(pinNumber, nullptr);
	disableInterrupt(pinNumber);
}

#ifndef CONFIG_MINIMAL_LIBC_RAND

#include <stdlib.h>

void randomSeed(unsigned long seed) {
	srand(seed);
}

long random(long min, long max) {
	return rand() % (max - min) + min;
}

long random(long max) {
	return rand() % max;
}

#endif

unsigned long pulseIn(pin_size_t pinNumber, uint8_t state, unsigned long timeout) {
	struct k_timer timer;
	int64_t start, end, delta = 0;
	const struct gpio_dt_spec *spec = &arduino_pins[pinNumber];

	if (!gpio_is_ready_dt(spec)) {
		return 0;
	}

	k_timer_init(&timer, NULL, NULL);
	k_timer_start(&timer, K_MSEC(timeout), K_NO_WAIT);

	while (gpio_pin_get_dt(spec) == state && k_timer_status_get(&timer) == 0)
		;
	if (k_timer_status_get(&timer) > 0) {
		goto cleanup;
	}

	while (gpio_pin_get_dt(spec) != state && k_timer_status_get(&timer) == 0)
		;
	if (k_timer_status_get(&timer) > 0) {
		goto cleanup;
	}

	start = k_uptime_ticks();
	while (gpio_pin_get_dt(spec) == state && k_timer_status_get(&timer) == 0)
		;
	if (k_timer_status_get(&timer) > 0) {
		goto cleanup;
	}
	end = k_uptime_ticks();

	delta = k_ticks_to_us_floor64(end - start);

cleanup:
	k_timer_stop(&timer);
	return (unsigned long)delta;
}

void enableInterrupt(pin_size_t pinNumber) {
	struct gpio_port_callback *pcb = find_gpio_port_callback(arduino_pins[pinNumber].port);

	if (pcb) {
		pcb->handlers[arduino_pins[pinNumber].pin].enabled = true;
	}
}

void disableInterrupt(pin_size_t pinNumber) {
	struct gpio_port_callback *pcb = find_gpio_port_callback(arduino_pins[pinNumber].port);

	if (pcb) {
		pcb->handlers[arduino_pins[pinNumber].pin].enabled = false;
	}
}

void interrupts(void) {
	if (interrupts_disabled) {
		irq_unlock(irq_key);
		interrupts_disabled = false;
	}
}

void noInterrupts(void) {
	if (!interrupts_disabled) {
		irq_key = irq_lock();
		interrupts_disabled = true;
	}
}

int digitalPinToInterrupt(pin_size_t pin) {
	struct gpio_port_callback *pcb = find_gpio_port_callback(arduino_pins[pin].port);

	return (pcb) ? pin : -1;
}
