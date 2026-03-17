/*
 * Copyright (c) Arduino s.r.l. and/or its affiliated companies
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

/* Map the surgical pinctrl indices to the Arduino ADC pins */
uint8_t arduino_adc_pinctrl_idx[] = {0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 8, 9, 10, 11};

uint8_t arduino_pwm_pinctrl_idx[] = {0, 0, 1, 0, 0, 0, 1, 1, 2, 2};

void _on_1200_bps() {
	uint32_t tmp = (uint32_t) & (RTC->BKP0R);
	tmp += (RTC_BKP_DR0 * 4U);
	*(__IO uint32_t *)tmp = (uint32_t)0xDF59;
	NVIC_SystemReset();
}
