/*
 * Copyright (c) 2024 Ayush Singh <ayush@beagleboard.org>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

void enableInterrupt(pin_size_t);
void disableInterrupt(pin_size_t);
/* Generic device power and initialization management */
bool begin_device(const struct device *dev, const struct pinctrl_dev_config *pcfg = nullptr);
void end_device(const struct device *dev, const struct pinctrl_dev_config *pcfg = nullptr);
#ifdef __cplusplus
} // extern "C"
#endif
