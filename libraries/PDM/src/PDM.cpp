/* Copyright (C) Arduino SRL (Daniele Aimo)
 * SPDX-License-Identifier: MPL-2.0 */

#include "PDM.h"
#include "utility/PDM_impl.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

/* ---- CONFIGURATION ----- */

/* enable disable library debug */
// #define PDM_DEBUG_ENABLED
/* NOTE: quite obvious to note that enabling the debug flag make any sample
 * obtained from the microphone via serial "dirty" because the debug log is
 * inserted into the stream obtained from the microphone. So enable it only if
 * the data from the mic are unimportant */

/* SLAB configuration */
/* THREAD configuration */
#define PDM_THREAD_STACK_SIZE 1024
#define PDM_THREAD_PRIORITY   7
/* mic power regulator configuration */
/* DT_NODELABEL(mic_pwr) gets the node ID.
 * DT_NODE_HAS_STATUS(..., okay) returns 1 if status is "okay", 0 otherwise. */
#define MIC_PWR_NODE          DT_NODELABEL(mic_pwr)
#if DT_NODE_EXISTS(MIC_PWR_NODE) && DT_NODE_HAS_STATUS(MIC_PWR_NODE, okay)
static const struct device *mic_regulator = DEVICE_DT_GET(MIC_PWR_NODE);
#define MIC_PWR_PRESENT
#endif

/* ---- static local variables ---- */
static struct k_msgq pdm_rx_msgq;
static char __aligned(4) pdm_msgq_buffer[SLAB_BLOCK_NUM * sizeof(void *)];

struct k_mem_slab pdm_slab;
static uint8_t __aligned(32) pdm_slab_buffer[SLAB_BLOCK_SIZE * SLAB_BLOCK_NUM];

static struct k_thread pdm_thread_data;
K_THREAD_STACK_DEFINE(pdm_thread_stack, PDM_THREAD_STACK_SIZE);
static k_tid_t pdm_tid;

static void (*_onReceive)(void) = NULL;

/* ---- MIC RECEIVING THREAD ---- */
void pdm_thread(void *, void *, void *) {
	void *buffer;
	uint32_t size;

	while (true) {
		int ret = arduino::pdm_read(&buffer, &size);
		if (ret < 0) {
			continue;
		}

		if (k_msgq_put(&pdm_rx_msgq, &buffer, K_NO_WAIT) == 0) {
			if (_onReceive) {
				// Loop to handle leftovers without causing stack recursion
				int last_avail = PDM.available();
				while (last_avail > 0) {
					_onReceive(); // Trigger user callback

					int current_avail = PDM.available();
					// Safety catch: If the user didn't read anything, break the loop
					// to prevent the OS thread from hanging infinitely.
					if (current_avail == last_avail) {
						break;
					}
					last_avail = current_avail;
				}
			}
		} else {
			// Queue is full, drop the frame
			k_mem_slab_free(&pdm_slab, buffer);
		}
	}
}

/* ---- PDM CLASS ---- */

/* ______________________________________________________________constructor */
PDMClass::PDMClass() : pdm_init(false), active(false), active_block{nullptr, 0, 0} {
}

/* _______________________________________________________________destructor */
PDMClass::~PDMClass() {
}

