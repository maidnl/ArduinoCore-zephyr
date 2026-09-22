/*
 * Copyright (c) Arduino s.r.l. and/or its affiliated companies
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "zephyr/sys/printk.h"
#include <stdint.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sketch);

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/llext/llext.h>
#include <zephyr/llext/buf_loader.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/logging/log_ctrl.h>

#include <stdlib.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/uart/cdc_acm.h>
#include <zephyr/drivers/uart.h>

#include <zephyr/devicetree/fixed-partitions.h>

#define HEADER_LEN 16

struct sketch_header_v1 {
	uint8_t ver;    // @ 0x07
	uint32_t len;   // @ 0x08
	uint16_t magic; // @ 0x0c
	uint8_t flags;  // @ 0x0e
} __attribute__((packed));

#define SKETCH_FLAG_DEBUG        0x01
#define SKETCH_FLAG_LINKED       0x02
#define SKETCH_FLAG_IMMEDIATE    0x04
#define SKETCH_FLAG_WAIT_FOR_APP 0x08

#define SKETCH_RAM_BUFFER_LEN 131072

#define MCU_BOOT_HEADER_OFFSET 32

/* Need to replicate logic from zephyrSerial.h to avoid C++ here */
#define ZARD_BOARD_HAS_SERIALUSB                                                                   \
	DT_NODE_HAS_PROP(DT_PATH(zephyr_user), cdc_acm_serial) && CONFIG_USBD_CDC_ACM_CLASS
#define ZARD_FIRST_SERIAL_IS_SERIALUSB                                                             \
	ZARD_BOARD_HAS_SERIALUSB && !(DT_NODE_HAS_PROP(DT_PATH(zephyr_user), arduino_router_serial))

#ifdef CONFIG_BOARD_ARDUINO_MEZZA
struct dynamic_dfu_data {
	const struct device *flash_dev;
	uint32_t sketch_offset;
	uint32_t sketch_addr;
	uint32_t erase_size;
	uint32_t block_num;
	uint32_t current_offset;
};

static struct dynamic_dfu_data dfu_state = {
											.sketch_addr = 0,
											.sketch_offset = 0,
											.erase_size = 0,
											.block_num = 0,
											.current_offset = 0};
#endif

#if ZARD_FIRST_SERIAL_IS_SERIALUSB
const struct device *const usb_dev =
	DEVICE_DT_GET(DT_PHANDLE_BY_IDX(DT_PATH(zephyr_user), cdc_acm_serial, 0));

#include <zephyr/usb/usbd.h>
struct usbd_context *usbd_init_device(usbd_msg_cb_t msg_cb);
static struct usbd_context *_usbd = NULL;

int usbd_config_set(struct usbd_context *uds_ctx, uint8_t new_cfg);

int loader_usb_disable() {
	int err = usbd_disable(_usbd);
	if (err) {
		// at least reset the configuration
		usbd_config_set(_usbd, 0);
	}
	usbd_shutdown(_usbd);
	return err;
}

static void loader_usb_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *msg) {
	if (usbd_can_detect_vbus(ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			usbd_enable(ctx);
		}
	}
}

int loader_usb_enable(void) {
	int err;
	_usbd = usbd_init_device(loader_usb_msg_cb);
	if (_usbd == NULL) {
		return -ENODEV;
	}
	if (!usbd_can_detect_vbus(_usbd)) {
		err = usbd_enable(_usbd);
		if (err) {
			return err;
		}
	}
	return 0;
}

#if CONFIG_SHELL
static int enable_shell_usb(void) {
	bool log_backend = CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL > 0;
	uint32_t level = (CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL > LOG_LEVEL_DBG) ?
						 CONFIG_LOG_MAX_LEVEL :
						 CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL;
	static const struct shell_backend_config_flags cfg_flags = SHELL_DEFAULT_BACKEND_CONFIG_FLAGS;

	shell_init(shell_backend_uart_get_ptr(), usb_dev, cfg_flags, log_backend, level);

	return 0;
}
#endif
#endif

#ifdef CONFIG_USERSPACE
K_THREAD_STACK_DEFINE(llext_stack, CONFIG_MAIN_STACK_SIZE);
struct k_thread llext_thread;

