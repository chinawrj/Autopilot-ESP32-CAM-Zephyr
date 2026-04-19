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

### M0: Project Scaffold & Zephyr Environment ✅ Complete
> **Completed**: Day 2

- [x] Zephyr workspace setup (`west init`, `west update`) — T2 topology, Zephyr v4.1.0
- [x] ESP32 board support verified (`west build -b esp32_devkitc_wrover/esp32/procpu`)
- [x] Board overlay for LED on GPIO33 (DTS overlay with led0 alias)
- [x] prj.conf with WiFi, networking, DHCP config
- [x] WiFi management module (connect, auto-reconnect, secure credential reading)
- [x] Build passed + flash succeeded + serial shows WiFi connection log + got IP (192.168.1.168)
- [x] Git: initial commit

**Completion criteria**: Serial output shows `WiFi connected` with assigned IP address. ✅ Verified

---

### M1: Basic TCP Video Stream ✅ Complete
> **Completed**: Day 5

- [x] Camera driver integration (custom I2S DMA driver, not Zephyr video subsystem)
- [x] OV2640 initialization with PSRAM frame buffer (64KB in PSRAM)
- [x] HTTP server (custom socket-based, BSD sockets API, listener + stream worker threads)
- [x] GET `/` → homepage (styled HTML with MJPEG embed)
- [x] GET `/stream/tcp` → MJPEG stream (multipart/x-mixed-replace)
- [x] Browser verified: MJPEG stream working (real camera frames)
- [x] Frame source abstraction (pluggable camera backend)
- [x] JPEG header reconstruction (pre-capture DQT/DHT/SOF0/SOS extraction)
- [x] Concurrent page serving while streaming (503 for busy stream)
- [x] Timing instrumentation (capture_ms, send_ms, FPS)

**Completion criteria**: Browser displays live MJPEG stream from `http://<DEVICE_IP>/stream/tcp`. ✅ Verified

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
| 1 | 2025-04-19 | Project scaffold, Zephyr workspace, LED blink build passing | ✅ |
| 2 | 2026-04-19 | WiFi module, on-device verified (IP: 192.168.1.168), M0 complete | ✅ |
| 3 | 2026-04-19 | HTTP server + MJPEG test stream, browser verified | ✅ |
| 4 | 2026-04-19 | OV2640 camera bring-up, I2S DMA capture, JPEG header reconstruction | ✅ |
| 5 | 2026-04-20 | HTTP architecture rewrite, AEC/AGC tuning, M1 complete | ✅ |
