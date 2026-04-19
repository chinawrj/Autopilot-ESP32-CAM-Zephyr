---
name: board-pinout-reference
description: "Hardware pinout reference for YD-ESP32-CAM (VCC-GND Studio): GPIO assignments, camera OV2640 pins, SD card pins, conflicts. Use when: writing pin config, initializing camera/SD, debugging GPIO conflicts."
---

# Skill: Dev Board Pinout & Resource Reference

## Purpose

Provides the AI Agent with quick-reference information on dev board hardware pin definitions (pinout),
core module parameters, and schematic highlights, to avoid peripheral failures or hardware damage
caused by incorrect pin mappings during development.

**When to use:**
- Need to confirm which physical pin or function a GPIO corresponds to
- Need to check pin mappings for camera/SD card/serial and other peripherals before writing driver code
- Need to know the core module model, Flash/PSRAM capacity, and other parameters used by the dev board
- Need pinout reference when wiring or designing peripheral circuits
- Need to confirm whether a pin is input-only or has special restrictions during debugging

**When not to use:**
- Low-level chip register programming (refer to the official Espressif TRM)
- Using standard framework API calls (e.g., `gpio_set_level`)
- The dev board is not in this Skill's supported list

## Prerequisites

- No special dependencies; this Skill is a pure data reference
- To query boards not in the list, use the `web-page-inspector` Skill to scrape from the manufacturer's website

## Supported Boards

| Manufacturer | Model | Core Module | Status |
|-------------|-------|-------------|--------|
| VCC-GND Studio | YD-ESP32-CAM | ESP32-WROVER-E-N8R8 | ✅ Included |

> More boards will be added over time. To contribute a new board, see the "Adding a New Board" section at the end.

---

## Board: YD-ESP32-CAM (VCC-GND Studio)

### Summary

| Parameter | Value |
|-----------|-------|
| **Manufacturer** | VCC-GND Studio (vcc-gnd.com) |
| **Model** | YD-ESP32-CAM |
| **Core Chip** | ESP32-D0WD-V3 (Dual-core Xtensa LX6, 240MHz) |
| **Core Module** | Espressif ESP32-WROVER-E-N8R8 |
| **Flash** | 8 MB |
| **PSRAM** | 8 MB (external) |
| **Camera** | OV2640 (default), compatible with OV3660 |
| **TF Card Slot** | Yes (Micro SD, SPI mode) |
| **Onboard LED** | GPIO33 |
| **BOOT Button** | GPIO0 |
| **Power Supply** | 5V (VIN) / 3.3V |
| **USB-to-Serial** | None onboard (external USB-TTL adapter required for flashing) |

### ESP32 Pin Multiplexing (PINMUX) Notes

> ⚠️ **Understanding PINMUX is a prerequisite for correctly using the pin table.**

ESP32 peripheral pin assignment uses two mechanisms, and different peripherals use different ones:

| Mechanism | Principle | Remappable? | Peripherals on This Board |
|-----------|-----------|-------------|--------------------------|
| **GPIO Matrix** | Signals routed to GPIOs via a programmable crossbar matrix | ✅ Freely remappable | Camera (I2S), UART, I2C (SCCB) |
| **IO MUX** | Signals directly connected to fixed GPIOs with no matrix delay | ❌ Hardware-fixed | SDMMC Host Slot 2 (HS2) |

**Impact on the pin table:**

- **Camera pins**: Routed via GPIO Matrix. The GPIO numbers in the table are **chosen by the board PCB designer**,
  not mandated by the ESP32 chip. Different ESP32-CAM boards from different manufacturers may use different mappings.
  Each pin must be configured via `#define` in code.
- **TF card (SDMMC) pins**: Connected directly to SDMMC HS2 Slot via IO MUX. The GPIO numbers are
  **hardware-fixed by the ESP32 chip**, the same across all ESP32 boards, and cannot be changed.
  If accessing the SD card via SPI mode, remapping through GPIO Matrix is possible.
- **Serial pins**: UART0 defaults to IO MUX assignment of GPIO1 (TX) / GPIO3 (RX), but can be remapped to other pins via GPIO Matrix.
- **I2C (SCCB) pins**: Routed via GPIO Matrix, freely remappable.

> Reference: [ESP32 Technical Reference Manual - IO MUX and GPIO Matrix](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_cn.pdf) §4

### Camera Interface Pins (OV2640/OV3660)

> 🔀 **Routing: GPIO Matrix** — The following pins are determined by board PCB design, not fixed by the chip

