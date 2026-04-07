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

int init_dev_apply_pinctrl(const struct device *dev);

#ifdef __cplusplus
} // extern "C"
#endif
