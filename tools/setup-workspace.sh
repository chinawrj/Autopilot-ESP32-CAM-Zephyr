#!/usr/bin/env bash
# setup-workspace.sh — Initialize Zephyr workspace and apply necessary patches
# Run this after cloning the repo to set up the development environment.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$REPO_ROOT"

echo "=== Autopilot-ESP32-CAM-Zephyr Workspace Setup ==="

# Step 1: Initialize west workspace
if [ ! -d ".west" ]; then
    echo "[1/5] Initializing west workspace..."
    west init -l manifest
else
    echo "[1/5] West workspace already initialized, skipping."
fi

# Step 2: Update modules
echo "[2/5] Updating west modules (this may take a while)..."
west update

# Step 3: Fetch ESP32 binary blobs
echo "[3/5] Fetching ESP32 binary blobs..."
west blobs fetch hal_espressif

# Step 4: Install Python requirements
echo "[4/5] Installing Python requirements..."
pip3 install -r zephyr/scripts/requirements.txt --quiet --break-system-packages 2>/dev/null || \
pip3 install -r zephyr/scripts/requirements.txt --quiet

# Step 5: Patch xtensa CMakeLists.txt for ESP32 core-isa.h include path
# This fixes a mismatch in Zephyr v4.1.0 where the build system references
# ZEPHYR_XTENSA_MODULE_DIR but the module system sets ZEPHYR_HAL_XTENSA_MODULE_DIR.
# Additionally, the Espressif toolchain (esp-14.2.0) uses #include_next for core-isa.h
# which requires the ESP HAL's xtensa include path.
XTENSA_CMAKE="zephyr/arch/xtensa/core/CMakeLists.txt"
if [ -f "$XTENSA_CMAKE" ]; then
    if grep -q 'ZEPHYR_XTENSA_MODULE_DIR' "$XTENSA_CMAKE"; then
        echo "[5/5] Patching xtensa CMakeLists.txt for ESP32 core-isa.h..."
        sed -i.bak \
            -e 's|\${ZEPHYR_XTENSA_MODULE_DIR}|\${ZEPHYR_HAL_XTENSA_MODULE_DIR}|g' \
            "$XTENSA_CMAKE"
        # Add Espressif HAL xtensa include path after each HAL_XTENSA include
        sed -i.bak \
            -e '/ZEPHYR_HAL_XTENSA_MODULE_DIR.*include\/zephyr/a\
      -I${ZEPHYR_HAL_ESPRESSIF_MODULE_DIR}/components/xtensa/${CONFIG_SOC_SERIES}/include' \
            "$XTENSA_CMAKE"
        rm -f "${XTENSA_CMAKE}.bak"
        echo "  Patched successfully."
    else
        echo "[5/5] Xtensa CMakeLists.txt already patched, skipping."
    fi
else
    echo "[5/5] WARNING: $XTENSA_CMAKE not found. Run 'west update' first."
fi

echo ""
echo "=== Setup complete ==="
echo ""
echo "To build:"
echo "  source zephyr/zephyr-env.sh"
echo "  export ZEPHYR_TOOLCHAIN_VARIANT=espressif"
echo "  export ESPRESSIF_TOOLCHAIN_PATH=\$HOME/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf"
echo "  west build -b esp32_devkitc_wrover/esp32/procpu"
