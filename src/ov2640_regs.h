/*
 * OV2640 Register Configuration
 *
 * Register tables extracted from Zephyr's ov2640.c for standalone use
 * without CONFIG_VIDEO (which adds ~2MB DRAM overhead).
 *
 * Configures OV2640 for JPEG output at QVGA (320x240).
 */

#ifndef OV2640_REGS_H
#define OV2640_REGS_H

#include <stdint.h>

struct ov2640_reg {
	uint8_t addr;
	uint8_t value;
};

/* Bank select */
#define BANK_SEL            0xFF
#define BANK_SEL_DSP        0x00
#define BANK_SEL_SENSOR     0x01

/* DSP registers (bank 0) */
#define R_BYPASS            0x05
#define R_BYPASS_DSP_EN     0x00
#define R_BYPASS_DSP_BYPAS  0x01
#define QS                  0x44
#define HSIZE               0x51
#define VSIZE               0x52
#define XOFFL               0x53
#define YOFFL               0x54
#define VHYX                0x55
#define TEST                0x57
#define ZMOW                0x5A
#define ZMOH                0x5B
#define ZMHH                0x5C
#define BPADDR              0x7C
#define BPDATA              0x7D
#define SIZEL               0x8C
#define HSIZE8              0xC0
#define VSIZE8              0xC1
#define CTRL0               0xC2
#define CTRL0_YUV422        0x08
#define CTRL0_YUV_EN        0x04
#define CTRL0_RGB_EN        0x02
#define CTRL1               0xC3
#define CTRL2               0x86
#define CTRL2_DCW_EN        0x20
#define CTRL2_SDE_EN        0x10
#define CTRL2_UV_ADJ_EN     0x08
#define CTRL2_UV_AVG_EN     0x04
#define CTRL2_CMX_EN        0x01
#define CTRL3               0x87
#define CTRL3_BPC_EN        0x80
#define CTRL3_WPC_EN        0x40
#define R_DVP_SP            0xD3
#define R_DVP_SP_AUTO_MODE  0x80
#define IMAGE_MODE          0xDA
#define IMAGE_MODE_JPEG_EN    0x10
#define IMAGE_MODE_RGB565     0x08
#define IMAGE_MODE_HREF_VSYNC 0x02
#define IMAGE_MODE_YUV422     0x00
#define RESET               0xE0
#define RESET_MICROC        0x40
#define RESET_SCCB          0x20
#define RESET_JPEG          0x10
#define RESET_DVP           0x04
#define MC_BIST             0xF9
#define MC_BIST_RESET       0x80
#define MC_BIST_BOOT_ROM_SEL 0x40
#define CTRLI               0x50
#define CTRLI_LP_DP         0x80

/* Sensor registers (bank 1) */
#define COM7                0x12
#define COM7_SRST           0x80
#define COM7_RES_UXGA       0x00
#define COM7_RES_SVGA       0x40
#define COM7_RES_CIF        0x20
#define COM7_ZOOM_EN        0x04
#define COM7_COLOR_BAR      0x02
#define COM8                0x13
#define COM8_DEFAULT        0xC0
#define COM8_BNDF_EN        0x20
#define COM8_AGC_EN         0x04
#define COM8_AEC_EN         0x01
#define COM9                0x14
#define COM9_DEFAULT        0x08
#define COM9_AGC_GAIN_8x    0x02
#define COM10               0x15
#define CLKRC               0x11
#define COM1                0x03
#define COM2                0x09
#define COM2_OUT_DRIVE_3x   0x02
#define COM3                0x0C
#define COM3_BAND_AUTO      0x02
#define COM19               0x25
#define ZOOMS               0x26
#define REG04               0x04
#define REG04_HREF_EN       0x08
#define AEW                 0x24
#define AEB                 0x25
#define VV                  0x26
#define FLL                 0x46
#define FLH                 0x47
#define REG5D               0x5D
#define REG5E               0x5E
#define REG5F               0x5F
#define REG60               0x60
#define HISTO_LOW           0x61
#define HISTO_HIGH          0x62
#define BD50                0x4F
#define BD60                0x50
#define REG32               0x32
#define REG32_UXGA          0x36
#define ARCOM2              0x34
#define HSTART              0x17
#define HSTOP               0x18
#define VSTART              0x19
#define VSTOP               0x1A

/* Resolution defines */
#define SVGA_HSIZE          800
#define SVGA_VSIZE          600
#define UXGA_HSIZE          1600
#define UXGA_VSIZE          1200

