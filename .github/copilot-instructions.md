# Autopilot-ESP32-CAM-Zephyr - Copilot Instructions

## Project Overview

This is an autonomous driving development project based on the **VCC-GND Studio YD-ESP32-CAM** development board using **Zephyr RTOS**.
The AI Agent acts as a senior embedded engineer, iterating daily to autonomously deliver the full development lifecycle from scratch to completion.

## Hardware Platform

- **Dev Board**: YD-ESP32-CAM (VCC-GND Studio)
- **Core Module**: ESP32-WROVER-E-N8R8 (Flash 8MB, PSRAM 8MB)
- **Core Chip**: ESP32-D0WD-V3 (Dual-core Xtensa LX6, 240MHz)
- **Camera**: OV2640
- **Onboard LED**: GPIO33

## Development Rules

### 0. Iron Rules (Highest Priority)

- **All terminal commands run via tmux** — Use sentinel mode from `.github/skills/tmux-multi-shell/SKILL.md`
- **Every code change must be tested on-device** — build → flash → monitor → verify, see `.github/skills/automated-testing/SKILL.md`
- **Compilation passing ≠ done** — Must observe expected behavior in serial logs
- **Browser must use visible mode (headless=False)** — No headless mode; use `~/.patchright-userdata` persistence directory
- **No restrictions on available tools** — Use every available tool
- **Python must use project-local `.venv/`** — `source .venv/bin/activate` before any `pip install` or `python3` usage. Never use system Python or external venvs.

### 1. WiFi Credential Security

**Strictly forbidden** to hardcode WiFi passwords into source code or any Git-tracked file.

WiFi credentials are read from the following sources (by priority):
1. Environment variables: `ESP_WIFI_SSID` / `ESP_WIFI_PASSWORD`
2. Config file: `~/.esp-wifi-credentials` (format in .github/skills/wifi-credentials/)
3. Kconfig: `CONFIG_WIFI_SSID` / `CONFIG_WIFI_PASSWORD` (prj.conf contains no passwords)

### 2. Iterative Workflow

- Each conversation is treated as one "work day"
- Each work day has clear small goals (1–3 tasks)
- Each task is tested and verified immediately after completion
- Use `docs/daily-logs/day-NNN.md` to log daily work
- Use `docs/TARGET.md` to track overall progress

### 3. Git Commit Conventions

```
feat: New feature
fix: Bug fix
refactor: Refactoring
docs: Documentation
test: Testing
chore: Build/tooling
```

### 4. Build Environment (Zephyr)

```bash
# Zephyr uses 'west' as the meta-tool
# Ensure ZEPHYR_BASE is set (or use west workspace)
west build -b esp32s3_devkitc/esp32s3/procpu
west flash
west espressif monitor
```

Key differences from ESP-IDF:
- **Build**: `west build` (not `idf.py build`)
- **Flash**: `west flash` (not `idf.py flash`)
- **Monitor**: `west espressif monitor` or `minicom` (not `idf.py monitor`)
- **Config**: `prj.conf` + board overlays (not `sdkconfig.defaults`)
- **APIs**: Zephyr native APIs (GPIO, WiFi, HTTP, WebSocket, etc.)

### 5. Serial Port

The YD-ESP32-CAM has no onboard USB-to-serial converter; an external USB-TTL adapter is required:
- TX → GPIO1 (U0TXD)
- RX → GPIO3 (U0RXD)
- GND → GND
- Baud rate: 115200

### 6. Camera Pins (AI-Thinker Compatible)

```c
#define PWDN_GPIO_NUM    32
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM     0
#define SIOD_GPIO_NUM    26
#define SIOC_GPIO_NUM    27
#define Y9_GPIO_NUM      35
#define Y8_GPIO_NUM      34
#define Y7_GPIO_NUM      39
#define Y6_GPIO_NUM      36
#define Y5_GPIO_NUM      21
#define Y4_GPIO_NUM      19
#define Y3_GPIO_NUM      18
#define Y2_GPIO_NUM       5
#define VSYNC_GPIO_NUM   25
#define HREF_GPIO_NUM    23
#define PCLK_GPIO_NUM    22
```

### 7. Available Skills

Skills provide standardized operating procedures. Read the corresponding SKILL.md before executing:

| Skill | Purpose |
|-------|---------|
| `.github/skills/tmux-multi-shell/` | tmux multi-terminal management |
| `.github/skills/zephyr-build-flash/` | Zephyr build, flash, and monitor |
| `.github/skills/esp32-serial-tools/` | Serial communication and log parsing |
| `.github/skills/cdp-web-inspector/` | Browser automation (Patchright) |
| `.github/skills/web-page-inspector/` | Web page data extraction |
| `.github/skills/automated-testing/` | Full test chain (build→flash→serial→browser) |
| `.github/skills/daily-iteration/` | Daily iteration workflow |
| `.github/skills/code-refactoring/` | Code refactoring strategy |
| `.github/skills/environment-setup/` | Environment verification |
| `.github/skills/project-scaffolding/` | Project scaffold generation |
| `.github/skills/board-pinout-reference/` | YD-ESP32-CAM pinout reference |
| `.github/skills/wifi-credentials/` | WiFi credential security |

### 8. MCP Servers

The following MCP servers are configured in `.vscode/mcp.json`:

| Server | Purpose |
|--------|---------|
| `espressif-documentation` | Search Espressif official docs (ESP32, Zephyr-on-ESP32) |
| `esp-component-registry` | Search ESP component registry for examples and components |