void llext_entry(void *arg0, void *arg1, void *arg2) {
	void (*fn)(struct llext_loader *, struct llext *) = arg0;
	fn(arg1, arg2);
}
#endif /* CONFIG_USERSPACE */

/* Export Flash parameters for use by core building scripts */
#ifdef CONFIG_BOARD_ARDUINO_MEZZA
/* TODO : the _sketch_max_size is used in a improper way !!! */
__attribute__((retain)) const uintptr_t sketch_max_size =
	DT_REG_SIZE(DT_NODELABEL(slot1_partition));
#else
__attribute__((retain)) const uintptr_t sketch_base_addr =
	DT_PARTITION_ADDR(DT_NODELABEL(user_sketch));
__attribute__((retain)) const uintptr_t sketch_max_size = DT_REG_SIZE(DT_NODELABEL(user_sketch));
#endif

/* Determine maximum size of the loader application */
#if DT_HAS_PARTITION_LABEL(image_0) /* "image_0" partition size */
#define LOADER_MAX_SIZE DT_REG_SIZE(DT_NODE_BY_PARTITION_LABEL(image_0))
#elif CONFIG_FLASH_LOAD_SIZE > 0 /* forced value from Kconfig */
#define LOADER_MAX_SIZE CONFIG_FLASH_LOAD_SIZE
#elif CONFIG_FLASH_USES_MAPPED_PARTITION /* size of the mapped code partition */
#define LOADER_MAX_SIZE DT_REG_SIZE(DT_CHOSEN(zephyr_code_partition))
#elif CONFIG_FLASH_LOAD_OFFSET /* heuristic: size of Flash minus load offset */
#if DT_NODE_EXISTS(DT_NODELABEL(code_flash))
#define LOADER_MAX_SIZE (DT_REG_SIZE(DT_NODELABEL(code_flash)) - CONFIG_FLASH_LOAD_OFFSET)
#else
#define LOADER_MAX_SIZE (DT_REG_SIZE(DT_NODELABEL(flash0)) - CONFIG_FLASH_LOAD_OFFSET)
#endif
#else /* default: size of whole Flash */
#if DT_NODE_EXISTS(DT_NODELABEL(code_flash))
#define LOADER_MAX_SIZE DT_REG_SIZE(DT_NODELABEL(code_flash))
#else
#define LOADER_MAX_SIZE DT_REG_SIZE(DT_NODELABEL(flash0))
#endif
#endif
__attribute__((retain)) const uintptr_t loader_max_size = LOADER_MAX_SIZE;

struct backup_store {
	uint32_t wait_for_app_magic;
};
extern volatile __stm32_backup_sram_section struct backup_store backup;

