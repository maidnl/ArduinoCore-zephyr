/*
 * Copyright (c) Arduino s.r.l. and/or its affiliated companies
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/* This dance is necessary to force the inclusion of "Arduino_RouterBridge.h"
 * during the library discovery phase, so that the library include path is
 * added to the sketch build process.
 */

#if ARDUINO_LIBRARY_DISCOVERY_PHASE
#include "Arduino_RouterBridge.h"
#elif __has_include("Arduino_RouterBridge.h")
#include "Arduino_RouterBridge.h"
#endif
