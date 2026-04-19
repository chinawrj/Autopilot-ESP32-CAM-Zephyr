#!/bin/bash
# Zephyr Build & Flash — Self-Test
# Run: bash .github/skills/zephyr-build-flash/self-test.sh

PASS=0; FAIL=0; SKIP=0

check() {
  local name=$1 cmd=$2 skip_msg=${3:-}
  if eval "$cmd" > /dev/null 2>&1; then
    echo "  ✅ SELF_TEST_PASS: $name"
    PASS=$((PASS+1))
  elif [[ -n "$skip_msg" ]]; then
    echo "  ⏭️  SELF_TEST_SKIP: $name ($skip_msg)"
    SKIP=$((SKIP+1))
  else
    echo "  ❌ SELF_TEST_FAIL: $name"
    FAIL=$((FAIL+1))
  fi
}

# Check west tool
if command -v west > /dev/null 2>&1; then
  check "west_installed" "west --version"
else
  echo "  ⏭️  SELF_TEST_SKIP: west_installed (pip install west)"
  SKIP=$((SKIP+1))
fi

# Check ZEPHYR_BASE
if [[ -n "${ZEPHYR_BASE:-}" && -d "${ZEPHYR_BASE:-}" ]]; then
  check "zephyr_base" "true"
else
  echo "  ⏭️  SELF_TEST_SKIP: zephyr_base (ZEPHYR_BASE not set or dir missing)"
  SKIP=$((SKIP+1))
fi

# Check Zephyr SDK or espressif toolchain
if [[ -n "${ZEPHYR_SDK_INSTALL_DIR:-}" ]] || command -v xtensa-esp32-elf-gcc > /dev/null 2>&1; then
  check "toolchain" "true"
else
  echo "  ⏭️  SELF_TEST_SKIP: toolchain (Zephyr SDK or ESP32 toolchain not found)"
  SKIP=$((SKIP+1))
fi

# Check cmake (always available on dev machines)
check "cmake" "command -v cmake"

# Check ninja
check "ninja" "command -v ninja"

# Check python3
check "python3" "command -v python3"

echo ""
echo "Results: $PASS pass, $FAIL fail, $SKIP skip"
[[ $FAIL -eq 0 ]]