static int loader(const struct shell *sh) {
	const struct flash_area *fa;
	int rc;

	/* Test that attempting to open a disabled flash area fails */
#ifdef CONFIG_BOARD_ARDUINO_MEZZA
	rc = flash_area_open(PARTITION_ID(slot0_partition), &fa);
#else
	rc = flash_area_open(PARTITION_ID(user_sketch), &fa);
#endif
	if (rc) {
		printk("Failed to open flash area, rc %d\n", rc);
		return rc;
	}

#ifdef CONFIG_BOARD_ARDUINO_MEZZA
	uintptr_t base_addr = DT_PARTITION_ADDR(DT_NODELABEL(slot0_partition));
	base_addr += dfu_state.sketch_offset;
	dfu_state.sketch_addr = base_addr;
	printk(">>> SKETCH OFFSET = 0x%08X\n", base_addr);
#else
	uintptr_t base_addr = DT_PARTITION_ADDR(DT_NODELABEL(user_sketch));
#endif

	char header[HEADER_LEN];
#ifdef CONFIG_BOARD_ARDUINO_MEZZA
	rc = flash_area_read(fa, dfu_state.sketch_offset, header, sizeof(header));
#else
	rc = flash_area_read(fa, 0, header, sizeof(header));
#endif
	if (rc) {
		printk("Failed to read header, rc %d\n", rc);
		return rc;
	}

	bool sketch_valid = true;
	struct sketch_header_v1 *sketch_hdr = (struct sketch_header_v1 *)(header + 7);
	if (sketch_hdr->ver != 0x1 || sketch_hdr->magic != 0x2341) {
		printk("Invalid sketch header\n");
		sketch_valid = false;
		// This is not a valid sketch, but try to start a shell anyway
	} else {
		printk("??????????????????????????\n");
	}

		printk("AAAA\n");
#if ZARD_FIRST_SERIAL_IS_SERIALUSB
		printk("BBBB\n");
	int debug = (!sketch_valid) || (sketch_hdr->flags & SKETCH_FLAG_DEBUG);
#if CONFIG_SHELL
	if (strcmp(k_thread_name_get(k_current_get()), "main") == 0) {
		// disables default shell on UART
		shell_uninit(shell_backend_uart_get_ptr(), NULL);
		// enables USB and starts the shell
		loader_usb_enable();
		int dtr;
		do {
			// wait for the serial port to open
			uart_line_ctrl_get(usb_dev, UART_LINE_CTRL_DTR, &dtr);
			k_sleep(K_MSEC(100));
		} while (!dtr);
		enable_shell_usb();
	}
#elif CONFIG_LOG
#if !CONFIG_USB_DEVICE_INITIALIZE_AT_BOOT
	if (debug) {
		loader_usb_enable();
	}
#endif
	for (int i = 0; i < log_backend_count_get(); i++) {
		const struct log_backend *backend;
		backend = log_backend_get(i);
		log_backend_init(backend);
		log_backend_enable(backend, backend->cb->ctx, CONFIG_LOG_DEFAULT_LEVEL);
		if (!debug) {
			break;
		}
	}
		printk("CCCC\n");
#endif
#endif

#if defined(CONFIG_BOARD_ARDUINO_UNO_Q) || defined(CONFIG_BOARD_ARDUINO_VENTUNO_Q)
		printk("DDDD\n");
	void matrixBegin(void);
	void matrixEnd(void);
	void matrixPlay(const uint8_t *buf, uint32_t len);
	void matrixSetGrayscaleBits(uint8_t _max);
	void matrixGrayscaleWrite(uint8_t *buf);
#include "bootanimation.h"
#include "usbanimation.h"

	uint8_t *_bootanimation = (uint8_t *)bootanimation;
	size_t _bootanimation_len = bootanimation_len;
	uint8_t *_bootanimation_end = (uint8_t *)bootanimation_end;
	size_t _bootanimation_end_len = bootanimation_end_len;

	__attribute__((packed)) struct bootanimation_user_data {
		size_t magic; // must be 0xBA for bootanimation
		size_t len_loop;
		size_t len_end;
		size_t empty;
		char buf_loop;
	};

	backup.wait_for_app_magic = 0;

	uintptr_t bootanimation_addr = DT_REG_ADDR(DT_GPARENT(DT_NODELABEL(bootanimation))) +
								   DT_REG_ADDR(DT_NODELABEL(bootanimation));

	struct bootanimation_user_data *user_bootanimation =
		(struct bootanimation_user_data *)bootanimation_addr;
	if (user_bootanimation->magic == 0xBA) {
		_bootanimation = &(user_bootanimation->buf_loop);
		_bootanimation_len = user_bootanimation->len_loop;
		_bootanimation_end_len = user_bootanimation->len_end;
		_bootanimation_end = _bootanimation + user_bootanimation->len_loop;
	}

	if ((!sketch_valid) || !(sketch_hdr->flags & SKETCH_FLAG_IMMEDIATE)) {
		// Start the bootanimation while waiting for the MPU to boot

		const struct gpio_dt_spec mpu_booted =
			GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), control_gpios, 0);
		gpio_pin_configure_dt(&mpu_booted, GPIO_INPUT | GPIO_PULL_DOWN);
		const struct gpio_dt_spec usb_mode =
			GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), control_gpios, 1);
		gpio_pin_configure_dt(&usb_mode, GPIO_INPUT | GPIO_PULL_UP);

		k_sleep(K_MSEC(400));

		if (gpio_pin_get_dt(&usb_mode) == 0) {
			// USB mode, skip the animation
			matrixBegin();
			matrixSetGrayscaleBits(8);
			while (1) {
				matrixPlay(usbanimation_raw, usbanimation_raw_len);
				k_sleep(K_MSEC(10));
			}
		}

		if (gpio_pin_get_dt(&mpu_booted) == 0) {
			matrixBegin();
			matrixSetGrayscaleBits(8);
			while (gpio_pin_get_dt(&mpu_booted) == 0) {
				matrixPlay(_bootanimation, _bootanimation_len);
			}
			matrixPlay(_bootanimation_end, _bootanimation_end_len);
			uint8_t _framebuffer[104] = {0};
			matrixGrayscaleWrite(_framebuffer);
			k_sleep(K_MSEC(10));
			matrixEnd();
		}

		if (sketch_hdr->flags & SKETCH_FLAG_WAIT_FOR_APP) {
			while (backup.wait_for_app_magic == 0) {
				k_sleep(K_MSEC(100));
			}
		}
	}
		printk("EEEE\n");
