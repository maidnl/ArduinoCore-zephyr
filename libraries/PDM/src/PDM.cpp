#include "PDM.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <zephyr/kernel.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/gpio.h>
/* --------------
 * CONFIGURATION 
 * ------------- */

#define PDM_DEBUG_ENABLED

/* --------------------------------------------------------------------------
 * Define a slab used by pdm stream
 * ------------------------------------------------------------------------- */

#define SLAB_BLOCK_SIZE       DEFAULT_PDM_BUFFER_SIZE
#define SLAB_BLOCK_NUM        4
#define SLAB_ALIGN            4
//K_MEM_SLAB_DEFINE(name, slab_block_size, slab_num_blocks, slab_align)
//K_MEM_SLAB_DEFINE_STATIC(pdm_slab, SLAB_BLOCK_SIZE, SLAB_BLOCK_NUM, SLAB_ALIGN);

struct k_mem_slab pdm_slab;
static uint8_t __aligned(4) pdm_slab_buffer[SLAB_BLOCK_SIZE * SLAB_BLOCK_NUM];

/* ---------------------------------------------------------------------------
 * Define a mutex to deal with double buffer and threads
 * --------------------------------------------------------------------------*/

//K_MUTEX_DEFINE(pdm_mutex);
struct k_mutex pdm_mutex;
/* ---------------------------------------------------------------------------
 * Define a Thread to "simulate" irq behavior as expected by PDM API
 * --------------------------------------------------------------------------*/

#define PDM_THREAD_STACK_SIZE  4096
#define PDM_THREAD_PRIORITY    7
/* PDM receiving thread */
static void pdm_thread(void *, void *, void *);
/* data available callback function */
static void (*_onReceive)(void) = NULL;
static PDMDoubleBuffer *pdm_db = nullptr;
/* the PDM mic zephyr device */
static const struct device *const dmic_dev = DEVICE_DT_GET(DT_NODELABEL(dmic_dev));

K_THREAD_DEFINE(pdm_tid, PDM_THREAD_STACK_SIZE, pdm_thread, NULL, NULL, NULL,
                PDM_THREAD_PRIORITY, 0, 0);

