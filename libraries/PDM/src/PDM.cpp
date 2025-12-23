#include "PDM.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <zephyr/kernel.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/gpio.h>

/* ---- CONFIGURATION ----- */

/* enable disable library debug */
//#define PDM_DEBUG_ENABLED
/* NOTE: quite obvious to note that enabling the debug flag make any sample 
 * obtained from the microphone via serial "dirty" because the debug log is
 * inserted into the stream obtained from the microphone. So enable it only if 
 * the data from the mic are unimportant */

/* SLAB configuration */
#define SLAB_BLOCK_SIZE       DEFAULT_PDM_BUFFER_SIZE
#define SLAB_BLOCK_NUM        4
#define SLAB_ALIGN            4
/* THREAD configuration */
#define PDM_THREAD_STACK_SIZE  1024
#define PDM_THREAD_PRIORITY    7
/* mic power regulator configuration */
/* DT_NODELABEL(mic_pwr) gets the node ID.
 * DT_NODE_HAS_STATUS(..., okay) returns 1 if status is "okay", 0 otherwise. */
#define MIC_PWR_NODE DT_NODELABEL(mic_pwr)
#if DT_NODE_EXISTS(MIC_PWR_NODE) && DT_NODE_HAS_STATUS(MIC_PWR_NODE, okay)
    static const struct device *mic_regulator = DEVICE_DT_GET(MIC_PWR_NODE);
    #define MIC_PWR_PRESENT
#endif

/* ---- static local variables ---- */

static struct k_mem_slab pdm_slab;
static uint8_t __aligned(4) pdm_slab_buffer[SLAB_BLOCK_SIZE * SLAB_BLOCK_NUM];
static struct k_mutex pdm_mutex;
static void pdm_thread(void *, void *, void *);
static void (*_onReceive)(void) = NULL;
static PDMDoubleBuffer *pdm_db = nullptr;
/* the PDM mic zephyr device */
static const struct device *const dmic_dev = DEVICE_DT_GET(DT_NODELABEL(dmic_dev));

/* ---- MIC RECEIVING THREAD ---- */

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
	
	if (ret < 0) {
		#ifdef PDM_DEBUG_ENABLED
		Serial.print("[ERR]: dmic_read failed with err = ");
		Serial.println(ret);
		#endif
		continue;
	}

	if(pdm_db != nullptr) {
		#ifdef PDM_DEBUG_ENABLED
		Serial.print("[LOG]: receiving ");
		Serial.print(size);
		Serial.println(" bytes from mic");
		#endif

		k_mutex_lock(&pdm_mutex, K_FOREVER);

		if (pdm_db->available() == 0) {
			#ifdef PDM_DEBUG_ENABLED
			if(size > pdm_db->availableForWrite()) {
				Serial.println("[WRG]: mic data loss!");
			}
			#endif
			memcpy(pdm_db->data(), buffer, pdm_db->availableForWrite());
			pdm_db->swap(pdm_db->availableForWrite());

 			k_mutex_unlock(&pdm_mutex);
			/* notify the user data are ready */
			if (_onReceive) {
				_onReceive();
			}
		}
		else {
  			k_mutex_unlock(&pdm_mutex);
		}
	}
	/* always free slab for next round */
	k_mem_slab_free(&pdm_slab, buffer);
    }
}

/* ---- PDM CLASS ---- */

/* ______________________________________________________________constructor */
PDMClass::PDMClass() : active(false),  pdm_init(false) {}

/* _______________________________________________________________destructor */
PDMClass::~PDMClass() {}