#endif

	size_t sketch_buf_len = sketch_hdr->len;
	printk("SKETCH LEN = %d\n", sketch_buf_len);

	if (sketch_hdr->flags & SKETCH_FLAG_LINKED) {
#ifdef CONFIG_BOARD_ARDUINO_PORTENTA_C33
#if CONFIG_MPU
		barrier_dmem_fence_full();
#endif
#if CONFIG_DCACHE
		barrier_dsync_fence_full();
#endif
#if CONFIG_ICACHE
		barrier_isync_fence_full();
#endif
#endif

#if ZARD_FIRST_SERIAL_IS_SERIALUSB
		if (debug) {
			// Disable USB before jumping to sketch
			loader_usb_disable();
		}
#endif

		printk("GGGGG\n");
		extern struct k_heap llext_heap;
		typedef void (*entry_point_t)(struct k_heap *heap, size_t heap_size);
		entry_point_t entry_point = (entry_point_t)(base_addr + HEADER_LEN + 1);
		entry_point(&llext_heap, llext_heap.heap.init_bytes);
		// should never reach here
		for (;;) {
			k_sleep(K_FOREVER);
		}
	}

		printk("HHHHH\n");
#if defined(CONFIG_LLEXT_STORAGE_WRITABLE)
		printk("IIIII\n");
	uint8_t *sketch_buf = k_aligned_alloc(4096, sketch_buf_len);

	if (!sketch_buf) {
		printk("Unable to allocate %d bytes\n", sketch_buf_len);
		return -ENOMEM;
	}

	rc = flash_area_read(fa, 0, sketch_buf, sketch_buf_len);
	if (rc) {
		printk("Failed to read sketch area, rc %d\n", rc);
		return rc;
	}
		printk("LLLLL\n");
#else
	// Assuming the sketch is stored in the same flash device as the loader
		printk("MMMMM\n");
	uint8_t *sketch_buf = (uint8_t *)base_addr;
#endif

#ifdef CONFIG_LLEXT
		printk("NNNN sketch_buf_len = %i\n", sketch_buf_len);
	struct llext_buf_loader buf_loader = LLEXT_BUF_LOADER(sketch_buf, sketch_buf_len);
	struct llext_loader *ldr = &buf_loader.loader;

		printk("M (1)\n");
	LOG_HEXDUMP_DBG(sketch_buf, 4, "4 byte MAGIC");

		printk("M (2)\n");
	struct llext_load_param ldr_parm = LLEXT_LOAD_PARAM_DEFAULT;
		printk("M (3)\n");
	struct llext *ext;
	int res;

	res = llext_load(ldr, "sketch", &ext, &ldr_parm);
		printk("M (4)\n");
	if (res) {
		printk("Failed to load sketch, rc %d\n", res);
		return res;
	}
		printk("M (5)\n");

	void (*main_fn)() = llext_find_sym(&ext->exp_tab, "main");
	if (!main_fn) {
		printk("Failed to find main function\n");
		return -ENOENT;
	} else {
		printk("MAIN FUNCTION FOUND!!!!\n");
	}
#endif

