---
name: zephyr-build-flash
description: "Zephyr RTOS build, flash, and monitor workflow for ESP32. Use when: building with west, flashing firmware, monitoring serial output with Zephyr toolchain."
---

# Skill: Zephyr Build & Flash (ESP32)

## Purpose

Manage the Zephyr RTOS build, configuration, flash, and monitor workflow for ESP32 targets.

**When to use:**
- Building a Zephyr project for ESP32 / ESP32-S3 targets
- Configuring Kconfig options via `prj.conf` or `menuconfig`
- Flashing firmware to an ESP32 device
- Monitoring serial output with `west espressif monitor`

**When not to use:**
- ESP-IDF native projects (use `esp32-build-flash` skill instead)
- Arduino or PlatformIO projects
- Non-ESP32 Zephyr targets (adapt board name accordingly)

## Prerequisites

- Zephyr SDK installed (or espressif toolchain via `west espressif install`)
- `west` meta-tool installed: `pip install west`
- Zephyr workspace initialized: `west init` + `west update`
- Python dependencies installed: `pip install -r zephyr/scripts/requirements.txt`
- Serial port available (e.g., `/dev/cu.wchusbserial110` on macOS)

## Steps

> **⛔ All build, flash, and monitor commands MUST be executed via tmux.**
> See `.github/skills/tmux-multi-shell/SKILL.md` for session management and sentinel mode.
> Never run `west build`, `west flash`, or monitor commands outside of tmux.

### 1. Environment Setup

```bash
# Option A: Using Zephyr workspace (recommended)
cd ~/zephyrproject
source zephyr/zephyr-env.sh

# Option B: Set ZEPHYR_BASE manually
export ZEPHYR_BASE=~/zephyrproject/zephyr

# Verify west is available
west --version
```

### 2. Project Configuration

```bash
# prj.conf is the main Kconfig configuration file
# Edit prj.conf to enable/disable features:
cat prj.conf

# Interactive menuconfig (optional)
west build -t menuconfig
```

Key Kconfig options for ESP32-CAM with Zephyr:

```ini
# prj.conf example for ESP32 WiFi + Camera project
CONFIG_WIFI=y
CONFIG_WIFI_ESP32=y
CONFIG_NET_L2_WIFI_MGMT=y
CONFIG_ESP_SPIRAM=y
CONFIG_HEAP_MEM_POOL_SIZE=131072
CONFIG_MAIN_STACK_SIZE=8192
CONFIG_NET_SOCKETS=y
CONFIG_NET_TCP=y
CONFIG_HTTP_SERVER=y
CONFIG_LOG=y
```

### 3. Build

```bash
# Build for ESP32 target (adjust board name as needed)
west build -b esp32s3_devkitc/esp32s3/procpu -p auto

# Clean build (when switching boards or major config changes)
west build -b esp32s3_devkitc/esp32s3/procpu -p always

# Build with extra CMake args
west build -b esp32s3_devkitc/esp32s3/procpu -- -DOVERLAY_CONFIG=overlay-debug.conf
```

**Build output location**: `build/zephyr/zephyr.bin`

### 4. Flash

```bash
# Flash via west (uses esptool or openocd)
west flash

# Flash with explicit serial port
west flash --esp-device /dev/cu.wchusbserial110

# If flash fails, try holding BOOT button during flash
# Or use esptool directly:
esptool.py --chip esp32 -p /dev/cu.wchusbserial110 -b 460800 write_flash -z 0x0 build/zephyr/zephyr.bin
```

### 5. Monitor Serial Output

```bash
# Option A: west espressif monitor (recommended for ESP32)
west espressif monitor

# Option B: minicom
minicom -D /dev/cu.wchusbserial110 -b 115200

# Option C: screen
screen /dev/cu.wchusbserial110 115200
```

### 6. Full Build-Flash-Monitor Cycle (via tmux)

```bash
# In tmux build window:
tmux_exec "espcam:build" "west build -b esp32s3_devkitc/esp32s3/procpu -p auto" 300

# Check build result
tmux_exec "espcam:build" "west flash" 120

# Start monitor in monitor window
tmux send-keys -t espcam:monitor C-c ; sleep 1
tmux send-keys -t espcam:monitor "west espressif monitor" C-m
sleep 8
tmux capture-pane -t espcam:monitor -p -S -500 | tail -50
```

## Common Issues

| Issue | Solution |
|-------|----------|
| `west: command not found` | `pip install west` and ensure `~/.local/bin` is in PATH |
| `ZEPHYR_BASE not set` | `source ~/zephyrproject/zephyr/zephyr-env.sh` |
| `Board not found` | Check `west boards \| grep esp32` for available board names |
| Flash fails with timeout | Hold BOOT button during flash; check USB connection |
| PSRAM not detected | Ensure `CONFIG_ESP_SPIRAM=y` in prj.conf |
| Camera driver missing | May need ESP32 camera HAL wrapper for Zephyr |

## Self-Test

```bash
#!/bin/bash
# Run: bash .github/skills/zephyr-build-flash/self-test.sh

PASS=0; FAIL=0; SKIP=0

check() {
  local name=$1 cmd=$2 skip_msg=$3
  if eval "$cmd" > /dev/null 2>&1; then
    echo "  ✅ SELF_TEST_PASS: $name"
    ((PASS++))
  elif [[ -n "$skip_msg" ]]; then
    echo "  ⏭️  SELF_TEST_SKIP: $name ($skip_msg)"
    ((SKIP++))
  else
    echo "  ❌ SELF_TEST_FAIL: $name"
    ((FAIL++))
  fi
}

# Check west tool
if command -v west > /dev/null 2>&1; then
  check "west_installed" "west --version"
else
  echo "  ⏭️  SELF_TEST_SKIP: west_installed (pip install west)"
  ((SKIP++))
fi

# Check ZEPHYR_BASE
if [[ -n "$ZEPHYR_BASE" && -d "$ZEPHYR_BASE" ]]; then
  check "zephyr_base" "true"
else
  echo "  ⏭️  SELF_TEST_SKIP: zephyr_base (ZEPHYR_BASE not set)"
  ((SKIP++))
fi

# Check cmake
check "cmake" "command -v cmake"

# Check ninja
check "ninja" "command -v ninja"

# Check python3
check "python3" "command -v python3"

echo ""
echo "Results: $PASS pass, $FAIL fail, $SKIP skip"
[[ $FAIL -eq 0 ]]
```

## Blind Test

**Scenario:**
A new AI Agent needs to set up and build a Zephyr project for an ESP32 board.

**Test Prompt:**
```
You are an AI development assistant. Read this Skill, then:
1. Check if west and Zephyr environment are available
2. Create a minimal Zephyr application (main.c that prints "Hello World" via LOG_INF)
3. Build it for an ESP32 target
4. Report the build result and firmware size
5. Clean up temporary files
```

**Acceptance Criteria:**
- [ ] Agent checks for west and ZEPHYR_BASE before building
- [ ] Agent creates proper CMakeLists.txt and prj.conf for Zephyr
- [ ] Agent uses `west build` with correct board target
- [ ] Agent reports build success or failure with details

**Common Failure Modes:**
- Agent uses `idf.py build` instead of `west build` → Skill must clearly distinguish from ESP-IDF
- Agent omits `-b <board>` flag → Build fails with no target
- Agent doesn't source zephyr-env.sh → ZEPHYR_BASE not set

## Success Criteria

- `west build` completes without errors for the target board
- `build/zephyr/zephyr.bin` exists with non-zero size
- Serial monitor shows expected boot output after flash
