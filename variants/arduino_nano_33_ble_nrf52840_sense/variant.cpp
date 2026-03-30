/*
 * Copyright (c) Arduino s.r.l. and/or its affiliated companies
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "Arduino.h"
#include <hal/nrf_power.h>
#include <zephyr/dt-bindings/gpio/nordic-nrf-gpio.h>

void _on_1200_bps() {
	nrf_power_gpregret_set(NRF_POWER, 0, 0xb0);
	NVIC_SystemReset();
}

void initVariant(void) {
	static const struct gpio_dt_spec enable_sensors =
		GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), pin_enable_gpios);
	if (gpio_is_ready_dt(&enable_sensors)) {
		gpio_flags_t flags =
			enable_sensors.dt_flags | GPIO_OUTPUT_HIGH | GPIO_ACTIVE_HIGH | NRF_GPIO_DRIVE_H0H1;
		gpio_pin_configure(enable_sensors.port, enable_sensors.pin, flags);
		gpio_pin_set(enable_sensors.port, enable_sensors.pin, HIGH);
		delay(500);
	}
}

#include <zephyr/device.h>
#include <hal/nrf_gpio.h> // This header contains NRF_GPIO_PIN_MAP

// 1. Define your friendly names using the Nordic macro
#define P0_00 NRF_GPIO_PIN_MAP(0, 0)
#define P0_01 NRF_GPIO_PIN_MAP(0, 1)
#define P0_02 NRF_GPIO_PIN_MAP(0, 2)
#define P0_03 NRF_GPIO_PIN_MAP(0, 3)
#define P0_04 NRF_GPIO_PIN_MAP(0, 4)
#define P0_05 NRF_GPIO_PIN_MAP(0, 5)
#define P0_06 NRF_GPIO_PIN_MAP(0, 6)
#define P0_07 NRF_GPIO_PIN_MAP(0, 7)
#define P0_08 NRF_GPIO_PIN_MAP(0, 8)
#define P0_09 NRF_GPIO_PIN_MAP(0, 9)
#define P0_10 NRF_GPIO_PIN_MAP(0, 10)
#define P0_11 NRF_GPIO_PIN_MAP(0, 11)
#define P0_12 NRF_GPIO_PIN_MAP(0, 12)
#define P0_13 NRF_GPIO_PIN_MAP(0, 13)
#define P0_14 NRF_GPIO_PIN_MAP(0, 14)
#define P0_15 NRF_GPIO_PIN_MAP(0, 15)
#define P0_16 NRF_GPIO_PIN_MAP(0, 16)
#define P0_17 NRF_GPIO_PIN_MAP(0, 17)
#define P0_18 NRF_GPIO_PIN_MAP(0, 18)
#define P0_19 NRF_GPIO_PIN_MAP(0, 19)
#define P0_20 NRF_GPIO_PIN_MAP(0, 20)
#define P0_21 NRF_GPIO_PIN_MAP(0, 21)
#define P0_22 NRF_GPIO_PIN_MAP(0, 22)
#define P0_23 NRF_GPIO_PIN_MAP(0, 23)
#define P0_24 NRF_GPIO_PIN_MAP(0, 24)
#define P0_25 NRF_GPIO_PIN_MAP(0, 25)
#define P0_26 NRF_GPIO_PIN_MAP(0, 26)
#define P0_27 NRF_GPIO_PIN_MAP(0, 27)
#define P0_28 NRF_GPIO_PIN_MAP(0, 28)
#define P0_29 NRF_GPIO_PIN_MAP(0, 29)
#define P0_30 NRF_GPIO_PIN_MAP(0, 30)
#define P0_31 NRF_GPIO_PIN_MAP(0, 31)
#define P1_00 NRF_GPIO_PIN_MAP(1, 0)
#define P1_01 NRF_GPIO_PIN_MAP(1, 1)
#define P1_02 NRF_GPIO_PIN_MAP(1, 2)
#define P1_03 NRF_GPIO_PIN_MAP(1, 3)
#define P1_04 NRF_GPIO_PIN_MAP(1, 4)
#define P1_05 NRF_GPIO_PIN_MAP(1, 5)
#define P1_06 NRF_GPIO_PIN_MAP(1, 6)
#define P1_07 NRF_GPIO_PIN_MAP(1, 7)
#define P1_08 NRF_GPIO_PIN_MAP(1, 8)
#define P1_09 NRF_GPIO_PIN_MAP(1, 9)
#define P1_10 NRF_GPIO_PIN_MAP(1, 10)
#define P1_11 NRF_GPIO_PIN_MAP(1, 11)
#define P1_12 NRF_GPIO_PIN_MAP(1, 12)
#define P1_13 NRF_GPIO_PIN_MAP(1, 13)
#define P1_14 NRF_GPIO_PIN_MAP(1, 14)
#define P1_15 NRF_GPIO_PIN_MAP(1, 15)
#define P1_16 NRF_GPIO_PIN_MAP(1, 16)
#define P1_17 NRF_GPIO_PIN_MAP(1, 17)
#define P1_18 NRF_GPIO_PIN_MAP(1, 18)
#define P1_19 NRF_GPIO_PIN_MAP(1, 19)
#define P1_20 NRF_GPIO_PIN_MAP(1, 20)
#define P1_21 NRF_GPIO_PIN_MAP(1, 21)
#define P1_22 NRF_GPIO_PIN_MAP(1, 22)
#define P1_23 NRF_GPIO_PIN_MAP(1, 23)
#define P1_24 NRF_GPIO_PIN_MAP(1, 24)
#define P1_25 NRF_GPIO_PIN_MAP(1, 25)
#define P1_26 NRF_GPIO_PIN_MAP(1, 26)
#define P1_27 NRF_GPIO_PIN_MAP(1, 27)
#define P1_28 NRF_GPIO_PIN_MAP(1, 28)
#define P1_29 NRF_GPIO_PIN_MAP(1, 29)
#define P1_30 NRF_GPIO_PIN_MAP(1, 30)
#define P1_31 NRF_GPIO_PIN_MAP(1, 31)

typedef struct {
	uint32_t pin;
	const struct device *const *peripheral_devs;
	uint8_t ain_val; // 0 = Not Analog, 1 = AIN0, 2 = AIN1, etc.
} PinAllocation_t;

static const struct device *const pin_uart0_devices[] = {DEVICE_DT_GET(DT_NODELABEL(uart0)), NULL};
static const struct device *const pin_spi2_devices[] = {DEVICE_DT_GET(DT_NODELABEL(spi2)), NULL};
static const struct device *const pin_i2c0_devices[] = {DEVICE_DT_GET(DT_NODELABEL(i2c0)), NULL};

static const PinAllocation_t g_pin_map[] = {

	{P1_03, pin_uart0_devices, 0}, // D0/TX
	{P1_10, pin_uart0_devices, 0}, // D1/RX
	{P1_11, nullptr, 0},           // D2
	{P1_12, nullptr, 0},           // D3
	{P1_15, nullptr, 0},           // D4
	{P1_13, nullptr, 0},           // D5
	{P1_14, nullptr, 0},           // D6
	{P0_23, nullptr, 0},           // D7

	// D8 - D13
	{P0_21, nullptr, 0},          // D8
	{P0_27, nullptr, 0},          // D9
	{P1_02, nullptr, 0},          // D10
	{P1_01, pin_spi2_devices, 0}, // D11/MOSI
	{P1_08, pin_spi2_devices, 0}, // D12/MISO
	{P0_13, pin_spi2_devices, 0}, // D13/SCK/LED

	// A0 - A7
	{P0_04, nullptr, 3},          // A0
	{P0_05, nullptr, 4},          // A1
	{P0_30, nullptr, 7},          // A2
	{P0_29, nullptr, 6},          // A3
	{P0_31, pin_i2c0_devices, 8}, // A4/SDA
	{P0_02, pin_i2c0_devices, 1}, // A5/SCL
	{P0_28, nullptr, 5},          // A6
	{P0_03, nullptr, 2},          // A7

	// LEDs
	{P0_24, nullptr, 0}, // LED R
	{P0_16, nullptr, 0}, // LED G
	{P0_06, nullptr, 0}, // LED B
	{P1_09, nullptr, 0}, // LED PWR

	{P0_19, nullptr, 0}, // INT APDS

	// PDM
	{P0_17, nullptr, 0}, // PDM PWR
	{P0_26, nullptr, 0}, // PDM CLK
	{P0_25, nullptr, 0}, // PDM DIN

	// Internal I2C
	{P0_14, nullptr}, // SDA2
	{P0_15, nullptr}, // SCL2

	// Internal I2C
	{P1_00, nullptr, 0}, // I2C_PULL
	{P0_22, nullptr, 0}  // VDD_ENV_ENABLE
};

const size_t g_pin_table_size = sizeof(g_pin_map) / sizeof(g_pin_map[0]);

uint32_t get_absolute_nrf_pin(uint8_t arduino_pin) {
	// Basic bounds checking to prevent crashes
	if (arduino_pin >= (sizeof(g_pin_map) / sizeof(g_pin_map[0]))) {
		return 0xFFFFFFFF; // Return disconnected/invalid state
	}
	return g_pin_map[arduino_pin].pin;
}

#undef SDA
#undef SCL
#undef MOSI
#undef MISO
#undef SCK
#undef SS

#include <zephyr/kernel.h>
#include <hal/nrf_twim.h>
#include <hal/nrf_spim.h>
#include <hal/nrf_uarte.h>

/* Reminder about PSEL register in nordic microcontrollers
	Bits 0-4: The physical pin number
	Bit  5: The port number(0 or 1)
	Bits 6-30: Reserved(Always 0)
	Bit 31 : The Connection bit(0 = Connected, 1 = Disconnected)
*/
// Helper macro to check and disconnect a PSEL register if it matches our pin
#define CHECK_AND_DISCONNECT_PSEL(psel_reg, absolute_pin)                                          \
	do {                                                                                           \
		if ((uint32_t)psel_reg == absolute_pin) {                                                  \
			psel_reg = 0xFFFFFFFF; /* 0xFFFFFFFF is the disconnected state */                      \
		}                                                                                          \
	} while (0)