#ifdef CONFIG_USERSPACE
	/*
	 * Due to the number of MPU regions on some parts with MPU (USERSPACE)
	 * enabled we need to always call into the extension from a new dedicated
	 * thread to avoid running out of MPU regions on some parts.
	 *
	 * This is part dependent behavior and certainly on MMU capable parts
	 * this should not be needed! This test however is here to be generic
	 * across as many parts as possible.
	 */
	struct k_mem_domain domain;

	k_mem_domain_init(&domain, 0, NULL);

#ifdef Z_LIBC_PARTITION_EXISTS
	k_mem_domain_add_partition(&domain, &z_libc_partition);
#endif

	res = llext_add_domain(ext, &domain);
	if (res == -ENOSPC) {
		printk("Too many memory partitions for this particular hardware\n");
		return -1;
	}
	printk("CREATE THREAD!!\n");
	k_thread_create(&llext_thread, llext_stack, K_THREAD_STACK_SIZEOF(llext_stack), &llext_entry,
					llext_bootstrap, ext, main_fn, 1, K_INHERIT_PERMS, K_FOREVER);

	k_mem_domain_add_thread(&domain, &llext_thread);

	k_thread_start(&llext_thread);
	k_thread_join(&llext_thread, K_FOREVER);
#else

#if ZARD_FIRST_SERIAL_IS_SERIALUSB
	if (debug) {
		// Disable USB before jumping to sketch
		loader_usb_disable();
	}
#endif

#ifdef CONFIG_LLEXT
	llext_bootstrap(ext, main_fn, NULL);
#endif

#endif

	return 0;
}

#if CONFIG_SHELL
SHELL_CMD_REGISTER(sketch, NULL, "Run sketch", loader);
#endif

#ifdef CONFIG_BOARD_ARDUINO_MEZZA

#include <zephyr/retention/retention.h>
#include <zephyr/retention/bootmode.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/drivers/pwm.h>
static const struct pwm_dt_spec pwm_led = PWM_DT_SPEC_GET(DT_ALIAS(fade_led));

#define FADE_DELAY_MS          10
#define FADE_STEPS             50
#define FADE_THREAD_STACK_SIZE 1024

K_THREAD_STACK_DEFINE(fade_led_stack, FADE_THREAD_STACK_SIZE);
static struct k_thread fade_led_thread;
static atomic_t fade_led_running;

static void blink_fade_led(void *arg0, void *arg1, void *arg2) {
	uint32_t pulse_width = 0;
	bool increasing = true;

	ARG_UNUSED(arg0);
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);

	if (!pwm_is_ready_dt(&pwm_led)) {
		LOG_ERR("Error: PWM device %s is not ready", pwm_led.dev->name);
		return;
	}

	uint32_t step_size = pwm_led.period / FADE_STEPS;

	while (atomic_get(&fade_led_running)) {
		int ret = pwm_set_pulse_dt(&pwm_led, pulse_width);
		if (ret < 0) {
			LOG_ERR("Error %d: failed to set pulse width", ret);
			break;
		}

		if (increasing) {
			pulse_width += step_size;

			if (pulse_width >= pwm_led.period) {
				pulse_width = pwm_led.period;
				increasing = false;
			}
		} else {
			if (pulse_width <= step_size) {
				pulse_width = 0;
				increasing = true;
			} else {
				pulse_width -= step_size;
			}
		}
		k_msleep(FADE_DELAY_MS);
	}

	int ret = pwm_set_pulse_dt(&pwm_led, 0);
	if (ret < 0) {
		LOG_ERR("Error %d: failed to turn off LED", ret);
	}
}

static bool check_boot_mode() {
	bool rv = false;
	int boot_mode = bootmode_check(BOOT_MODE_TYPE_BOOTLOADER);
	if (boot_mode < 0) {
		LOG_ERR("ERROR: Unable to read boot mode (%d)\n", boot_mode);
	}
	if (boot_mode == 1) {
		rv = true;
	}
	if (bootmode_clear()) {
		printk("ERROR: Unable to clear boot mode\n");
	}
	return rv;
}

#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_dfu.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/dfu/flash_img.h>

struct usbd_dfu_flash_data {
	struct flash_img_context fi_ctx;
	uint32_t last_block;
	const uint8_t id;