/* __________________________________________________________________begin() */
int PDMClass::begin(int channels, int sampleRate) {
	/* 
	 * +++++++++ INITIALISATIONs and CHECKs +++++++++++++
	 */
	/* --- SLAB and MUTEX INITIALIZATION --- */
	/* To be performed just once */
	if(!pdm_init) {
		int err = k_mem_slab_init(&pdm_slab, pdm_slab_buffer, SLAB_BLOCK_SIZE, SLAB_BLOCK_NUM);
    		if (err != 0) {
			#ifdef PDM_DEBUG_ENABLED
        		Serial.println("[ERR]: slab mic initialization failed with code: " + String(err));
        		#endif
			return 0; /* failed slab initialization */
    		}
		err = k_mutex_init(&pdm_mutex);
    		if (err != 0) {
			#ifdef PDM_DEBUG_ENABLED
        		Serial.println("[ERR]: mutex mic initialization failed with code: " + String(err));
        		#endif
			return 0; /* failed mutex initialization */
    		}
		pdm_init = true;
	}
	/* --- verify digital microphone is ready --- */
	if (!device_is_ready(dmic_dev)) {
		#ifdef PDM_DEBUG_ENABLED
		Serial.print("[WRN]: PDM " + String(dmic_dev->name) + " is not ready");
		#endif
		return 0; /* mic device not ready */
	}
	/* --- check on channels --- */
	if(channels < 1 || channels > 2) {
		#ifdef PDM_DEBUG_ENABLED
		Serial.print("[ERR]: PDM unsupported number of channels");
		#endif
		return 0; /* wrong number of channels */
	}
	/* --- check on sampleRate --- */
	if( !(sampleRate == 16000 || sampleRate == 41667) ) {
		#ifdef PDM_DEBUG_ENABLED
		Serial.print("[ERR]: PDM unsupported sample rate");
		#endif
		return 0; /* sample rate not supported */
	}
	/* assing the pointer used by the thread to the "internal" double buffer*/
	pdm_db = &db;
	/*
	 * +++++++++++ Set up PDM configuration +++++++++++++++
	 */
	stream.pcm_width = SAMPLE_BIT_WIDTH;
	stream.mem_slab  = &pdm_slab;

	cfg.io.min_pdm_clk_freq = 1000000;
	cfg.io.max_pdm_clk_freq = 3500000;
	cfg.io.min_pdm_clk_dc   = 40;
	cfg.io.max_pdm_clk_dc   = 60;

	cfg.streams = &stream;
	cfg.channel.req_num_streams = 1;
	
	if(channels == 1) {
		cfg.channel.req_num_chan = 1;
		cfg.channel.req_chan_map_lo = dmic_build_channel_map(0, 0, PDM_CHAN_LEFT);
		cfg.streams[0].pcm_rate = sampleRate;
		cfg.streams[0].block_size = SLAB_BLOCK_SIZE;
	} else {
		/* 2 channels */
		/* [TODO]: Configuration not verified on real hw */
		cfg.channel.req_num_chan = 2;
		cfg.channel.req_chan_map_lo =
				dmic_build_channel_map(0, 0, PDM_CHAN_LEFT) |
				dmic_build_channel_map(1, 0, PDM_CHAN_RIGHT);
		cfg.streams[0].pcm_rate = sampleRate;
		cfg.streams[0].block_size = SLAB_BLOCK_SIZE;
	}

	/*
	 * +++++++++++ Start MIC listening +++++++++++++++
	 */

	if(!active) {
		/* --- Send mic configuration to driver --- */
		if(dmic_configure(dmic_dev, &cfg) < 0) {
			#ifdef PDM_DEBUG_ENABLED
			Serial.println("[ERR]: mic configuration failed");
			#endif
			return 0;
		}
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
			/* [TODO]:
			 * - does the mic do not use regulator? 
			 * - does the mic is always powered up?*/
			return 0;
		}
		#endif
		/* --- start the microphone --- */
		#ifdef PDM_DEBUG_ENABLED
		Serial.println("[LOG]: mic start listening");
		#endif
		if (dmic_trigger(dmic_dev, DMIC_TRIGGER_START) < 0) {
			#ifdef PDM_DEBUG_ENABLED
			Serial.println("[ERR]: mic START trigger failed");
			#endif
			return 0;
		}
		/* --- resume receiving thread --- */
		#ifdef PDM_DEBUG_ENABLED
		Serial.println("[LOG]: mic rx thread starting");
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
		Serial.println("[LOG]: mic stop listening");
		#endif
		if (dmic_trigger(dmic_dev, DMIC_TRIGGER_STOP) < 0) {
			#ifdef PDM_DEBUG_ENABLED
			Serial.println("[ERR]: mic STOP trigger failed");
			#endif
		}
		/* --- stop receiving thread --- */
		#ifdef PDM_DEBUG_ENABLED
		Serial.println("[LOG]: mic rx thread suspended");
		#endif
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
  k_mutex_lock(&pdm_mutex, K_FOREVER);
  size_t avail = db.available();
  k_mutex_unlock(&pdm_mutex);
  return (int)avail;
}

/* ___________________________________________________________________read() */
int PDMClass::read(void* buffer, size_t size) {
  k_mutex_lock(&pdm_mutex, K_FOREVER);
  int read = db.read(buffer, size);
  k_mutex_unlock(&pdm_mutex);
  return read;
}

/* ______________________________________________________________onReceive() */
void PDMClass::onReceive(void(* func)(void)) {
  _onReceive = func;
}

#include <hal/nrf_pdm.h>
/* ________________________________________________________________setGain() */
void PDMClass::setGain(int gain) {
	/* at the present the zephyr dmic_nrfx_pdm.c does not support the set
	 * of the gain (gain_l and gain_r are defined in the nrf HAL but not
	 * used by the driver which use a default value) */
	NRF_PDM->GAINR = gain;
	NRF_PDM->GAINL = gain;
}

/* __________________________________________________________setBufferSize() */
size_t PDMClass::getBufferSize() {
	return db.getSize();

}

PDMClass PDM;
