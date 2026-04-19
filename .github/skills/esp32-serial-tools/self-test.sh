#!/bin/bash
# self-test for esp32-serial-tools
# Run: bash .github/skills/esp32-serial-tools/self-test.sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/../_common/detect-python.sh"

PASS=0
FAIL=0
SKIP=0

test_pass() { echo "SELF_TEST_PASS: $1"; PASS=$((PASS + 1)); }
test_fail() { echo "SELF_TEST_FAIL: $1"; FAIL=$((FAIL + 1)); }
skip_case() { echo "SELF_TEST_SKIP: $1 ($2)"; SKIP=$((SKIP + 1)); }

test_case() {
  local name=$1; shift
  if "$@" 2>/dev/null; then
    test_pass "$name"
  else
    test_fail "$name"
  fi
}

# --- Detect Python with serial ---
PYTHON=$(detect_python "serial")
if [ -n "$PYTHON" ]; then
  test_pass "pyserial_import ($PYTHON)"
else
  skip_case "pyserial_import" "pip install pyserial"
  PYTHON="python3"  # fallback for non-serial tests
fi

# --- Test 2: Serial device detection ---
PORTS=$(ls /dev/tty.usb* /dev/cu.usb* /dev/ttyUSB* /dev/ttyACM* 2>/dev/null)
if [ -n "$PORTS" ]; then
  test_pass "serial_device"
else
  skip_case "serial_device" "no USB serial device connected"
fi

# --- Test 3: idf.py monitor exists ---
if command -v idf.py &>/dev/null; then
  test_pass "idf_monitor"
else
  skip_case "idf_monitor" "ESP-IDF not loaded"
fi

# --- Test 4: Log pattern matching logic ---
test_case "pattern_matching" $PYTHON -c "
import re
test_lines = [
    'I (1234) wifi: got ip:192.168.1.100',
    'E (5678) app: Guru Meditation Error',
    'I (9999) httpd: httpd_start: Started',
    'W (1111) warn: some warning here',
]
ip_found = False
error_found = False
for line in test_lines:
    m = re.search(r'got ip:(\d+\.\d+\.\d+\.\d+)', line)
    if m:
        assert m.group(1) == '192.168.1.100', f'Wrong IP: {m.group(1)}'
        ip_found = True
    if re.search(r'error|panic|assert|abort', line, re.IGNORECASE):
        error_found = True
assert ip_found, 'IP pattern not found'
assert error_found, 'Error pattern not found'
"

# --- Test 5: Log level filter ---
test_case "log_level_filter" $PYTHON -c "
import re
lines = [
    'I (100) tag: info msg',
    'W (200) tag: warn msg',
    'E (300) tag: error msg',
    'D (400) tag: debug msg',
]
levels = {}
for line in lines:
    m = re.match(r'^([IWED]) \(', line)
    if m:
        levels[m.group(1)] = levels.get(m.group(1), 0) + 1
assert levels == {'I': 1, 'W': 1, 'E': 1, 'D': 1}, f'Wrong levels: {levels}'
"

# --- Summary ---
echo ""
echo "Results: $PASS passed, $FAIL failed, $SKIP skipped"
exit $FAIL
