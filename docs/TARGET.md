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

### M2: HUD Overlay + LED Control ✅ Complete
> **Completed**: Day 6

- [x] Real-time FPS calculation (server-side frame rate tracking, fps×10 integer)
- [x] Virtual temperature sensor (ESP32 hardware RNG, baseline 25°C ±3°C)
- [x] REST API: GET `/api/status` → JSON {fps, uptime, temp, led, led_mode, stream, frames}
- [x] REST API: GET `/api/led/{on,off,toggle,auto}` → JSON {led, mode}
- [x] LED control module (src/led_control.c) with auto/manual modes
- [x] Frontend HUD: JavaScript polling /api/status every 2s + overlay on video
- [x] LED control buttons on index page
- [x] Camera capture yield fix (k_busy_wait→k_yield for network coexistence)

**Completion criteria**: `/api/status` returns valid JSON; LED toggles via API. ✅ Verified

---

### M3: Stream Stability ✅ Complete
> **Completed**: Day 7

- [x] Diagnosed TCP accept corruption bug (Zephyr v4.1.0 ESP32 TCP state machine)
- [x] Two-port architecture fix (HTTP:80, Stream:81 on separate listen sockets)
- [x] poll()-based multiplexing of both listen sockets in single thread
- [x] Comprehensive interleaved HTTP→Stream testing (8/8 pass)
- [x] ZVFS_OPEN_MAX raised to 16 (default 4 was too low for server)
- [x] TCP retry count tuned (5→2) for faster connection cleanup

**Completion criteria**: HTTP pages and MJPEG stream work reliably in any interleaved order. ✅ Verified

---

### M4: WebSocket Video Stream ✅ Complete
> **Completed**: Day 8

- [x] Custom WebSocket handshake (SHA-1 + Base64, RFC 6455 compliant)
- [x] `/stream/ws` endpoint: real-time JPEG frames via WebSocket binary messages
- [x] Frontend page (`/ws`): WebSocket receive + Canvas rendering
- [x] TCP/WebSocket dual-path coexistence (MJPEG on :81/stream/tcp, WS on :81/stream/ws)
- [x] Browser verified (Chromium): Canvas renders live camera frames
- [x] Python `websockets` library verified: 3+ JPEG frames received

**Completion criteria**: Both `/stream/tcp` (MJPEG) and `/stream/ws` (WebSocket) work simultaneously. ✅ Verified

---

### M5: Stability & Optimization 🔄 In Progress
> **Started**: Day 9

- [x] Watchdog timer configuration (30s HW WDT, feeds from heartbeat loop)
- [x] Memory monitoring (system heap stats in `/api/status` + HUD)
- [x] WiFi RSSI tracking (signal strength in `/api/status` + HUD + heartbeat log)
- [x] Memory leak detection (3-min soak + 20 connect/disconnect cycles — no leaks)
- [x] Code refactoring (HTML template extraction, http_server.c 1146→1051 lines)
- [ ] WiFi reconnection stress testing
- [ ] 24-hour continuous operation test
- [ ] Full code documentation pass

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
| 6 | 2026-04-20 | HUD overlay, status API, LED control, capture yield fix, M2 complete | ✅ |
| 7 | 2026-04-20 | TCP accept bug diagnosis, two-port fix (HTTP:80/Stream:81), M3 complete | ✅ |
| 8 | 2026-04-20 | WebSocket GUID fix, browser+Python WS streaming verified, M4 complete | ✅ |
| 9 | 2026-04-20 | Watchdog timer, heap monitoring, WiFi RSSI in status API + HUD, M5 started | ✅ |
| 10 | 2026-04-20 | HTML template extraction, memory leak soak test (PASS, zero drift) | ✅ |