/*
 * This function checks the hardware registers of common peripherals.
 * If it finds that a peripheral is using our target pin, it severs the connection.
 */
void nrf_hardware_disconnect_pin(uint32_t absolute_nrf_pin) {

#ifdef NRF_TWIM0
	CHECK_AND_DISCONNECT_PSEL(NRF_TWIM0->PSEL.SCL, absolute_nrf_pin);
	CHECK_AND_DISCONNECT_PSEL(NRF_TWIM0->PSEL.SDA, absolute_nrf_pin);
#endif
#ifdef NRF_TWIM1
	CHECK_AND_DISCONNECT_PSEL(NRF_TWIM1->PSEL.SCL, absolute_nrf_pin);
	CHECK_AND_DISCONNECT_PSEL(NRF_TWIM1->PSEL.SDA, absolute_nrf_pin);
#endif

#ifdef NRF_SPIM0
	CHECK_AND_DISCONNECT_PSEL(NRF_SPIM0->PSEL.SCK, absolute_nrf_pin);
	CHECK_AND_DISCONNECT_PSEL(NRF_SPIM0->PSEL.MOSI, absolute_nrf_pin);
	CHECK_AND_DISCONNECT_PSEL(NRF_SPIM0->PSEL.MISO, absolute_nrf_pin);
#endif
#ifdef NRF_SPIM1
	CHECK_AND_DISCONNECT_PSEL(NRF_SPIM1->PSEL.SCK, absolute_nrf_pin);
	CHECK_AND_DISCONNECT_PSEL(NRF_SPIM1->PSEL.MOSI, absolute_nrf_pin);
	CHECK_AND_DISCONNECT_PSEL(NRF_SPIM1->PSEL.MISO, absolute_nrf_pin);
#endif

#ifdef NRF_UARTE0
	CHECK_AND_DISCONNECT_PSEL(NRF_UARTE0->PSEL.TXD, absolute_nrf_pin);
	CHECK_AND_DISCONNECT_PSEL(NRF_UARTE0->PSEL.RXD, absolute_nrf_pin);
#endif
#ifdef NRF_UARTE1
	CHECK_AND_DISCONNECT_PSEL(NRF_UARTE1->PSEL.TXD, absolute_nrf_pin);
	CHECK_AND_DISCONNECT_PSEL(NRF_UARTE1->PSEL.RXD, absolute_nrf_pin);
#endif
#ifdef NRF_PWM0
	for (int i = 0; i < 4; i++) {
		CHECK_AND_DISCONNECT_PSEL(NRF_PWM0->PSEL.OUT[i], absolute_nrf_pin);
	}
#endif
#ifdef NRF_PWM1
	for (int i = 0; i < 4; i++) {
		CHECK_AND_DISCONNECT_PSEL(NRF_PWM1->PSEL.OUT[i], absolute_nrf_pin);
	}
#endif
#ifdef NRF_PWM2
	for (int i = 0; i < 4; i++) {
		CHECK_AND_DISCONNECT_PSEL(NRF_PWM2->PSEL.OUT[i], absolute_nrf_pin);
	}
#endif

#ifdef NRF_GPIOTE
	for (int i = 0; i < 8; i++) {
		// The pin number is stored in bits 8-12, and the port is in bit 13.
		// Shifting right by 8 and masking with 0x3F (which is 0011 1111 in binary)
		// extracts the exact absolute pin number!
		uint32_t config_reg = NRF_GPIOTE->CONFIG[i];
		uint32_t gpiote_pin = (config_reg >> 8) & 0x3F;

		// If this channel is using our pin, and the channel is actually enabled (bit 0)
		if ((gpiote_pin == absolute_nrf_pin) && ((config_reg & 0x03) != 0)) {
			// Write 0 to completely disable this GPIOTE channel
			NRF_GPIOTE->CONFIG[i] = 0;
		}
	}
#endif
#ifdef NRF_SAADC
	uint32_t ain_val = 0;

	for (size_t i = 0; i < g_pin_table_size; i++) {
		if (g_pin_map[i].pin == absolute_nrf_pin) {
			ain_val = g_pin_map[i].ain_val;
			break;
		}
	}

	if (ain_val != 0) {
		for (int i = 0; i < 8; i++) {
			if (NRF_SAADC->CH[i].PSELP == ain_val) {
				NRF_SAADC->CH[i].PSELP = 0;
			}
			if (NRF_SAADC->CH[i].PSELN == ain_val) {
				NRF_SAADC->CH[i].PSELN = 0;
			}
		}
	}
#endif
}

void nrf_hardware_disconnect_device_pins(const struct device *dev) {
	if (dev == NULL) {
		return;
	}
	for (size_t i = 0; i < g_pin_table_size; i++) {
		const struct device *const *dev_array = g_pin_map[i].peripheral_devs;
		if (dev_array != NULL) {
			for (size_t j = 0; dev_array[j] != NULL; j++) {
				if (dev_array[j] == dev) {
					uint32_t abs_pin = g_pin_map[i].pin;
					nrf_hardware_disconnect_pin(abs_pin);
				}
			}
		}
	}
}