| Camera Signal | ESP32 GPIO | Description |
|--------------|-----------|-------------|
| D0 | GPIO5 | Data line bit0 |
| D1 | GPIO18 | Data line bit1 |
| D2 | GPIO19 | Data line bit2 |
| D3 | GPIO21 | Data line bit3 |
| D4 | GPIO36 | Data line bit4 (input-only) |
| D5 | GPIO39 | Data line bit5 (input-only) |
| D6 | GPIO34 | Data line bit6 (input-only) |
| D7 | GPIO35 | Data line bit7 (input-only) |
| XCLK | GPIO0 | Master clock output (shared with BOOT button) |
| PCLK | GPIO22 | Pixel clock input |
| VSYNC | GPIO25 | Frame sync |
| HREF | GPIO23 | Line sync |
| SDA | GPIO26 | SCCB data line (I2C) |
| SCL | GPIO27 | SCCB clock line (I2C) |
| POWER GPIO | GPIO32 | Camera power control (active low) |

### TF Card (Micro SD) Pins

> 📌 **Routing: IO MUX (SDMMC HS2)** — The following pins are hardware-fixed by the ESP32 chip and cannot be changed

| TF Card Signal | ESP32 GPIO | SDMMC HS2 Function | Description |
|---------------|-----------|-------------------|-------------|
| CLK | GPIO14 | HS2_CLK | SDMMC clock |
| CMD | GPIO15 | HS2_CMD | SDMMC command line |
| DATA0 | GPIO2 | HS2_DATA0 | SDMMC data line 0 |
| DATA1 | GPIO4 | HS2_DATA1 | Data line 1 (unused in 1-bit mode) |
| DATA2 | GPIO12 | HS2_DATA2 | Data line 2 (⚠️ must be low at boot) |
| DATA3 | GPIO13 | HS2_DATA3 | Data line 3 / SPI CS |

> ⚠️ **GPIO12** is the MTDI pin. At boot, ESP32 reads this pin to determine VDD_SDIO voltage.
> If the SD card pulls GPIO12 high at power-on, it may cause incorrect Flash supply voltage and boot failure.
> Solution: Use `espefuse.py set_flash_voltage 3.3V` to force a fixed voltage.

### Serial Pins

> 🔀 **Routing: IO MUX default + GPIO Matrix remappable**

| Signal | ESP32 GPIO | IO MUX Function | Description |
|--------|-----------|----------------|-------------|
| TX | GPIO1 | U0TXD | UART0 default transmit (IO MUX Function 1) |
| RX | GPIO3 | U0RXD | UART0 default receive (IO MUX Function 1) |

> Note: The serial GPIO column is blank in the original VCC-GND Studio documentation; the actual pins are GPIO1/GPIO3 (UART0 default IO MUX pins).
> UART signals can be remapped to other pins via GPIO Matrix, but since this board has no onboard USB-to-serial converter,
> these default pins must be used when connecting an external USB-TTL adapter.

### Other Pins

| Function | ESP32 GPIO | Description |
|----------|-----------|-------------|
| BOOT Button | GPIO0 | Hold during power-on to enter download mode; shared with XCLK |
| Onboard LED | GPIO33 | Active high to turn on |
| Flash LED | GPIO4 | flash LED (shared with SD DAT1) |

### GPIO Conflict Warnings

```
⚠️ Key conflicts:
├── GPIO0:  XCLK (camera) + BOOT button → Disconnect camera or hold BOOT when flashing
├── GPIO4:  flash LED + SD DAT1 → flash LED unavailable when using SD 4-bit mode
├── GPIO12: SD DAT2 + MTDI (boot voltage select) → Must fix voltage via efuse
├── GPIO2:  SD DAT0 → Some programmers require GPIO2 floating to enter download mode
└── GPIO34-39: Input-only, cannot be used as outputs
```

### ESP-IDF Camera Configuration Reference

```c
// camera_pins.h for YD-ESP32-CAM
#define CAMERA_MODEL_YD_ESP32_CAM

#define PWDN_GPIO_NUM    32
#define RESET_GPIO_NUM   -1  // No hardware reset pin
#define XCLK_GPIO_NUM     0
#define SIOD_GPIO_NUM    26  // SDA
#define SIOC_GPIO_NUM    27  // SCL

#define Y9_GPIO_NUM      35  // D7
#define Y8_GPIO_NUM      34  // D6
#define Y7_GPIO_NUM      39  // D5
#define Y6_GPIO_NUM      36  // D4
#define Y5_GPIO_NUM      21  // D3
#define Y4_GPIO_NUM      19  // D2
#define Y3_GPIO_NUM      18  // D1
#define Y2_GPIO_NUM       5  // D0

#define VSYNC_GPIO_NUM   25
#define HREF_GPIO_NUM    23
#define PCLK_GPIO_NUM    22
```