/* Default sensor configuration (from Zephyr ov2640.c) */
static const struct ov2640_reg ov2640_default_regs[] = {
	{ BANK_SEL, BANK_SEL_DSP },
	{ 0x2c,     0xff },
	{ 0x2e,     0xdf },
	{ BANK_SEL, BANK_SEL_SENSOR },
	{ 0x3c,     0x32 },
	{ CLKRC,    0x80 },
	{ COM2,     COM2_OUT_DRIVE_3x },
	{ REG04,    REG04_HREF_EN },
	{ COM8,     COM8_DEFAULT | COM8_BNDF_EN | COM8_AGC_EN | COM8_AEC_EN },
	{ COM9,     COM9_DEFAULT | (COM9_AGC_GAIN_8x << 5) },
	{ COM10,    0x00 },
	{ 0x2c,     0x0c },
	{ 0x33,     0x78 },
	{ 0x3a,     0x33 },
	{ 0x3b,     0xfb },
	{ 0x3e,     0x00 },
	{ 0x43,     0x11 },
	{ 0x16,     0x10 },
	{ 0x39,     0x02 },
	{ 0x35,     0x88 },
	{ 0x22,     0x0a },
	{ 0x37,     0x40 },
	{ 0x23,     0x00 },
	{ ARCOM2,   0xa0 },
	{ 0x06,     0x02 },
	{ 0x06,     0x88 },
	{ 0x07,     0xc0 },
	{ 0x0d,     0xb7 },
	{ 0x0e,     0x01 },
	{ 0x4c,     0x00 },
	{ 0x4a,     0x81 },
	{ 0x21,     0x99 },
	{ AEW,      0x40 },
	{ AEB,      0x38 },
	{ VV,       0x82 },
	{ COM19,    0x00 },
	{ ZOOMS,    0x00 },
	{ 0x5c,     0x00 },
	{ 0x63,     0x00 },
	{ FLL,      0x00 },
	{ FLH,      0x00 },
	{ COM3,     0x08 | COM3_BAND_AUTO },
	{ REG5D,    0x55 },
	{ REG5E,    0x7d },
	{ REG5F,    0x7d },
	{ REG60,    0x55 },
	{ HISTO_LOW,   0x70 },
	{ HISTO_HIGH,  0x80 },
	{ 0x7c,     0x05 },
	{ 0x20,     0x80 },
	{ 0x28,     0x30 },
	{ 0x6c,     0x00 },
	{ 0x6d,     0x80 },
	{ 0x6e,     0x00 },
	{ 0x70,     0x02 },
	{ 0x71,     0x94 },
	{ 0x73,     0xc1 },
	{ 0x3d,     0x34 },
	{ 0x5a,     0x57 },
	{ BD50,     0xbb },
	{ BD60,     0x9c },
	{ BANK_SEL, BANK_SEL_DSP },
	{ 0xe5,     0x7f },
	{ MC_BIST,  MC_BIST_RESET | MC_BIST_BOOT_ROM_SEL },
	{ 0x41,     0x24 },
	{ RESET,    RESET_JPEG | RESET_DVP },
	{ 0x76,     0xff },
	{ 0x33,     0xa0 },
	{ 0x42,     0x20 },
	{ 0x43,     0x18 },
	{ 0x4c,     0x00 },
	{ CTRL3,    CTRL3_BPC_EN | CTRL3_WPC_EN | 0x10 },
	{ 0x88,     0x3f },
	{ 0xd7,     0x03 },
	{ 0xd9,     0x10 },
	{ R_DVP_SP, R_DVP_SP_AUTO_MODE | 0x2 },
	{ 0xc8,     0x08 },
	{ 0xc9,     0x80 },
	{ BPADDR,   0x00 },
	{ BPDATA,   0x00 },
	{ BPADDR,   0x03 },
	{ BPDATA,   0x48 },
	{ BPDATA,   0x48 },
	{ BPADDR,   0x08 },
	{ BPDATA,   0x20 },
	{ BPDATA,   0x10 },
	{ BPDATA,   0x0e },
	{ 0x90,     0x00 },
	{ 0x91,     0x0e },
	{ 0x91,     0x1a },
	{ 0x91,     0x31 },
	{ 0x91,     0x5a },
	{ 0x91,     0x69 },
	{ 0x91,     0x75 },
	{ 0x91,     0x7e },
	{ 0x91,     0x88 },
	{ 0x91,     0x8f },
	{ 0x91,     0x96 },
	{ 0x91,     0xa3 },
	{ 0x91,     0xaf },
	{ 0x91,     0xc4 },
	{ 0x91,     0xd7 },
	{ 0x91,     0xe8 },
	{ 0x91,     0x20 },
	{ 0x92,     0x00 },
	{ 0x93,     0x06 },
	{ 0x93,     0xe3 },
	{ 0x93,     0x03 },
	{ 0x93,     0x03 },
	{ 0x93,     0x00 },
	{ 0x93,     0x02 },
	{ 0x93,     0x00 },
	{ 0x93,     0x00 },
	{ 0x93,     0x00 },
	{ 0x93,     0x00 },
	{ 0x93,     0x00 },
	{ 0x93,     0x00 },
	{ 0x93,     0x00 },
	{ 0x96,     0x00 },
	{ 0x97,     0x08 },
	{ 0x97,     0x19 },
	{ 0x97,     0x02 },
	{ 0x97,     0x0c },
	{ 0x97,     0x24 },
	{ 0x97,     0x30 },
	{ 0x97,     0x28 },
	{ 0x97,     0x26 },
	{ 0x97,     0x02 },
	{ 0x97,     0x98 },
	{ 0x97,     0x80 },
	{ 0x97,     0x00 },
	{ 0x97,     0x00 },
	{ 0xa4,     0x00 },
	{ 0xa8,     0x00 },
	{ 0xc5,     0x11 },
	{ 0xc6,     0x51 },
	{ 0xbf,     0x80 },
	{ 0xc7,     0x10 },
	{ 0xb6,     0x66 },
	{ 0xb8,     0xA5 },
	{ 0xb7,     0x64 },
	{ 0xb9,     0x7C },
	{ 0xb3,     0xaf },
	{ 0xb4,     0x97 },
	{ 0xb5,     0xFF },
	{ 0xb0,     0xC5 },
	{ 0xb1,     0x94 },
	{ 0xb2,     0x0f },
	{ 0xc4,     0x5c },
	{ 0xa6,     0x00 },
	{ 0xa7,     0x20 },
	{ 0xa7,     0xd8 },
	{ 0xa7,     0x1b },
	{ 0xa7,     0x31 },
	{ 0xa7,     0x00 },
	{ 0xa7,     0x18 },
	{ 0xa7,     0x20 },
	{ 0xa7,     0xd8 },
	{ 0xa7,     0x19 },
	{ 0xa7,     0x31 },
	{ 0xa7,     0x00 },
	{ 0xa7,     0x18 },
	{ 0xa7,     0x20 },
	{ 0xa7,     0xd8 },
	{ 0xa7,     0x19 },
	{ 0xa7,     0x31 },
	{ 0xa7,     0x00 },
	{ 0xa7,     0x18 },
	{ 0x7f,     0x00 },
	{ 0xe5,     0x1f },
	{ 0xe1,     0x77 },
	{ 0xdd,     0x7f },
	{ CTRL0,    CTRL0_YUV422 | CTRL0_YUV_EN | CTRL0_RGB_EN },
	{ 0x00,     0x00 },
};

