#!/bin/bash
# self-test for board-pinout-reference
# Run: bash .github/skills/board-pinout-reference/self-test.sh

PASS=0
FAIL=0
SKILL=".github/skills/board-pinout-reference/SKILL.md"

# Change to project root directory
cd "$(dirname "$0")/../../.." || exit 1

test_case() {
    local name=$1
    shift
    if "$@" 2>/dev/null; then
        echo "SELF_TEST_PASS: $name"
        PASS=$((PASS + 1))
    else
        echo "SELF_TEST_FAIL: $name"
        FAIL=$((FAIL + 1))
    fi
}

test_grep() {
    local name=$1
    local pattern=$2
    if grep -qE "$pattern" "$SKILL" 2>/dev/null; then
        echo "SELF_TEST_PASS: $name"
        PASS=$((PASS + 1))
    else
        echo "SELF_TEST_FAIL: $name"
        FAIL=$((FAIL + 1))
    fi
}

test_count() {
    local name=$1
    local pattern=$2
    local expected=$3
    local actual
    actual=$(grep -cE "$pattern" "$SKILL" 2>/dev/null)
    if [[ "$actual" -ge "$expected" ]]; then
        echo "SELF_TEST_PASS: $name (found $actual, expected >= $expected)"
        PASS=$((PASS + 1))
    else
        echo "SELF_TEST_FAIL: $name (found $actual, expected >= $expected)"
        FAIL=$((FAIL + 1))
    fi
}

# --- Structural integrity ---
test_case "skill_md_exists" test -f "$SKILL"
test_grep "has_board_section" "^## Board:"
test_grep "has_self_test" "## Self-Test"
test_grep "has_blind_test" "### Blind Test"
test_grep "has_module_reference" "## Core Module Reference"

# --- YD-ESP32-CAM data completeness ---

# Camera pins: D0-D7, XCLK, PCLK, VSYNC, HREF, SDA, SCL, POWER GPIO = 15
test_count "camera_pins_count" "GPIO[0-9]+" 15

# Verify key GPIO mappings exist
test_grep "cam_d0_gpio5" "D0.*GPIO5"
test_grep "cam_d7_gpio35" "D7.*GPIO35"
test_grep "cam_xclk_gpio0" "XCLK.*GPIO0"
test_grep "cam_vsync_gpio25" "VSYNC.*GPIO25"
test_grep "cam_sda_gpio26" "SDA.*GPIO26"
test_grep "cam_scl_gpio27" "SCL.*GPIO27"
test_grep "cam_pwdn_gpio32" "POWER.*GPIO32"

# TF card (SD) pins
test_grep "sd_clk_gpio14" "CLK.*GPIO14"
test_grep "sd_cmd_gpio15" "CMD.*GPIO15"
test_grep "sd_data0_gpio2" "DATA0.*GPIO2"
test_grep "sd_data1_gpio4" "DATA1.*GPIO4"
test_grep "sd_data2_gpio12" "DATA2.*GPIO12"
test_grep "sd_data3_gpio13" "DATA3.*GPIO13"

# Serial pins
test_grep "uart_tx" "TX.*GPIO1"
test_grep "uart_rx" "RX.*GPIO3"

# Other key pins
test_grep "led_gpio33" "LED.*GPIO33"
test_grep "boot_gpio0" "BOOT.*GPIO0"

# --- GPIO number validity (0-39 for ESP32) ---
# Extract all GPIO numbers and verify they are in the 0-39 range
invalid_gpios=$(grep -oE "GPIO[0-9]+" "$SKILL" | sed 's/GPIO//' | sort -un | awk '$1 > 39')
if [[ -z "$invalid_gpios" ]]; then
    echo "SELF_TEST_PASS: gpio_range_valid (all GPIOs in 0-39)"
    PASS=$((PASS + 1))
else
    echo "SELF_TEST_FAIL: gpio_range_valid (invalid GPIOs: $invalid_gpios)"
    FAIL=$((FAIL + 1))
fi

# --- ESP-IDF code configuration consistency ---
# Verify #define values in code match the pin table
test_grep "code_pwdn_32" "PWDN_GPIO_NUM.*32"
test_grep "code_xclk_0" "XCLK_GPIO_NUM.*0"
test_grep "code_siod_26" "SIOD_GPIO_NUM.*26"
test_grep "code_sioc_27" "SIOC_GPIO_NUM.*27"
test_grep "code_y2_5" "Y2_GPIO_NUM.*5"
test_grep "code_y9_35" "Y9_GPIO_NUM.*35"
test_grep "code_vsync_25" "VSYNC_GPIO_NUM.*25"
test_grep "code_href_23" "HREF_GPIO_NUM.*23"
test_grep "code_pclk_22" "PCLK_GPIO_NUM.*22"

# --- Conflict warnings ---
test_grep "conflict_gpio0" "GPIO0.*XCLK.*BOOT|GPIO0.*BOOT.*XCLK"
test_grep "conflict_gpio4" "GPIO4.*Flash.*SD|GPIO4.*SD.*flash|GPIO4.*flash LED"
test_grep "conflict_gpio12" "GPIO12.*MTDI|GPIO12.*boot|GPIO12.*efuse"
test_grep "input_only_warning" "input-only"

# --- Module parameters ---
test_grep "module_esp32_wrover_e" "ESP32-WROVER-E"
test_grep "module_flash_8mb" "Flash.*8.*MB|8.*MB.*Flash"
test_grep "module_psram_8mb" "PSRAM.*8.*MB|8.*MB.*PSRAM"


# --- PINMUX description ---
test_grep "has_pinmux_section" "PINMUX"
test_grep "has_gpio_matrix" "GPIO Matrix"
test_grep "has_io_mux" "IO MUX"
test_grep "sdmmc_hs2_fixed" "HS2.*fixed|hardware.fixed|cannot.*change"
test_grep "camera_gpio_matrix" "camera.*GPIO Matrix|GPIO Matrix.*camera|board.*PCB.*design"
test_grep "uart_remappable" "UART.*remap|remap.*pin|U0TXD"

# --- Summary ---
echo ""
echo "Results: $PASS passed, $FAIL failed"
exit $FAIL