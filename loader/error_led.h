/*
 * Copyright (c) Arduino s.r.l. and/or its affiliated companies
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZARD_LOADER_ERROR_LED_H
#define ZARD_LOADER_ERROR_LED_H

/*
 * Start blinking the error LED after a recoverable loader failure (the sketch
 * could not be loaded or started) and return immediately.
 */
void recoverable_error_led(void);

#endif /* ZARD_LOADER_ERROR_LED_H */