/* UXGA sensor resolution (needed before any windowing) */
static const struct ov2640_reg ov2640_uxga_regs[] = {
	{ BANK_SEL, BANK_SEL_SENSOR },
	{ COM7,     COM7_RES_UXGA },
	{ COM1,     0x0F },
	{ REG32,    REG32_UXGA },
	{ HSTART,   0x11 },
	{ HSTOP,    0x75 },
	{ VSTART,   0x01 },
	{ VSTOP,    0x97 },
	{ 0x3d,     0x34 },
	{ 0x35,     0x88 },
	{ 0x22,     0x0a },
	{ 0x37,     0x40 },
	{ 0x34,     0xa0 },
	{ 0x06,     0x02 },
	{ 0x0d,     0xb7 },
	{ 0x0e,     0x01 },
	{ 0x42,     0x83 },
	/* DSP output window for QVGA (320x240) */
	{ BANK_SEL, BANK_SEL_DSP },
	{ R_BYPASS, R_BYPASS_DSP_BYPAS },
	{ RESET,    RESET_DVP },
	{ HSIZE8,   (UXGA_HSIZE >> 3) },
	{ VSIZE8,   (UXGA_VSIZE >> 3) },
	{ SIZEL,    ((UXGA_HSIZE >> 6) & 0x40) | ((UXGA_HSIZE & 0x7) << 3) |
		    (UXGA_VSIZE & 0x7) },
	{ XOFFL,    0x00 },
	{ YOFFL,    0x00 },
	{ HSIZE,    ((UXGA_HSIZE >> 2) & 0xFF) },
	{ VSIZE,    ((UXGA_VSIZE >> 2) & 0xFF) },
	{ VHYX,     ((UXGA_VSIZE >> 3) & 0x80) | ((UXGA_HSIZE >> 7) & 0x08) },
	{ TEST,     (UXGA_HSIZE >> 4) & 0x80 },
	{ CTRL2,    CTRL2_DCW_EN | CTRL2_SDE_EN |
		    CTRL2_UV_AVG_EN | CTRL2_CMX_EN | CTRL2_UV_ADJ_EN },
	{ CTRLI,    CTRLI_LP_DP | 0x00 },
	{ R_DVP_SP, R_DVP_SP_AUTO_MODE | 0x04 },
	{ R_BYPASS, R_BYPASS_DSP_EN },
	{ RESET,    0x00 },
	{ 0x00,     0x00 },
};