	union {
		uint32_t uploaded;
		uint32_t downloaded;
	};
};

static int dfu_flash_read(void *const priv, const uint32_t block, const uint16_t size,
			   uint8_t buf[static CONFIG_USBD_DFU_TRANSFER_SIZE]) {
	struct usbd_dfu_flash_data *const data = priv;
	const struct flash_area *fa;
	uint32_t to_upload;
	int len;
	int ret;

	if (size == 0) {
		return 0;
	}

	if (block == 0) {
		data->last_block = 0;
		data->uploaded = 0;
	} else if (data->last_block + 1U != block) {
		return -EINVAL;
	}

	ret = flash_area_open(data->id, &fa);
	if (ret) {
		return ret;
	}

	to_upload = fa->fa_size - data->uploaded;
	len = (to_upload < size) ? (int)to_upload : (int)size;

	ret = flash_area_read(fa, data->uploaded, buf, len);
	flash_area_close(fa);
	if (ret) {
		return ret;
	}

	data->last_block = block;
	data->uploaded += size;

	return len;
}

static int dfu_flash_write(void *const priv, const uint32_t block, const uint16_t size,
			    const uint8_t buf[static CONFIG_USBD_DFU_TRANSFER_SIZE]) {
	struct usbd_dfu_flash_data *const data = priv;
	const bool flush = (size == 0);
	int ret;

	if (block == 0) {
		if (flash_img_init_id(&data->fi_ctx, data->id)) {
			return -EINVAL;
		}

		data->last_block = 0;
		data->downloaded = 0;

		if (size == 0) {
			/* Nothing to download */
			return 0;
		}
	} else if (data->last_block + 1U != block) {
		return -EINVAL;
	}

	ret = flash_img_buffered_write(&data->fi_ctx, buf, size, flush);
	if (ret) {
		return ret;
	}

	data->last_block = block;
	data->downloaded += size;

	return 0;
}

/* Loader update image: writes to the MCUboot secondary slot (slot1_partition) */
static bool slot1_next(void *priv, enum usb_dfu_state state, enum usb_dfu_state next) {
	ARG_UNUSED(priv);

	if (state == DFU_MANIFEST_SYNC && next == DFU_IDLE) {
		LOG_INF("Loader update download finished, requesting MCUboot upgrade");
		if (IS_ENABLED(CONFIG_BOOTLOADER_MCUBOOT)) {
			boot_request_upgrade(false);
		}
	}

	return true;
}

static struct usbd_dfu_flash_data slot1_data = {
	.id = PARTITION_ID(slot1_partition),
};

USBD_DFU_DEFINE_IMG(all_image, "complete_image", &slot1_data, dfu_flash_read, dfu_flash_write,
					slot1_next);

/* ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */
/* ---------------------- DYNAMIC DFU ALTERNATE ----------------------------- */
/* ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */



