/*
 * I2S Camera Capture — Implementation
 *
 * Captures JPEG frames from OV2640 via ESP32 I2S0 in camera mode.
 *
 * Architecture:
 * - I2S0 in LCD/camera slave RX mode
 * - Camera signals (D0-D7, VSYNC, HREF, PCLK) routed via GPIO matrix
 * - DMA ring of SRAM bounce buffers (ESP32 cannot DMA to PSRAM)
 * - VSYNC GPIO interrupt for frame boundary detection
 * - DMA in_suc_eof interrupt to copy data incrementally to frame buffer
 * - Byte filtering: each 32-bit DMA word yields 1 JPEG byte (sample1, bits 23:16)
 *
 * Camera pin mapping (YD-ESP32-CAM / AI-Thinker compatible):
 *   D0=GPIO5, D1=GPIO18, D2=GPIO19, D3=GPIO21,
 *   D4=GPIO36, D5=GPIO39, D6=GPIO34, D7=GPIO35
 *   VSYNC=GPIO25, HREF=GPIO23, PCLK=GPIO22
 */

#include "cam_i2s_capture.h"
#include "cam_jpeg_fixup.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <string.h>

#include <soc/i2s_struct.h>
#include <soc/i2s_reg.h>
#include <soc/gpio_sig_map.h>
#include <soc/gpio_pins.h>
#include <soc/periph_defs.h>
#include <soc/soc.h>
#include <esp_rom_gpio.h>
#include <esp_rom_lldesc.h>
#include <hal/gpio_ll.h>
#include <esp_private/periph_ctrl.h>
#include <esp_intr_alloc.h>

LOG_MODULE_REGISTER(cam_i2s, LOG_LEVEL_INF);

/* Camera data pins (YD-ESP32-CAM) */
static const uint8_t cam_data_pins[8] = {
	5, 18, 19, 21, 36, 39, 34, 35  /* D0..D7 */
};
#define CAM_VSYNC_PIN  25
#define CAM_HREF_PIN   23
#define CAM_PCLK_PIN   22

/*
 * DMA configuration.
 * Each descriptor max 4092 bytes. Using a ring for continuous capture.
 * rx_eof_num triggers in_suc_eof interrupt per ring cycle so we can
 * incrementally copy data to the frame buffer.
 *
 * In camera mode, each 8-bit pixel becomes a 32-bit DMA word,
 * so DMA_BUF_SIZE bytes of DMA holds DMA_BUF_SIZE/4 camera bytes.
 *
 * DRAM is at ~95% — keep DMA buffers small (4KB total).
 */
#define DMA_BUF_SIZE     1024  /* Must be multiple of 4 */
#define DMA_BUF_COUNT    4
#define DMA_TOTAL_SIZE   (DMA_BUF_SIZE * DMA_BUF_COUNT)

/* Max JPEG frame size after byte extraction (QVGA ~5-15KB typical) */
#define FRAME_BUF_SIZE   (64 * 1024)

/* I2S0 peripheral */
#define I2S_HW           I2S0

/*
 * DMA element: In camera mode with rx_fifo_mod=1, rx_bits_mod=8,
 * each camera byte is packed into a 32-bit word in SM_0A00_0B00 mode.
 * For JPEG: extract only sample1 (bits 23:16, byte offset 2).
 * Reference: esp32-camera ll_cam_dma_filter_jpeg()
 * Reference: esp32-camera ll_cam.c ll_cam_memcpy()
 */
typedef union {
	struct {
		uint8_t sample2;
		uint8_t unused2;
		uint8_t sample1;
		uint8_t unused1;
	};
	uint32_t val;
} dma_elem_t;

/* Static SRAM buffers for DMA (cannot be in PSRAM on ESP32) */
static lldesc_t dma_desc[DMA_BUF_COUNT] __attribute__((aligned(4)));
static uint8_t dma_buf[DMA_BUF_COUNT][DMA_BUF_SIZE] __attribute__((aligned(4)));

/* Frame buffer in PSRAM (ext_ram.bss linker section → SPIRAM) */
static uint8_t frame_buf_psram[FRAME_BUF_SIZE]
	__attribute__((section(".ext_ram.bss")));