### Arduino Camera Configuration Reference

```cpp
// For YD-ESP32-CAM (identical to AI-Thinker ESP32-CAM pinout)
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"
```

> Note: The YD-ESP32-CAM camera pinout is fully compatible with the AI-Thinker ESP32-CAM.
> In Arduino libraries, you can directly select `CAMERA_MODEL_AI_THINKER`.

---

## Core Module Reference: ESP32-WROVER-E-N8R8

| Parameter | Value |
|-----------|-------|
| **Chip** | ESP32-D0WD-V3 |
| **CPU** | Dual-core Xtensa LX6, up to 240 MHz |
| **SRAM** | 520 KB |
| **Flash** | 8 MB (Quad SPI) |
| **PSRAM** | 8 MB (Octal SPI) |
| **WiFi** | 802.11 b/g/n, 2.4 GHz |
| **Bluetooth** | BT 4.2 + BLE |
| **Operating Temperature** | -40°C ~ 85°C |
| **Supply Voltage** | 3.0V ~ 3.6V |
| **Antenna** | PCB onboard antenna |
| **Package Size** | 18mm × 20mm × 3.2mm |
| **Certifications** | FCC / CE / TELEC / KCC |

> Espressif official datasheet: https://www.espressif.com/sites/default/files/documentation/esp32-wrover-e_esp32-wrover-ie_datasheet_cn.pdf

---

## Adding a New Board

When contributing new board information to this Skill, please follow this template:

```markdown
## Board: <Model> (<Manufacturer>)

### Summary
| Parameter | Value |
|-----------|-------|
| **Manufacturer** | ... |
| **Model** | ... |
| **Core Module** | ... |
| **Flash** | ... |
| **PSRAM** | ... |
| ... | ... |

### <Peripheral> Pins
| Signal | GPIO | Description |
|--------|------|-------------|
| ... | ... | ... |

### GPIO Conflict Warnings
List known pin conflicts and boot restrictions.

### Code Configuration Reference
Provide pin configuration code snippets for ESP-IDF / Arduino / MicroPython.
```

**Data source requirements:**
- Official manufacturer documentation or schematics (preferred)
- Physical board silkscreen markings
- Community-verified third-party sources (cite the source)

---

## Self-Test

### Self-Test Steps

Verify the completeness and consistency of pin data in SKILL.md.

```bash
bash skills/board-pinout-reference/self-test.sh
```

### Expected Results

| Test Item | Expected |
|-----------|----------|
| SKILL.md exists | PASS |
| At least 1 board included | PASS |
| Camera pin count = 15 | PASS |
| TF card pin count = 6 | PASS |
| Serial pin count = 2 | PASS |
| GPIO numbers in valid range (0-39) | PASS |
| No duplicate GPIO mappings (within same peripheral) | PASS |
| ESP-IDF config code matches pin table | PASS |
| Conflict warnings included | PASS |
| Module reference section included | PASS |

### Blind Test

**Scenario:**
A new AI Agent needs to write camera initialization code for the YD-ESP32-CAM board and correctly configure the SD card.

**Test Prompt:**
> I have a VCC-GND Studio YD-ESP32-CAM development board. Please help me:
> 1. Provide the OV2640 camera pin configuration (ESP-IDF style #defines)
> 2. Tell me which GPIOs the SD card uses and any important caveats
> 3. Which GPIO is the onboard LED

**Acceptance Criteria:**
- [ ] Camera configuration includes all 15 pin definitions (PWDN, RESET, XCLK, SDA, SCL, D0-D7, VSYNC, HREF, PCLK)
- [ ] SD card lists 6 pins (CLK=14, CMD=15, DATA0=2, DATA1=4, DATA2=12, DATA3=13)
- [ ] Explicitly mentions the GPIO12 boot voltage conflict and the `espefuse.py` solution
- [ ] Onboard LED correctly identified as GPIO33
- [ ] Mentions the GPIO0 XCLK/BOOT dual-function conflict

**Common Failure Modes:**
- Agent confuses AI-Thinker ESP32-CAM with YD-ESP32-CAM differences → Pins are actually compatible, but modules differ
- Agent provides incorrect PWDN pin (some boards use -1, YD board uses GPIO32)
- Agent omits the GPIO12 boot restriction → May cause the user's board to fail to boot

## Success Criteria

- [ ] Pin table data comes from reliable sources (manufacturer docs / physical verification)
- [ ] GPIO mappings for each peripheral are complete with no omissions
- [ ] Critical conflicts and restrictions are prominently marked (⚠️ symbol)
- [ ] Provides configuration snippets that can be directly copied into code
- [ ] All Self-Tests PASS
