# Autopilot ESP32-CAM Zephyr — Project Goals & Progress Tracking

## Overall Goal

Build a complete **real-time camera web server** on the YD-ESP32-CAM (ESP32-WROVER-E-N8R8)
using **Zephyr RTOS**, supporting dual-path TCP/WebSocket video streaming, real-time HUD overlay,
and LED control.

## Key Difference from ESP-IDF Version

This project uses **Zephyr RTOS** instead of ESP-IDF. Key implications:
- Build system: `west build` / `west flash` (not `idf.py`)
- Configuration: `prj.conf` + Kconfig (not `sdkconfig.defaults`)
- APIs: Zephyr native APIs (GPIO, WiFi, HTTP, WebSocket, etc.)
- Camera driver: Requires Zephyr video subsystem or ESP32 camera HAL wrapper
- Monitor: `west espressif monitor` or `minicom -D /dev/cu.wchusbserial110 -b 115200`

## Milestone Progress

### M0: Project Scaffold & Zephyr Environment ⬜ Not Started
> **Target**: Day 1-2

- [ ] Zephyr workspace setup (`west init`, `west update`)
- [ ] ESP32 board support verified (`west build -b esp32s3_devkitc/esp32s3/procpu` or custom board)
- [ ] Custom board overlay for YD-ESP32-CAM (pin mappings, PSRAM, camera)
- [ ] prj.conf with WiFi, networking, HTTP server basics
- [ ] WiFi management module (connect, auto-reconnect, secure credential reading)
- [ ] Build passed + flash succeeded + serial shows WiFi connection log + got IP
- [ ] Git: initial commit

**Completion criteria**: Serial output shows `WiFi connected` with assigned IP address.

---

### M1: Basic TCP Video Stream ⬜ Not Started
> **Target**: Day 3-5

- [ ] Camera driver integration (Zephyr video subsystem or ESP32 camera HAL)
- [ ] OV2640 initialization with PSRAM frame buffer
- [ ] HTTP server (Zephyr `net/http/server` or custom socket-based)
- [ ] GET `/` → homepage (index.html)
- [ ] GET `/stream/tcp` → MJPEG stream (multipart/x-mixed-replace)
- [ ] Browser verified: MJPEG stream working

**Completion criteria**: Browser displays live MJPEG stream from `http://<DEVICE_IP>/stream/tcp`.

---

### M2: HUD Overlay ⬜ Not Started
> **Target**: Day 6-7

- [ ] Real-time FPS calculation (server-side frame rate tracking)
- [ ] Virtual temperature sensor (ESP32 hardware RNG, baseline 25°C ±3°C)
- [ ] REST API: GET `/api/status` → JSON {fps, temperature, led_state}
- [ ] Frontend HUD: JavaScript polling + overlay on video stream

**Completion criteria**: `/api/status` returns valid JSON with fps, temperature, led_state.

---

### M3: LED Control ⬜ Not Started
> **Target**: Day 8

- [ ] GPIO33 LED driver (Zephyr GPIO API)
- [ ] REST API: POST `/api/led` {state: on/off/toggle}
- [ ] Frontend button with real-time state feedback

**Completion criteria**: LED toggles via API and browser button.

---

### M4: WebSocket Video Stream ⬜ Not Started
> **Target**: Day 9-11

- [ ] WebSocket server (Zephyr WebSocket API)
- [ ] `/stream/ws` endpoint: real-time JPEG frames via WebSocket
- [ ] Frontend page: WebSocket receive + Canvas rendering
- [ ] TCP/WebSocket dual-path coexistence

**Completion criteria**: Both `/stream/tcp` (MJPEG) and `/stream/ws` (WebSocket) work simultaneously.

---

### M5: Stability & Optimization ⬜ Not Started
> **Target**: Day 12-14

- [ ] Memory leak detection (Zephyr heap monitoring)
- [ ] WiFi reconnection stress testing
- [ ] PSRAM optimization (frame buffer allocation strategy)
- [ ] Watchdog timer configuration
- [ ] 24-hour continuous operation test
- [ ] Code refactoring and documentation

**Completion criteria**: 24-hour continuous operation without crash; all tests pass.

---

## Daily Log Index

| Day | Date | Summary | Status |
|-----|------|---------|--------|
| 1 | — | | ⬜ |
