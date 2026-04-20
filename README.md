# Autopilot ESP32-CAM Zephyr

A real-time camera web server on the **YD-ESP32-CAM** (ESP32-WROVER-E-N8R8) using **Zephyr RTOS v4.1.0**.

## Features

- **OV2640 Camera** with runtime resolution switching (QVGA → UXGA)
- **Dual-path streaming**: MJPEG over TCP and WebSocket binary
- **Web UI** with live video, HUD telemetry overlay, LED control, resolution selector
- **REST API** for status, LED control, resolution switching, and snapshots
- **WiFi** with auto-reconnect, power save disabled, RSSI monitoring
- **Hardware watchdog** (30s timeout)
- **Memory-safe**: Zero-leak verified (3-min soak + 20 connect/disconnect cycles)

## Hardware

| Component | Detail |
|-----------|--------|
| Board | YD-ESP32-CAM (VCC-GND Studio) |
| SoC | ESP32-D0WD-V3 (Dual-core Xtensa LX6, 240MHz) |
| Memory | Flash 8MB, PSRAM 8MB |
| Camera | OV2640 (DVP interface) |
| LED | GPIO33 (onboard) |

## Supported Resolutions

| Resolution | Size | Typical FPS |
|-----------|------|-------------|
| QVGA | 320×240 | ~14 |
| VGA | 640×480 | ~5 |
| SVGA | 800×600 | ~3 |
| XGA | 1024×768 | ~2 |
| SXGA | 1280×1024 | ~1.5 |
| UXGA | 1600×1200 | ~1.1 |

## Quick Start

### Prerequisites

- Zephyr SDK with ESP32 support
- `west` meta-tool
- USB-TTL adapter (CH340/CP2102) connected to GPIO1 (TX) and GPIO3 (RX)

### Build & Flash

```bash
# Initialize workspace (first time only)
west init -l .
west update
source zephyr/zephyr-env.sh

# Build
export ZEPHYR_TOOLCHAIN_VARIANT=espressif
export ESPRESSIF_TOOLCHAIN_PATH=$HOME/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf
west build -b esp32_devkitc_wrover/esp32/procpu

# Flash (adjust serial port)
west flash --esp-device /dev/cu.wchusbserial210 --esp-baud-rate 460800

# Monitor
west espressif monitor -p /dev/cu.wchusbserial210
```

### WiFi Configuration

WiFi credentials are read from (in priority order):
1. Environment variables: `ESP_WIFI_SSID` / `ESP_WIFI_PASSWORD`
2. Config file: `~/.esp-wifi-credentials`
3. Kconfig defaults in `prj.conf`

Create `~/.esp-wifi-credentials`:
```
SSID=YourNetworkName
PASSWORD=YourPassword
```

## Web Interface

Once booted, navigate to `http://<device-ip>/` for the main UI.

### Endpoints

| Endpoint | Port | Description |
|----------|------|-------------|
| `GET /` | 80 | Main page (MJPEG stream + HUD) |
| `GET /ws` | 80 | WebSocket stream page |
| `GET /stream/tcp` | 81 | MJPEG stream |
| `GET /stream/ws` | 81 | WebSocket binary stream |
| `GET /snapshot.jpg` | 81 | Single JPEG snapshot |
| `GET /api/status` | 80 | JSON status (FPS, heap, WiFi, resolution) |
| `GET /api/led/{on\|off\|toggle\|auto}` | 80 | LED control |
| `GET /api/resolution/{qvga\|vga\|...\|uxga}` | 80 | Set resolution |
| `GET /api/resolution/get` | 80 | Get current resolution |

## Architecture

```
main.c              — App entry, WiFi init, watchdog
camera_init.c       — OV2640 I2C config, resolution switching
cam_i2s_capture.c   — I2S DMA capture, JPEG frame extraction
cam_jpeg_fixup.c    — JPEG header reconstruction (DQT/DHT/SOF0)
frame_source.c      — Frame provider abstraction
http_server.c       — HTTP routing, API handlers
stream_handler.c    — MJPEG/WebSocket streaming thread
wifi_manager.c      — WiFi connect, reconnect, stats
led_control.c       — LED GPIO control (manual/auto modes)
html_pages.h        — Embedded HTML/CSS/JS templates
ov2640_regs.h       — OV2640 register definitions
```

## License

MIT
