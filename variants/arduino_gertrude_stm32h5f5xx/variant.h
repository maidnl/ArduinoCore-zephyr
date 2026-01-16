/*
 * Copyright (c) 2022 Dhruva Gole
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// TODO: correctly handle these legacy defines
#define MOSI    0
#define MISO    0
#define SCK     0
#define SS      0
#define SDA     0
#define SCL     0

#define AR_DEFAULT          0
#define AR_INTERNAL2V5      1
#define AR_INTERNAL2V05     2
#define AR_INTERNAL1V5      3
#define AR_INTERNAL1V8      4
#define AR_EXTERNAL         5
#define AR_INTERNAL         AR_INTERNAL2V5

// RGB LEDs Pin Map
#define LED3_R DIGITAL_PIN_GPIOS_FIND_NODE(DT_NODELABEL(red_led_0))
#define LED3_G DIGITAL_PIN_GPIOS_FIND_NODE(DT_NODELABEL(green_led_0))
#define LED3_B DIGITAL_PIN_GPIOS_FIND_NODE(DT_NODELABEL(blue_led_0))

#define LED4_R DIGITAL_PIN_GPIOS_FIND_NODE(DT_NODELABEL(red_led_1))
#define LED4_G DIGITAL_PIN_GPIOS_FIND_NODE(DT_NODELABEL(green_led_1))
#define LED4_B DIGITAL_PIN_GPIOS_FIND_NODE(DT_NODELABEL(blue_led_1))