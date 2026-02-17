/* Copyright (C) Arduino SRL (Daniele Aimo)
 *    SPDX-License-Identifier: MPL-2.0 */

#ifndef ARDUINO_ZEPHYR_PDM_IMPL_H
#define ARDUINO_ZEPHYR_PDM_IMPL_H

#include <zephyr/audio/dmic.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/kernel.h>

#define PDM_NUMBER_OF_SAMPLES 512

/* size in bit of an audio sample */
/* NANO 33 BLE will work only if this value is 16
 * GIGA can work up with 24 */
#define SAMPLE_BIT_WIDTH 16

#if defined(ARDUINO_NANO33BLE)
#define SLAB_BLOCK_NUM 4
#define SLAB_ALIGN     4
#if SAMPLE_BIT_WIDTH != 16
#error "Compilation runtime check: SAMPLE_BIT_WITDH must be set to 16 for ARDUINO_NANO33BLE"
#endif
#define SLAB_BLOCK_SIZE (PDM_NUMBER_OF_SAMPLES * 2)
/* SLAB configuration */
#elif defined(ARDUINO_GIGA)
#define SLAB_BLOCK_NUM 4
#define SLAB_ALIGN     32
#if SAMPLE_BIT_WIDTH == 16
#define SLAB_BLOCK_SIZE (PDM_NUMBER_OF_SAMPLES * 2)
#elif SAMPLE_BIT_WIDTH == 24
#define SLAB_BLOCK_SIZE (PDM_NUMBER_OF_SAMPLES * 4)
#else
#error "Compilation runtime check: SAMPLE_BIT_WIDTH must be 16 or 24 for ARDUINO_GIGA"
#endif
#endif

namespace arduino {

int pdm_read(void **buffer, size_t *size);
int pdm_configure(int channels, int sampleRate);
int pdm_start();
int pdm_stop();
void pdm_gain(int gain);

} // namespace arduino

#endif