static int dynamic_flash_write(void *const priv, const uint32_t block, const uint16_t size,
							   const uint8_t buf[static CONFIG_USBD_DFU_TRANSFER_SIZE]) {
	struct dynamic_dfu_data *const data = priv;
	int err;

	if (size == 0) {
		return 0; /* Nothing to write */
	}

	/* Block 0 indicates the start of a new DFU transfer */
	if (block == 0) {
		if (data->flash_dev == NULL || !device_is_ready(data->flash_dev)) {
			LOG_ERR("Flash device not configured");
			return -ENODEV;
		}

		if (data->sketch_addr == 0 || data->erase_size == 0 || data->block_num == 0) {
			LOG_ERR("Flash parameter not set");
			return -EINVAL;
		}

		data->current_offset = 0;

		LOG_INF("Erase header (1) 0x%08x bytes at 0x%08x", data->erase_size, 0);
		/* TODO */
		printk("Erase header (1) 0x%08x bytes at 0x%08x", data->erase_size, 0);

		/* Erase the target region before writing */
		err = flash_erase(data->flash_dev, 
						      DT_PARTITION_ADDR(DT_NODELABEL(slot0_partition)), 
								data->erase_size);
		if (err) {
			LOG_ERR("Flash erase header failed (%d)", err);
			return err;
		}

		LOG_INF("Starting DFU transfer. Erase sketch (2) 0x%08x bytes at 0x%08x",
				data->erase_size * data->block_num, data->sketch_addr);
		/* TODO */
		printk("Starting DFU transfer. Erase sketch (2) 0x%08x bytes at 0x%08x",
			   data->erase_size * data->block_num, data->sketch_addr);

		/* Erase the target region before writing */
		err = flash_erase(data->flash_dev, data->sketch_addr, data->erase_size * data->block_num);
		if (err) {
			LOG_ERR("Flash erase sketch failed (%d)", err);
			return err;
		}
	}

	data->current_offset = 0;
	uint32_t chunk_offset = 0;
	uint32_t remaining_size = size;

	while (remaining_size > 0) {
		uint32_t write_addr;
		uint32_t write_size;

		/* writing the HEADER */
		if (data->current_offset < data->erase_size) {

			write_addr = data->current_offset;
			write_size = MIN(remaining_size, data->erase_size - data->current_offset);
		} else {
			/* We are writing to the dynamic region */
			uint32_t dynamic_offset = data->current_offset - data->erase_size;
			write_addr = data->sketch_addr + dynamic_offset;
			write_size = remaining_size;
		}

		LOG_INF("Wrote %u bytes to 0x%08x", write_size, write_addr);

		printk("Wrote %u bytes to 0x%08x", write_size, write_addr);
		err = flash_write(data->flash_dev, write_addr, &buf[chunk_offset], write_size);
		if (err) {
			LOG_ERR("Flash write failed at 0x%08x", write_addr);
			return err;
		}

		data->current_offset += write_size;
		chunk_offset += write_size;
		remaining_size -= write_size;
	}

	return 0;
}

USBD_DFU_DEFINE_IMG(sketch_image, "sketch", &dfu_state, NULL, dynamic_flash_write, slot1_next);

/* +++ DFU USB CONFIGURATION and FUNCTIONS +++ */

USBD_DEVICE_DEFINE(dfu_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), CONFIG_USB_DEVICE_VID,
				   CONFIG_USB_DEVICE_PID + 0x0100);

USBD_DESC_LANG_DEFINE(sample_lang);
USBD_DESC_CONFIG_DEFINE(fs_cfg_desc, "DFU FS Configuration");
USBD_DESC_CONFIG_DEFINE(hs_cfg_desc, "DFU HS Configuration");

USBD_DESC_MANUFACTURER_DEFINE(sample_mfr, "Arduino");
USBD_DESC_PRODUCT_DEFINE(sample_product, "Arduino Mezza DFU");

static const uint8_t dfu_attributes = USB_SCD_SELF_POWERED | USB_SCD_REMOTE_WAKEUP;

USBD_CONFIGURATION_DEFINE(sample_fs_config, dfu_attributes, 100, &fs_cfg_desc);
USBD_CONFIGURATION_DEFINE(sample_hs_config, dfu_attributes, 100, &hs_cfg_desc);

K_SEM_DEFINE(dfu_update_sem, 0, 1);

static void dfu_msg_cb(struct usbd_context *const usbd_ctx, const struct usbd_msg *const msg) {
	LOG_INF("USBD message: %s", usbd_msg_type_string(msg->type));

	if (usbd_can_detect_vbus(usbd_ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			usbd_enable(usbd_ctx);
		}
		if (msg->type == USBD_MSG_VBUS_REMOVED) {
			usbd_disable(usbd_ctx);
		}
	}

	if (msg->type == USBD_MSG_DFU_DOWNLOAD_COMPLETED) {
		LOG_INF("DFU firmware download completed");

		/* Unlock the thread waiting for the update */
		k_sem_give(&dfu_update_sem);
	}
}