/* Resolution output window tables (ZMOW/ZMOH/ZMHH) */

/* QVGA (320x240) */
static const struct ov2640_reg ov2640_qvga_regs[] = {
	{ BANK_SEL, BANK_SEL_DSP },
	{ ZMOW,     (320 >> 2) & 0xFF },
	{ ZMOH,     (240 >> 2) & 0xFF },
	{ ZMHH,     ((320 >> 8) & 0x04) | ((240 >> 8) & 0x03) },
	{ 0x00,     0x00 },
};

/* VGA (640x480) */
static const struct ov2640_reg ov2640_vga_regs[] = {
	{ BANK_SEL, BANK_SEL_DSP },
	{ ZMOW,     (640 >> 2) & 0xFF },
	{ ZMOH,     (480 >> 2) & 0xFF },
	{ ZMHH,     ((640 >> 8) & 0x04) | ((480 >> 8) & 0x03) },
	{ 0x00,     0x00 },
};

/* SVGA (800x600) */
static const struct ov2640_reg ov2640_svga_regs[] = {
	{ BANK_SEL, BANK_SEL_DSP },
	{ ZMOW,     (800 >> 2) & 0xFF },
	{ ZMOH,     (600 >> 2) & 0xFF },
	{ ZMHH,     ((800 >> 8) & 0x04) | ((600 >> 8) & 0x03) },
	{ 0x00,     0x00 },
};

/* XGA (1024x768) */
static const struct ov2640_reg ov2640_xga_regs[] = {
	{ BANK_SEL, BANK_SEL_DSP },
	{ ZMOW,     (1024 >> 2) & 0xFF },
	{ ZMOH,     (768 >> 2) & 0xFF },
	{ ZMHH,     ((1024 >> 8) & 0x04) | ((768 >> 8) & 0x03) },
	{ 0x00,     0x00 },
};

/* SXGA (1280x1024) */
static const struct ov2640_reg ov2640_sxga_regs[] = {
	{ BANK_SEL, BANK_SEL_DSP },
	{ ZMOW,     (1280 >> 2) & 0xFF },
	{ ZMOH,     (1024 >> 2) & 0xFF },
	{ ZMHH,     ((1280 >> 8) & 0x04) | ((1024 >> 8) & 0x03) },
	{ 0x00,     0x00 },
};

/* UXGA (1600x1200) */
static const struct ov2640_reg ov2640_uxga_out_regs[] = {
	{ BANK_SEL, BANK_SEL_DSP },
	{ ZMOW,     (1600 >> 2) & 0xFF },
	{ ZMOH,     (1200 >> 2) & 0xFF },
	{ ZMHH,     ((1600 >> 8) & 0x04) | ((1200 >> 8) & 0x03) },
	{ 0x00,     0x00 },
};

/* JPEG mode enable.
 * Full register sequence from esp32-camera ov2640_settings_jpeg3.
 * Must be applied AFTER resolution/window programming.
 * 1. Assert JPEG + DVP reset
 * 2. Set IMAGE_MODE with JPEG_EN + HREF_VSYNC
 * 3. Configure JPEG DSP registers
 * 4. Release all resets
 */
static const struct ov2640_reg ov2640_jpeg_regs[] = {
	{ BANK_SEL, BANK_SEL_DSP },
	{ RESET,    RESET_JPEG | RESET_DVP },
	{ IMAGE_MODE, IMAGE_MODE_JPEG_EN | IMAGE_MODE_HREF_VSYNC },
	{ 0xD7,     0x03 },
	{ 0xE1,     0x77 },
	{ 0xE5,     0x1F },
	{ 0xD9,     0x10 },
	{ 0xDF,     0x80 },
	{ 0x33,     0x80 },
	{ 0x3C,     0x10 },
	{ 0xEB,     0x30 },
	{ 0xDD,     0x7F },
	{ RESET,    0x00 },
	{ 0x00,     0x00 },
};

#endif /* OV2640_REGS_H */