static uint8_t *frame_buf = frame_buf_psram;
static volatile size_t frame_pos;
static volatile bool frame_done;
static volatile bool vsync_blanking;  /* Rising edge: blanking started */
static volatile bool capturing;
static volatile int vsync_skip_remaining;  /* Frames to skip before capture */
static struct k_sem frame_sem;
static intr_handle_t i2s_intr_handle;

/* Debug counters */
static volatile uint32_t dbg_vsync_count;
static volatile uint32_t dbg_dma_isr_count;
static volatile uint32_t dbg_dma_bytes_total;
static volatile uint32_t dbg_dma_overflow;

/* ISR-driven DMA recycling state */
static volatile int isr_next_desc;
static volatile bool extract_active; /* ISR extracts data when true */

/* Raw DMA debug: first 8 words from capture phase */
static uint32_t raw_debug[8];
static volatile int raw_debug_count;

/* VSYNC GPIO interrupt callback */
static struct gpio_callback vsync_cb_data;

static void cam_i2s_dma_start(void);

static void vsync_isr(const struct device *dev, struct gpio_callback *cb,
		      uint32_t pins)
{
	dbg_vsync_count++;

	if (!capturing) {
		return;
	}

	if (vsync_skip_remaining > 0) {
		vsync_skip_remaining--;
		return;
	}

	if (!vsync_blanking) {
		/* First rising edge after warmup: blanking period started.
		 * Signal polling loop to reset DMA in task context. */
		vsync_blanking = true;
		return;
	}

	/* Second rising edge: active frame captured, next blanking starts */
	frame_done = true;
}

static void cam_i2s_isr(void *arg)
{
	uint32_t status = I2S_HW.int_st.val;

	I2S_HW.int_clr.val = status;

	/* IN_SUC_EOF (bit 9): one DMA descriptor completed */
	if (status & BIT(9)) {
		dbg_dma_isr_count++;
		int idx = isr_next_desc;

		if (dma_desc[idx].owner == 0 && dma_desc[idx].length > 0) {
			if (extract_active && !frame_done &&
			    frame_pos < FRAME_BUF_SIZE) {
				/* Extract sample1 from each 32-bit DMA word */
				const uint32_t *src =
					(const uint32_t *)dma_desc[idx].buf;
				size_t words = dma_desc[idx].length / 4;

				/* Capture raw DMA words for debug (first descriptor only) */
				if (raw_debug_count == 0) {
					int n = words > 8 ? 8 : words;
					for (int d = 0; d < n; d++) {
						raw_debug[d] = src[d];
					}
					raw_debug_count = n;
				}

				for (size_t j = 0;
				     j < words && frame_pos < FRAME_BUF_SIZE;
				     j++) {
					frame_buf[frame_pos++] =
						(src[j] >> 16) & 0xFF;
				}
			}
			dbg_dma_bytes_total += dma_desc[idx].length;

			/* Recycle descriptor immediately */
			dma_desc[idx].length = 0;
			dma_desc[idx].owner = 1;
			isr_next_desc = (idx + 1) % DMA_BUF_COUNT;
		}
	}

	/* IN_DSCR_EMPTY (bit 15): DMA ran out of descriptors */
	if (status & BIT(15)) {
		dbg_dma_overflow++;
	}
}

