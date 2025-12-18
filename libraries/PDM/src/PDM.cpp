#include "PDM.h"
#include <cstddef>
#include <cstdint>

#include <zephyr/kernel.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/drivers/regulator.h>

/* --------------
 * CONFIGURATION 
 * ------------- */

#define PDM_DEBUG_ENABLED

/* ---------------------------------------------------------------------------
 * Define a mutex to deal with double buffer and threads
 * --------------------------------------------------------------------------*/

K_MUTEX_DEFINE(pdm_mutex);

/* ---------------------------------------------------------------------------
 * Define a Thread to "simulate" irq behavior as expected by PDM API
 * --------------------------------------------------------------------------*/

#define PDM_THREAD_STACK_SIZE  1024
#define PDM_THREAD_PRIORITY    7

static void pdm_thread(void *, void *, void *);

K_THREAD_DEFINE(pdm_tid, PDM_THREAD_STACK_SIZE, pdm_thread, NULL, NULL, NULL,
                PDM_THREAD_PRIORITY, 0, 0);

void pdm_thread(void *, void *, void *) {
  
    pinMode(LED_BUILTIN, OUTPUT);

    /* suspend immediately the thread, until begin is not called */
    k_thread_suspend(pdm_tid);

    while (true) {
        digitalWrite(LED_BUILTIN, HIGH);
        k_msleep(200);

        digitalWrite(LED_BUILTIN, LOW);
        k_msleep(200);
    }
}

/* ----------------------------------------------------------------------------
 * Define MACROS to understand if a power regulator is defined in DT for the
 * microphone
 * ------------------------------------------------------------------------- */

/* DT_NODELABEL(mic_pwr) gets the node ID.
 * DT_NODE_HAS_STATUS(..., okay) returns 1 if status is "okay", 0 otherwise. */
#define MIC_PWR_NODE DT_NODELABEL(mic_pwr)

#if DT_NODE_EXISTS(MIC_PWR_NODE) && DT_NODE_HAS_STATUS(MIC_PWR_NODE, okay)
    static const struct device *mic_regulator = DEVICE_DT_GET(MIC_PWR_NODE);
    #define MIC_PWR_PRESENT
#endif

/* ----------------------------------------------------------------------------
 * PDM CLASS
 * ------------------------------------------------------------------------- */

/* data available callback function */
static void (*_onReceive)(void) = NULL;


/* the PDM mic zephyr device */
static const struct device *const dmic_dev = DEVICE_DT_GET(DT_NODELABEL(dmic_dev));

/* --- CONSTRUCTORs --- */
PDMClass::PDMClass() : active(false) {}
PDMClass::PDMClass(int pwrPin) : _pwrPin(pwrPin) {}

/* --- DESTRUCTOR --- */
PDMClass::~PDMClass() {}

/* --- PUBLIC FUNCTIONS --- */

int PDMClass::begin(int channels, int sampleRate) {
	
	//#ifdef pippo	
	if (!device_is_ready(dmic_dev)) {
		#ifdef PDM_DEBUG_ENABLED
		Serial.print("[WRN]: PDM " + String(dmic_dev->name) + " is not ready");
		#endif
		return 0;
	}
	//#endif






	if(!active) {
		/* HANDLE PWR PIN (IF PRESENT) */
		#ifdef MIC_PWR_PRESENT
		if (device_is_ready(mic_regulator)) {
			#ifdef PDM_DEBUG_ENABLED
			Serial.println("[LOG]: mic regulator enabled");
			#endif
			regulator_enable(mic_regulator);
        	} else {
			return 0;
		}
		#endif





			#ifdef PDM_DEBUG_ENABLED
		Serial.println("[LOG]: TH STARTING...");
			#endif
		k_thread_resume(pdm_tid);
		active = true;
	}

	
	


	(void)channels;
   (void)sampleRate;
   return 1;

}

void PDMClass::end() {
	if(active) {
		#ifdef PDM_DEBUG_ENABLED
		Serial.println("[LOG]: TH STOPPING...");
		#endif
		k_thread_suspend(pdm_tid);
		active = false;
		/* HANDLE PWR PIN (IF PRESENT) */
		#ifdef MIC_PWR_PRESENT
		regulator_disable(mic_regulator);
		#endif
	}
}

/* ______________________________________________________________available() */
int PDMClass::available() {
  k_mutex_lock(&pdm_mutex, K_FOREVER);
  size_t avail = db.available();
  k_mutex_unlock(&pdm_mutex);
  return (int)avail;
}

int PDMClass::read(void* buffer, size_t size) {
	(void)buffer;
	(void)size;
	return 1;
}

void PDMClass::onReceive(void(* func)(void)) {
  _onReceive = func;
}

void PDMClass::setGain(int gain) {
   (void)gain;
}

size_t PDMClass::getBufferSize() {
	return 1;

}