/* __________________________________________________________________begin() */
int PDMClass::begin(int channels, int sampleRate) {

	/* --- SLAB INITIALIZATION (to be performed once) --- */
	if (!pdm_init) {
		int err = k_mem_slab_init(&pdm_slab, pdm_slab_buffer, SLAB_BLOCK_SIZE, SLAB_BLOCK_NUM);
		if (err != 0) {
			return 0; /* failed slab initialization */
		}
		k_msgq_init(&pdm_rx_msgq, pdm_msgq_buffer, sizeof(void *), SLAB_BLOCK_NUM);

		pdm_tid = k_thread_create(&pdm_thread_data, pdm_thread_stack,
								  K_THREAD_STACK_SIZEOF(pdm_thread_stack), pdm_thread, NULL, NULL,
								  NULL, PDM_THREAD_PRIORITY, 0, K_FOREVER);
		pdm_init = true;
	}

	/* --- Start MIC listening --- */

	if (!active) {
		/* --- give microphone power --- */
#ifdef MIC_PWR_PRESENT
		if (device_is_ready(mic_regulator)) {
#ifdef PDM_DEBUG_ENABLED
			Serial.println("[LOG]: mic regulator enabled");
#endif
			regulator_enable(mic_regulator);
			/* give little time regulator */
			k_msleep(15);
		} else {
			/* [TODO]: suppose the microphone is always powered */
		}
#endif
		/* configure the microphone */
		if (arduino::pdm_configure(channels, sampleRate) < 0) {
			return 0;
		}
		/* --- start the microphone --- */
		if (arduino::pdm_start() < 0) {
			return 0;
		}
		/* --- start or resume receiving thread --- */
		static bool thread_started = false;
		if (!thread_started) {
			// First boot: explicitly start the thread
			k_thread_start(pdm_tid);
			thread_started = true;
		} else {
			// Subsequent boots: just resume it
			k_thread_resume(pdm_tid);
		}
		/* --- Set the status as ACTIVE --- */
		active = true;
	}
	return 1;
}

void PDMClass::end() {
	if (active) {
		/* --- stop the microphone --- */
		arduino::pdm_stop();
		/* --- stop receiving thread --- */
		k_thread_suspend(pdm_tid);
		/* --- shut down mic regulator --- */
#ifdef MIC_PWR_PRESENT
		regulator_disable(mic_regulator);
#endif

		/* --- Set the status as INACTIVE */
		active = false;
	}
}

/* ______________________________________________________________available() */
int PDMClass::available() {
	int avail = 0;
	// Bytes remaining in the block currently being read
	if (active_block.data != nullptr) {
		avail += (active_block.size - active_block.offset);
	}
	// Bytes waiting in the queue
	avail += k_msgq_num_used_get(&pdm_rx_msgq) * SLAB_BLOCK_SIZE;
	return avail;
}

/* ___________________________________________________________________read() */
int PDMClass::read(void *user_buffer, size_t size) {
	size_t bytes_read = 0;
	uint8_t *dst = (uint8_t *)user_buffer;

	while (bytes_read < size) {
		// If we don't have an active block, grab the next one from the queue
		if (active_block.data == nullptr) {
			void *new_block = nullptr;
			if (k_msgq_get(&pdm_rx_msgq, &new_block, K_NO_WAIT) == 0) {
				active_block.data = new_block;
				active_block.size = SLAB_BLOCK_SIZE;
				active_block.offset = 0;
			} else {
				break; // No more data available
			}
		}

		// Calculate how much we can copy from the current block
		size_t avail_in_block = active_block.size - active_block.offset;
		size_t to_copy = size - bytes_read;
		if (to_copy > avail_in_block) {
			to_copy = avail_in_block;
		}

		// Copy directly from slab to sketch
		memcpy(dst + bytes_read, (uint8_t *)active_block.data + active_block.offset, to_copy);

		bytes_read += to_copy;
		active_block.offset += to_copy;

		// Once the block is fully consumed, free it back to the Zephyr pool
		if (active_block.offset >= active_block.size) {
			k_mem_slab_free(&pdm_slab, active_block.data);
			active_block.data = nullptr;
		}
	}
	return bytes_read;
}

/* ______________________________________________________________onReceive() */
void PDMClass::onReceive(void (*func)(void)) {
	_onReceive = func;
}

/* ________________________________________________________________setGain() */
void PDMClass::setGain(int gain) {
	arduino::pdm_gain(gain);
}

/* __________________________________________________________setBufferSize() */
size_t PDMClass::getBufferSize() {
	return SLAB_BLOCK_SIZE;
}

PDMClass PDM;
