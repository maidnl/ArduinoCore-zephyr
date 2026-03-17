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
bool begin_device(const struct device *dev, int16_t pin_sub_idx = -1);
void end_device(const struct device *dev);
#ifdef __cplusplus
} // extern "C"
#endif