void pdm_thread(void *, void *, void *) {
 
    void *buffer;
    uint32_t size;
 
    k_thread_suspend(pdm_tid);

    while (true) {

	#ifdef PDM_DEBUG_ENABLED
	Serial.println("[LOG]: Call -> dmic_read");
	#endif
	
	int ret = dmic_read(dmic_dev, 0, &buffer, &size, SYS_FOREVER_MS);
	
	#ifdef PDM_DEBUG_ENABLED
	if (ret < 0) {
		Serial.print("[ERR]: dmic_read failed with err = ");
		Serial.println(ret);
	}
	#endif

	if(pdm_db != nullptr && ret == 0) {
		#ifdef PDM_DEBUG_ENABLED
		Serial.print("[LOG]: Microphone receiving ");
		Serial.print(size);
		Serial.println(" bytes of data");
		#endif
		k_mutex_lock(&pdm_mutex, K_FOREVER);
		if (pdm_db->available() == 0) {
			#ifdef PDM_DEBUG_ENABLED
			if(size > pdm_db->availableForWrite()) {
				Serial.println("[WRG]: Microphone possible lose of data");
			}
			#endif
			memcpy(pdm_db->data(), buffer, pdm_db->availableForWrite());
			pdm_db->swap(pdm_db->availableForWrite());

 			k_mutex_unlock(&pdm_mutex);
			// call receive callback if provided
			if (_onReceive) {
				_onReceive();
			}
		}
		else {
  			k_mutex_unlock(&pdm_mutex);
		}
		/* remember to free slab for next round */
		k_mem_slab_free(&pdm_slab, buffer);
	}
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

/* --- CONSTRUCTOR --- */
PDMClass::PDMClass() : active(false),  slab_init(false) {}

/* --- DESTRUCTOR --- */
PDMClass::~PDMClass() {}
/* --- PUBLIC FUNCTIONS --- */

int PDMClass::begin(int channels, int sampleRate) {

	#ifdef PDM_DEBUG_ENABLED
	Serial.println("[LOG]: Buffer Size: " + String(SLAB_BLOCK_SIZE));
        Serial.println("Bit Width: " + String(SAMPLE_BIT_WIDTH));
	#endif

	/* --- SLAB INITIALIZATION --- */
	if(!slab_init) {
		int slab_err = k_mem_slab_init(&pdm_slab, pdm_slab_buffer, SLAB_BLOCK_SIZE, SLAB_BLOCK_NUM);
    		if (slab_err != 0) {
			#ifdef PDM_DEBUG_ENABLED
        		Serial.println("[ERR]: Slab init failed with code: " + String(slab_err));
        		#endif
			return 0;
    		}
		/* --- DEBUG: Test Slab Allocation --- */
		#ifdef PDM_DEBUG_ENABLED
		void* test_block;
		if (k_mem_slab_alloc(&pdm_slab, &test_block, K_NO_WAIT) == 0) {
			Serial.println("[DIAG]: Slab allocation SUCCESS. Block Addr: " + String((uint32_t)test_block, HEX));
			k_mem_slab_free(&pdm_slab, test_block); // Free it immediately
		} else {
			Serial.println("[DIAG]: Slab allocation FAILED! (CRITICAL)");
			return 0;
		}
		#endif
		k_mutex_init(&pdm_mutex);
		slab_init = true;
	}
	/* assing the pointer used by the thread to the "internal" double buffer*/
	pdm_db = &db;
	
	/* --- verify digital microphone is ready --- */
	if (!device_is_ready(dmic_dev)) {
		#ifdef PDM_DEBUG_ENABLED
		Serial.print("[WRN]: PDM " + String(dmic_dev->name) + " is not ready");
		#endif
		return 0;
	}

	/* --- check on channels --- */
	if(channels < 1 || channels > 2) {
		#ifdef PDM_DEBUG_ENABLED
		Serial.print("[ERR]: PDM unsupported number of channels");
		#endif
		return 0; // Unsupported number of channels
	}

	/* --- check on sampleRate --- */
	if( !(sampleRate == 16000 || sampleRate == 41667) ) {
		#ifdef PDM_DEBUG_ENABLED
		Serial.print("[ERR]: PDM unsupported sample rate");
		#endif
		return 0; // Unsupported sampleRate
	}

	/* --- Set up PDM configuration --- */

	stream.pcm_width = SAMPLE_BIT_WIDTH;
	stream.mem_slab  = &pdm_slab;

	cfg.io.min_pdm_clk_freq = 1000000;
	cfg.io.max_pdm_clk_freq = 1100000;
	cfg.io.min_pdm_clk_dc   = 40;
	cfg.io.max_pdm_clk_dc   = 60;

	cfg.streams = &stream;
	cfg.channel.req_num_streams = 1;

	cfg.channel.req_num_chan = 1;
	cfg.channel.req_chan_map_lo = dmic_build_channel_map(0, 0, PDM_CHAN_LEFT);
	cfg.streams[0].pcm_rate = sampleRate;
	cfg.streams[0].block_size = SLAB_BLOCK_SIZE;


	if(!active) {
		/* --- Send configuration to driver --- */
		if(dmic_configure(dmic_dev, &cfg) < 0) {
			#ifdef PDM_DEBUG_ENABLED
			Serial.println("[ERR]: Microphone driver configuration failed");
			#endif
			return 0;
		}

		/* --- give microphone power --- */
		/* --- give microphone power (MANUAL OVERRIDE) --- */
		const struct device *gpio0_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));

		if (device_is_ready(gpio0_dev)) {
		// Force P0.17 HIGH (Standard Mic Power Pin for Nano 33 BLE Sense)
		// Note: If you are on a very specific custom revision, verify if it's pin 17.
		int ret = gpio_pin_configure(gpio0_dev, 17, GPIO_OUTPUT_ACTIVE);
		if (ret < 0) {
		Serial.println("[ERR]: Failed to force Mic Power Pin 17");
		} else {
		Serial.println("[LOG]: Forced Mic Power Pin 17 HIGH");
		k_msleep(20); // Vital delay for Mic startup
		}
		} else {
		Serial.println("[ERR]: GPIO0 Device not ready");
		}

		/* Old regulator code - commented out for debugging
#ifdef MIC_PWR_PRESENT
		if (device_is_ready(mic_regulator)) {
		regulator_enable(mic_regulator); 
		} 
#endif
		*/
		#ifdef MIC_PWR_PRESENT_ERASED
		if (device_is_ready(mic_regulator)) {
			#ifdef PDM_DEBUG_ENABLED
			Serial.println("[LOG]: mic regulator enabled");
			#endif
			regulator_enable(mic_regulator);
			k_msleep(15);
	 	} else {
			return 0;
		}
		#endif
		/* --- start the microphone --- */
		#ifdef PDM_DEBUG_ENABLED
		Serial.println("[LOG]: Microphone start");
		#endif
		if (dmic_trigger(dmic_dev, DMIC_TRIGGER_START) < 0) {
			#ifdef PDM_DEBUG_ENABLED
			Serial.println("[ERR]: Microphone START trigger failed");
			#endif
			return 0;
		}
		/* --- resume receiving thread --- */
		#ifdef PDM_DEBUG_ENABLED
		Serial.println("[LOG]: Microphone receiving thread starting");
		#endif
		k_thread_resume(pdm_tid);
		/* --- Set the status as ACTIVE --- */
		active = true;
	}
	return 1;
}

void PDMClass::end() {
	if(active) {
		/* --- stop the microphone --- */
		#ifdef PDM_DEBUG_ENABLED
		Serial.println("[LOG]: Microphone stop");
		#endif
		if (dmic_trigger(dmic_dev, DMIC_TRIGGER_STOP) < 0) {
			#ifdef PDM_DEBUG_ENABLED
			Serial.println("[ERR]: Microphone STOP trigger failed");
			#endif
		}
		/* --- stop receiving thread --- */
		#ifdef PDM_DEBUG_ENABLED
		Serial.println("[LOG]: Microphone receiving thread stopping");
		#endif
		k_thread_suspend(pdm_tid);

		/* HANDLE PWR PIN (IF PRESENT) */
		#ifdef MIC_PWR_PRESENT
		regulator_disable(mic_regulator);
		#endif
		/* --- Set the status as INACTIVE */
		active = false;
	}
}

/* ______________________________________________________________available() */
int PDMClass::available() {
  k_mutex_lock(&pdm_mutex, K_FOREVER);
  size_t avail = db.available();
  k_mutex_unlock(&pdm_mutex);
  Serial.println("avail");

  return (int)avail;
}

/* ___________________________________________________________________read() */
int PDMClass::read(void* buffer, size_t size) {
  k_mutex_lock(&pdm_mutex, K_FOREVER);
  int read = db.read(buffer, size);
  k_mutex_unlock(&pdm_mutex);
  Serial.println("read");
  return read;
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