static void cam_i2s_configure_gpio(void)
{
	/* Configure data pins as inputs and route to I2S0 via GPIO matrix */
	for (int i = 0; i < 8; i++) {
		esp_rom_gpio_pad_select_gpio(cam_data_pins[i]);
		gpio_ll_input_enable(&GPIO, cam_data_pins[i]);
		esp_rom_gpio_connect_in_signal(cam_data_pins[i],
					       I2S0I_DATA_IN0_IDX + i, false);
	}

	/*
	 * VSYNC routing to I2S: NOT inverted.
	 * OV2640 VSYNC: LOW during active frame, HIGH during blanking.
	 * I2S camera mode captures when V_SYNC input is LOW.
	 * No inversion needed for OV2640 default VSYNC polarity.
	 */
	esp_rom_gpio_pad_select_gpio(CAM_VSYNC_PIN);
	gpio_ll_input_enable(&GPIO, CAM_VSYNC_PIN);
	esp_rom_gpio_connect_in_signal(CAM_VSYNC_PIN, I2S0I_V_SYNC_IDX, false);

	/*
	 * H_SYNC: tie to constant HIGH (unused in camera mode).
	 * Reference: esp32-camera ll_cam_set_pin() ties H_SYNC high.
	 */
	esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ONE_INPUT,
				       I2S0I_H_SYNC_IDX, false);

	/*
	 * HREF → I2S0 H_ENABLE (gates data capture per line).
	 * When HREF is LOW (blanking), I2S does not sample — prevents
	 * garbage bytes at frame start.
	 */
	esp_rom_gpio_pad_select_gpio(CAM_HREF_PIN);
	gpio_ll_input_enable(&GPIO, CAM_HREF_PIN);
	esp_rom_gpio_connect_in_signal(CAM_HREF_PIN, I2S0I_H_ENABLE_IDX,
				       false);

	/* PCLK → I2S0 WS_IN (pixel clock drives sampling) */
	esp_rom_gpio_pad_select_gpio(CAM_PCLK_PIN);
	gpio_ll_input_enable(&GPIO, CAM_PCLK_PIN);
	esp_rom_gpio_connect_in_signal(CAM_PCLK_PIN, I2S0I_WS_IN_IDX, false);

	LOG_INF("Camera GPIO matrix configured");
}

static void cam_i2s_setup_dma(void)
{
	for (int i = 0; i < DMA_BUF_COUNT; i++) {
		dma_desc[i].size = DMA_BUF_SIZE;
		dma_desc[i].length = 0;
		dma_desc[i].buf = dma_buf[i];
		dma_desc[i].owner = 1;
		dma_desc[i].eof = 0;
		dma_desc[i].sosf = 0;
		dma_desc[i].offset = 0;
		dma_desc[i].empty = (uint32_t)&dma_desc[(i + 1) % DMA_BUF_COUNT];
	}
}

static void cam_i2s_configure_peripheral(void)
{
	periph_module_enable(PERIPH_I2S0_MODULE);

	/* Reset RX path */
	I2S_HW.conf.val = 0;
	I2S_HW.conf.rx_reset = 1;
	I2S_HW.conf.rx_reset = 0;
	I2S_HW.conf.rx_fifo_reset = 1;
	I2S_HW.conf.rx_fifo_reset = 0;

	/* Reset DMA */
	I2S_HW.lc_conf.val = 0;
	I2S_HW.lc_conf.in_rst = 1;
	I2S_HW.lc_conf.in_rst = 0;
	I2S_HW.lc_conf.ahbm_fifo_rst = 1;
	I2S_HW.lc_conf.ahbm_fifo_rst = 0;
	I2S_HW.lc_conf.ahbm_rst = 1;
	I2S_HW.lc_conf.ahbm_rst = 0;

	/* FIFO: SM_0A00_0B00 mode for 8-bit JPEG capture.
	 * Each camera byte packed into a 32-bit word at sample1 position. */
	I2S_HW.fifo_conf.val = 0;
	I2S_HW.fifo_conf.rx_data_num = 32;
	I2S_HW.fifo_conf.dscr_en = 1;
	I2S_HW.fifo_conf.rx_fifo_mod = 3;  /* SM_0A00_0B00 */
	I2S_HW.fifo_conf.rx_fifo_mod_force_en = 1;

	/* RX channel */
	I2S_HW.conf_chan.val = 0;
	I2S_HW.conf_chan.rx_chan_mod = 1;

	/* Camera/LCD mode */
	I2S_HW.conf2.val = 0;
	I2S_HW.conf2.lcd_en = 1;
	I2S_HW.conf2.camera_en = 1;

	/* RX slave mode (camera provides PCLK) */
	I2S_HW.conf.rx_slave_mod = 1;
	I2S_HW.conf.rx_right_first = 0;
	I2S_HW.conf.rx_msb_right = 0;
	I2S_HW.conf.rx_msb_shift = 0;
	I2S_HW.conf.rx_mono = 0;
	I2S_HW.conf.rx_short_sync = 0;

	/* Sample rate: match esp32-camera reference */
	I2S_HW.sample_rate_conf.val = 0;
	I2S_HW.sample_rate_conf.rx_bck_div_num = 1;
	I2S_HW.sample_rate_conf.rx_bits_mod = 0;

	/* Clock: APB clock with minimum divider */
	I2S_HW.clkm_conf.val = 0;
	I2S_HW.clkm_conf.clkm_div_num = 2;
	I2S_HW.clkm_conf.clkm_div_a = 0;
	I2S_HW.clkm_conf.clkm_div_b = 0;
	I2S_HW.clkm_conf.clk_en = 1;

	/* DMA burst mode */
	I2S_HW.lc_conf.check_owner = 0;
	I2S_HW.lc_conf.indscr_burst_en = 1;

	/*
	 * rx_eof_num: counted in 32-bit words, not bytes.
	 * Trigger in_suc_eof per DMA buffer for responsive data processing.
	 */
	I2S_HW.rx_eof_num = DMA_BUF_SIZE / 4;

	/* Timing: enable RX data synchronization */
	I2S_HW.timing.val = 0;
	I2S_HW.timing.rx_dsync_sw = 1;

	/* Set DMA inlink to descriptor ring */
	I2S_HW.in_link.addr = ((uint32_t)&dma_desc[0]) & 0xFFFFF;

	/* Enable in_suc_eof, in_dscr_empty, and in_dscr_err interrupts */
	I2S_HW.int_clr.val = 0xFFFFFFFF;
	I2S_HW.int_ena.val = 0;
	I2S_HW.int_ena.in_suc_eof = 1;
	I2S_HW.int_ena.in_dscr_err = 1;
	I2S_HW.int_ena.in_dscr_empty = 1;

	LOG_INF("I2S0 configured for camera mode");
}