/* blocking function - it waits until a dfu update is performed */
static void dfu_update(void) {
	int err;

	LOG_INF("Initializing USB DFU mode...");

	err = usbd_add_descriptor(&dfu_usbd, &sample_lang);
	if (err) {
		LOG_ERR("Failed to initialize language descriptor (%d)", err);
		return;
	}

	err = usbd_add_descriptor(&dfu_usbd, &sample_mfr);
	if (err) {
		LOG_ERR("Failed to initialize manufacturer descriptor (%d)", err);
		return;
	}

	err = usbd_add_descriptor(&dfu_usbd, &sample_product);
	if (err) {
		LOG_ERR("Failed to initialize product descriptor (%d)", err);
		return;
	}

	if (usbd_caps_speed(&dfu_usbd) == USBD_SPEED_HS) {
		usbd_add_configuration(&dfu_usbd, USBD_SPEED_HS, &sample_hs_config);
		usbd_register_class(&dfu_usbd, "dfu_dfu", USBD_SPEED_HS, 1);
		usbd_device_set_code_triple(&dfu_usbd, USBD_SPEED_HS, 0, 0, 0);
	}

	err = usbd_add_configuration(&dfu_usbd, USBD_SPEED_FS, &sample_fs_config);
	if (err) {
		LOG_ERR("Failed to add Full-Speed configuration");
		return;
	}

	err = usbd_register_class(&dfu_usbd, "dfu_dfu", USBD_SPEED_FS, 1);
	if (err) {
		LOG_ERR("Failed to add register classes");
		return;
	}

	usbd_device_set_code_triple(&dfu_usbd, USBD_SPEED_FS, 0, 0, 0);

	err = usbd_init(&dfu_usbd);
	if (err) {
		LOG_ERR("Failed to initialize USB device support");
		return;
	}

	err = usbd_msg_register_cb(&dfu_usbd, dfu_msg_cb);
	if (err) {
		LOG_ERR("Failed to register message callback");
		return;
	}

	err = usbd_enable(&dfu_usbd);
	if (err) {
		LOG_ERR("Failed to enable USB device support");
		return;
	}

	LOG_INF("USB initialized. Waiting indefinitely for DFU update...");

	k_sem_take(&dfu_update_sem, K_FOREVER);

	LOG_INF("DFU operation fulfilled. Deinitializing USB...");

	usbd_disable(&dfu_usbd);
	usbd_shutdown(&dfu_usbd);
}

void retrieve_flash_info() {
	const struct flash_area *fa;
	int rc;
	uint32_t value = 0;
	dfu_state.sketch_offset = 0;
	dfu_state.erase_size = 0;
	dfu_state.block_num = 0;

	dfu_state.flash_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller));
	
	rc = flash_area_open(PARTITION_ID(slot0_partition), &fa);
	if (rc) {
		printk("Failed to open flash area, rc %d\n", rc);
	}

	rc = flash_area_read(fa, MCU_BOOT_HEADER_OFFSET, &value, sizeof(value));
	if (rc) {
		printk("Failed to read sketch_address, rc %d\n", rc);
	} else {
		dfu_state.sketch_offset = value;
	}

	rc = flash_area_read(fa, MCU_BOOT_HEADER_OFFSET + 4, &value, sizeof(value));
	if (rc) {
		printk("Failed to read eraze size, rc %d\n", rc);
	} else {
		dfu_state.erase_size = value;
	}

	rc = flash_area_read(fa, MCU_BOOT_HEADER_OFFSET + 8, &value, sizeof(value));
	if (rc) {
		printk("Failed to read block num, rc %d\n", rc);
	} else {
		dfu_state.block_num = value;
	}

	printk("+++++++++ SKETCH OFFSET: 0x%08X\n", dfu_state.sketch_addr);
	printk("+++++++++ ERASE SIZE: 0x%08X\n", dfu_state.erase_size);
	printk("+++++++++ BLOCK NUM: 0x%08X\n", dfu_state.block_num);
}

#endif

int main(void) {
#ifdef CONFIG_BOARD_ARDUINO_MEZZA
	retrieve_flash_info();
	if (check_boot_mode()) {
		atomic_set(&fade_led_running, 1);
		k_thread_create(&fade_led_thread, fade_led_stack, K_THREAD_STACK_SIZEOF(fade_led_stack),
						blink_fade_led, NULL, NULL, NULL, 1, 0, K_NO_WAIT);
		/* waits for dfu update */
		dfu_update();
		atomic_clear(&fade_led_running);
		k_thread_join(&fade_led_thread, K_FOREVER);
	}

#endif
	loader(NULL);
	return 0;
}