static int cam_vsync_gpio_init(void)
{
	const struct device *gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));

	if (!device_is_ready(gpio0)) {
		LOG_ERR("GPIO0 device not ready for VSYNC");
		return -ENODEV;
	}

	/*
	 * OV2640 VSYNC: HIGH during blanking, LOW during active data.
	 * Rising edge = blanking period starts (safe to reset DMA).
	 * This matches esp32-camera reference (GPIO_INTR_POSEDGE).
	 */
	int ret = gpio_pin_configure(gpio0, CAM_VSYNC_PIN, GPIO_INPUT);

	if (ret < 0) {
		LOG_ERR("VSYNC pin configure failed: %d", ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure(gpio0, CAM_VSYNC_PIN,
					   GPIO_INT_EDGE_RISING);
	if (ret < 0) {
		LOG_ERR("VSYNC interrupt configure failed: %d", ret);
		return ret;
	}

	gpio_init_callback(&vsync_cb_data, vsync_isr, BIT(CAM_VSYNC_PIN));
	gpio_add_callback(gpio0, &vsync_cb_data);

	LOG_INF("VSYNC interrupt on GPIO%d (rising edge = blanking start)",
		CAM_VSYNC_PIN);
	return 0;
}

int cam_i2s_init(void)
{
	LOG_INF("I2S camera capture init");

	k_sem_init(&frame_sem, 0, 1);
	LOG_INF("Frame buffer: %d bytes at %p (PSRAM)", FRAME_BUF_SIZE, frame_buf);

	cam_i2s_setup_dma();
	cam_i2s_configure_gpio();

	int ret = cam_vsync_gpio_init();

	if (ret < 0) {
		return ret;
	}

	cam_i2s_configure_peripheral();

	/* Allocate I2S0 interrupt via ESP32 interrupt controller */
	esp_err_t err = esp_intr_alloc(ETS_I2S0_INTR_SOURCE,
				       ESP_INTR_FLAG_LOWMED,
				       cam_i2s_isr, NULL,
				       &i2s_intr_handle);
	if (err != ESP_OK) {
		LOG_ERR("esp_intr_alloc failed: %d", err);
		return -EIO;
	}

	LOG_INF("I2S camera capture ready");
	return 0;
}

/*
 * Full DMA reset + start sequence matching esp32-camera ll_cam_start().
 * Must reset ahbm_fifo_rst and ahbm_rst in addition to in_rst.
 */
static void cam_i2s_dma_start(void)
{
	/* Stop RX first */
	I2S_HW.conf.rx_start = 0;

	/* Reset I2S RX */
	I2S_HW.conf.rx_reset = 1;
	I2S_HW.conf.rx_reset = 0;
	I2S_HW.conf.rx_fifo_reset = 1;
	I2S_HW.conf.rx_fifo_reset = 0;

	/* Reset DMA — FULL sequence including AHB master */
	I2S_HW.lc_conf.in_rst = 1;
	I2S_HW.lc_conf.in_rst = 0;
	I2S_HW.lc_conf.ahbm_fifo_rst = 1;
	I2S_HW.lc_conf.ahbm_fifo_rst = 0;
	I2S_HW.lc_conf.ahbm_rst = 1;
	I2S_HW.lc_conf.ahbm_rst = 0;

	/* Restore lc_conf bits that may have been cleared by resets */
	I2S_HW.lc_conf.check_owner = 0;
	I2S_HW.lc_conf.indscr_burst_en = 1;

	/* Reset descriptors */
	for (int i = 0; i < DMA_BUF_COUNT; i++) {
		dma_desc[i].length = 0;
		dma_desc[i].owner = 1;
	}
	isr_next_desc = 0;

	/* Relink and start — order matters: addr, rx_eof_num, start, rx_start */
	I2S_HW.rx_eof_num = DMA_BUF_SIZE / 4;
	I2S_HW.in_link.addr = ((uint32_t)&dma_desc[0]) & 0xFFFFF;
	I2S_HW.in_link.start = 1;
	I2S_HW.int_clr.val = 0xFFFFFFFF;
	I2S_HW.conf.rx_start = 1;
}

int cam_i2s_capture_frame(const uint8_t **buf, size_t *size)
{
	#define WARMUP_FRAMES 30
	#define STREAM_SKIP_FRAMES 2
	static bool warmed_up;

	/* One-time DQT extraction: run DMA continuously for a few frames
	 * to capture the JPEG header tables.  This is a separate pass
	 * that does NOT affect the main capture timing. */
	if (!warmed_up && jpeg_saved_header_len() == 0) {
		LOG_INF("Extracting DQT tables (pre-capture)...");
		frame_pos = 0;
		frame_done = false;
		vsync_blanking = false;
		vsync_skip_remaining = 999; /* Don't trigger frame logic */
		extract_active = true;
		capturing = true;
		cam_i2s_dma_start();
		/* ~200ms at 15fps ≈ 3 frames — enough for DQTs */
		k_msleep(200);
		extract_active = false;
		I2S_HW.conf.rx_start = 0;
		I2S_HW.in_link.stop = 1;
		capturing = false;
		if (frame_pos > 0) {
			jpeg_extract_and_save_header(frame_buf, frame_pos);
		}
	}

	int skip = warmed_up ? STREAM_SKIP_FRAMES : WARMUP_FRAMES;

	/* Reset state */
	frame_pos = 0;
	frame_done = false;
	vsync_blanking = false;
	vsync_skip_remaining = skip;
	extract_active = false;
	isr_next_desc = 0;
	capturing = true;
	dbg_vsync_count = 0;
	dbg_dma_isr_count = 0;
	dbg_dma_bytes_total = 0;
	dbg_dma_overflow = 0;
	raw_debug_count = 0;

	if (!warmed_up) {
		LOG_INF("Capturing frame (skipping %d warm-up frames)...",
			skip);
	}

	/* Prime I2S RX: full DMA reset+start for warmup.
	 * ISR recycles descriptors (extract_active=false). */
	cam_i2s_dma_start();

	int64_t deadline = k_uptime_get() + 5000; /* 5s timeout */

	/* Phase 1: Wait for blanking period after warmup.
	 * Rising-edge VSYNC ISR sets vsync_blanking = true.
	 * ISR handles DMA recycling during warmup.
	 * Use k_yield to allow network stack to run between polls. */
	while (!vsync_blanking && k_uptime_get() < deadline) {
		k_yield();
	}

	if (!vsync_blanking) {
		LOG_WRN("Timeout waiting for VSYNC blanking");
		capturing = false;
		return -ETIMEDOUT;
	}

	/* Phase 2: Full DMA reset+start during blanking.
	 * Enable extraction so ISR copies bytes into frame_buf. */
	cam_i2s_dma_start();
	extract_active = true;

	/* Wait for frame_done (next rising VSYNC edge).
	 * Use k_yield to allow network stack to run between polls. */
	while (!frame_done && k_uptime_get() < deadline) {
		k_yield();
	}

	/* Phase 4: Let ISR drain remaining descriptors after frame_done.
	 * Don't stop RX immediately — give time for last DMA transfer. */
	k_busy_wait(500);

	/* Stop RX */
	I2S_HW.conf.rx_start = 0;
	I2S_HW.in_link.stop = 1;
	extract_active = false;
	capturing = false;

	if (!warmed_up) {
		LOG_INF("Diag: vsync=%u dma_isr=%u frame_pos=%zu "
			"dma_bytes=%u overflow=%u",
			dbg_vsync_count, dbg_dma_isr_count, frame_pos,
			dbg_dma_bytes_total, dbg_dma_overflow);
	}

	/* Raw DMA word dump — first capture only */
	if (!warmed_up && raw_debug_count >= 8) {
		LOG_INF("RawDMA[0..3]: %08x %08x %08x %08x",
			raw_debug[0], raw_debug[1],
			raw_debug[2], raw_debug[3]);
		LOG_INF("RawDMA[4..7]: %08x %08x %08x %08x",
			raw_debug[4], raw_debug[5],
			raw_debug[6], raw_debug[7]);
	}
	if (frame_pos == 0) {
		LOG_WRN("Frame captured but empty");
		return -ENODATA;
	}

	/* Verbose diagnostics — first capture only */
	if (!warmed_up) {
		/* Hex dump first 64 and last 32 camera bytes */
		size_t dump_len = frame_pos < 64 ? frame_pos : 64;
		char hex[193];
		size_t pos = 0;

		for (size_t i = 0; i < dump_len && pos < sizeof(hex) - 3;
		     i++) {
			static const char h[] = "0123456789abcdef";

			hex[pos++] = h[frame_buf[i] >> 4];
			hex[pos++] = h[frame_buf[i] & 0xF];
			hex[pos++] = ' ';
		}
		hex[pos] = '\0';
		LOG_INF("First[0..%d]: %s", (int)dump_len - 1, hex);

		if (frame_pos > 64) {
			size_t start = frame_pos > 32 ? frame_pos - 32 : 0;

			pos = 0;
			for (size_t i = start;
			     i < frame_pos && pos < sizeof(hex) - 3; i++) {
				static const char h[] = "0123456789abcdef";

				hex[pos++] = h[frame_buf[i] >> 4];
				hex[pos++] = h[frame_buf[i] & 0xF];
				hex[pos++] = ' ';
			}
			hex[pos] = '\0';
			LOG_INF("Last[%zu..%zu]: %s", start,
				frame_pos - 1, hex);
		}

		/* Search for ALL FF xx markers in captured data */
		int marker_count = 0;

		for (size_t i = 0; i + 1 < frame_pos &&
		     marker_count < 20; i++) {
			if (frame_buf[i] == 0xFF &&
			    frame_buf[i + 1] != 0x00 &&
			    frame_buf[i + 1] != 0xFF) {
				LOG_INF("Marker FF %02x at offset %zu",
					frame_buf[i + 1], i);
				marker_count++;
			}
		}

		/* Count non-zero bytes */
		size_t non_zero = 0;

		for (size_t i = 0; i < frame_pos; i++) {
			if (frame_buf[i] != 0) {
				non_zero++;
			}
		}
		LOG_INF("Non-zero bytes: %zu / %zu", non_zero, frame_pos);
	}

	/* Mark warmup done after first capture diagnostics */
	warmed_up = true;

	return jpeg_fixup_frame(frame_buf, frame_pos, FRAME_BUF_SIZE,
				buf, size);
}
