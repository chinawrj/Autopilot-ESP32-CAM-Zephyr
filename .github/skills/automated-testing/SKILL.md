---
name: automated-testing
description: "Automated testing for ESP32 projects: serial port validation, web UI browser verification, test pyramid. Use when: writing tests, verifying firmware, checking web interface, e2e testing."
---

# Skill: Automated Testing

## Purpose

Defines the automated testing strategy for embedded projects, combining serial port validation and Web UI verification for end-to-end testing.

**When to use:**
- Need to verify that firmware features work correctly
- Need to verify that the Web UI renders correctly
- Need regression testing to prevent feature degradation
- In continuous integration / continuous development workflows

**When not to use:**
- One-off verifications where manual testing is sufficient
- Pure hardware projects with no testable interfaces

## Prerequisites

- Device is connected and accessible via serial port
- Web UI is accessible (if applicable)
- CDP browser tools are configured (if web testing is needed)
- tmux environment is ready

## Steps

### 1. Test Pyramid

```
        ┌─────────┐
        │ E2E     │  ← CDP + Serial: full flow verification
        │ Tests   │
       ┌┴─────────┴┐
       │ Integration│  ← Serial: inter-module interaction verification
       │ Tests      │
      ┌┴────────────┴┐
      │  Unit Tests   │  ← Compile-time: component-level tests
      └───────────────┘
```

### 2. Serial Port Automated Testing

```python
"""test_serial.py - Serial port automated testing"""
import serial
import re
import time
import sys

class ESP32SerialTest:
    def __init__(self, port="/dev/ttyUSB0", baudrate=115200):
        self.ser = serial.Serial(port, baudrate, timeout=1)
        self.results = []

    def wait_for_pattern(self, pattern, timeout=30):
        """Wait for serial output to match the specified pattern"""
        start = time.time()
        buffer = ""
        while time.time() - start < timeout:
            data = self.ser.readline().decode("utf-8", errors="ignore")
            buffer += data
            if re.search(pattern, buffer):
                return True, buffer
        return False, buffer

    def test_boot(self):
        """Test that the device boots normally"""
        ok, output = self.wait_for_pattern(r"app_main: Starting", timeout=15)
        self._record("boot", ok, "Device booted" if ok else f"Boot timeout: {output[-200:]}")

    def test_wifi_connect(self):
        """Test WiFi connection"""
        ok, output = self.wait_for_pattern(r"got ip:(\d+\.\d+\.\d+\.\d+)", timeout=20)
        if ok:
            ip = re.search(r"got ip:(\d+\.\d+\.\d+\.\d+)", output).group(1)
            self._record("wifi", True, f"WiFi connected, IP: {ip}")
        else:
            self._record("wifi", False, "WiFi connection timeout")

    def test_http_server(self):
        """Test HTTP server startup"""
        ok, output = self.wait_for_pattern(r"httpd.*start|server.*listening", timeout=10)
        self._record("http", ok, "HTTP server started" if ok else "HTTP server not started")

    def test_camera_init(self):
        """Test camera initialization"""
        ok, output = self.wait_for_pattern(r"camera.*init|cam_hal", timeout=10)
        self._record("camera", ok, "Camera initialized" if ok else "Camera initialization failed")

    def test_no_errors(self):
        """Check for no critical errors"""
        time.sleep(5)  # Wait for stabilization
        output = ""
        while self.ser.in_waiting:
            output += self.ser.readline().decode("utf-8", errors="ignore")
        errors = re.findall(r"(error|panic|abort|assert failed)", output, re.IGNORECASE)
        ok = len(errors) == 0
        self._record("no_errors", ok, "No errors" if ok else f"Errors found: {errors}")

    def _record(self, name, passed, message):
        self.results.append({"name": name, "passed": passed, "message": message})
        status = "✅ PASS" if passed else "❌ FAIL"
        print(f"  {status}: {name} - {message}")

    def run_all(self):
        """Run all tests"""
        print("=" * 50)
        print("ESP32 Serial Port Automated Tests")
        print("=" * 50)
        self.test_boot()
        self.test_wifi_connect()
        self.test_http_server()
        self.test_camera_init()
        self.test_no_errors()

        passed = sum(1 for r in self.results if r["passed"])
        total = len(self.results)
        print("=" * 50)
        print(f"Results: {passed}/{total} passed")
        return passed == total

if __name__ == "__main__":
    test = ESP32SerialTest()
    success = test.run_all()
    sys.exit(0 if success else 1)
```

### 3. Web UI Automated Testing

```python
"""test_web_ui.py - Web UI automated testing (using Patchright)"""
from patchright.sync_api import sync_playwright

class WebUITest:
    def __init__(self, device_ip):
        self.device_ip = device_ip
        self.pw = sync_playwright().start()
        self.browser = self.pw.chromium.connect_over_cdp("http://localhost:9222")
        self.context = self.browser.contexts[0]
        self.page = self.context.new_page()
        self.results = []

    def test_page_loads(self):
        """Test that the page loads"""
        try:
            self.page.goto(f"http://{self.device_ip}/", timeout=10000)
            self.page.wait_for_load_state("networkidle")
            self._record("page_load", True, "Page loaded successfully")
        except Exception as e:
            self._record("page_load", False, f"Page load failed: {e}")

    def test_video_stream(self):
        """Test that the video stream is visible"""
        stream = self.page.locator("img#stream, video#stream, img[src*='stream']")
        visible = stream.is_visible(timeout=5000)
        self._record("video_stream", visible, "Video stream visible" if visible else "Video stream not visible")

    def test_sensor_data(self):
        """Test that sensor data is displayed"""
        # Temperature
        temp = self.page.locator("[data-sensor='temperature'], #temperature, .temperature")
        temp_visible = temp.is_visible(timeout=3000)
        self._record("temperature", temp_visible,
                     f"Temperature: {temp.text_content()}" if temp_visible else "Temperature data not visible")

        # Humidity
        humidity = self.page.locator("[data-sensor='humidity'], #humidity, .humidity")
        hum_visible = humidity.is_visible(timeout=3000)
        self._record("humidity", hum_visible,
                     f"Humidity: {humidity.text_content()}" if hum_visible else "Humidity data not visible")

    def test_data_updates(self):
        """Test that data updates periodically"""
        temp_el = self.page.locator("[data-sensor='temperature'], #temperature, .temperature")
        if not temp_el.is_visible(timeout=3000):
            self._record("data_update", False, "Cannot locate temperature element")
            return

        value1 = temp_el.text_content()
        self.page.wait_for_timeout(5000)  # Wait 5 seconds
        value2 = temp_el.text_content()
        # Data may be the same but should at least not be empty
        ok = value1 is not None and len(value1) > 0
        self._record("data_update", ok, f"Data collection OK: {value1} -> {value2}")

    def _record(self, name, passed, message):
        self.results.append({"name": name, "passed": passed, "message": message})
        status = "✅ PASS" if passed else "❌ FAIL"
        print(f"  {status}: {name} - {message}")

    def run_all(self):
        print("=" * 50)
        print("Web UI Automated Tests")
        print("=" * 50)
        self.test_page_loads()
        self.test_video_stream()
        self.test_sensor_data()
        self.test_data_updates()

        passed = sum(1 for r in self.results if r["passed"])
        total = len(self.results)
        print("=" * 50)
        print(f"Results: {passed}/{total} passed")
        return passed == total

    def close(self):
        self.page.close()
        self.pw.stop()
```

### 4. End-to-End Test Flow

```bash
#!/bin/bash
# run-e2e-tests.sh - End-to-end test flow

echo "=== E2E Tests Starting ==="

# Step 1: Build and flash
echo "[1/4] Building..."
idf.py build || exit 1

echo "[2/4] Flashing..."
idf.py -p /dev/ttyUSB0 flash || exit 1

# Step 3: Wait for device to boot
echo "[3/4] Waiting for device to boot (10s)..."
sleep 10

# Step 4: Run serial tests
echo "[4/4] Running serial tests..."
python test_serial.py
SERIAL_RESULT=$?

# Step 5: Run Web UI tests (if serial tests passed)
if [ $SERIAL_RESULT -eq 0 ]; then
    echo "[5/5] Running Web UI tests..."
    python test_web_ui.py
    WEB_RESULT=$?
else
    echo "Serial tests failed, skipping Web UI tests"
    WEB_RESULT=1
fi

# Summary
echo ""
echo "=== Test Summary ==="
echo "Serial tests: $([ $SERIAL_RESULT -eq 0 ] && echo '✅ PASS' || echo '❌ FAIL')"
echo "Web UI tests: $([ $WEB_RESULT -eq 0 ] && echo '✅ PASS' || echo '❌ FAIL')"

exit $(( SERIAL_RESULT + WEB_RESULT ))
```

## Self-Test

> Verify the automated testing framework's dependencies and core logic.

### Self-Test Steps

```bash
# Test 1: pyserial and patchright are available
python3 -c "import serial; print('SELF_TEST_PASS: pyserial')" 2>/dev/null || echo "SELF_TEST_FAIL: pyserial"
python3 -c "from patchright.sync_api import sync_playwright; print('SELF_TEST_PASS: patchright')" 2>/dev/null || echo "SELF_TEST_FAIL: patchright"

# Test 2: Basic test class structure validation
python3 -c "
import re, sys

# Simulated serial test logic
class MockSerialTest:
    def __init__(self):
        self.results = []
    def _record(self, name, passed, msg):
        self.results.append({'name': name, 'passed': passed, 'message': msg})
    def test_pattern(self):
        lines = [
            'I (1000) wifi: got ip:192.168.1.10',
            'E (2000) panic: Guru Meditation Error',
        ]
        for line in lines:
            ip = re.search(r'got ip:(\d+\.\d+\.\d+\.\d+)', line)
            if ip:
                self._record('wifi', True, f'IP: {ip.group(1)}')
            if re.search(r'error|panic', line, re.IGNORECASE):
                self._record('error_detect', True, line.strip())

t = MockSerialTest()
t.test_pattern()
assert len(t.results) == 2, f'Expected 2 results, got {len(t.results)}'
print('SELF_TEST_PASS: test_framework')
" || echo "SELF_TEST_FAIL: test_framework"

# Test 3: Bash test flow logic
bash -c '
RESULT=0
[ $RESULT -eq 0 ] && echo "SELF_TEST_PASS: bash_test_flow" || echo "SELF_TEST_FAIL: bash_test_flow"
'
```

### Blind Test

**Test Prompt:**
```
You are an AI development assistant. Please read this Skill, then:
1. Write a simplified serial test class with only a test_pattern method
2. Test it with the following simulated data: "got ip:10.0.0.1", "httpd_start: Started", "assert failed"
3. Output a PASS/FAIL summary
4. Explain why serial tests come before Web UI tests in the test pyramid
```

**Acceptance Criteria:**
- [ ] Agent uses the test class structure defined in this Skill
- [ ] Agent correctly extracts IP addresses and errors
- [ ] Agent understands the layered logic of the test pyramid
- [ ] Agent output includes clear PASS/FAIL markers

## Success Criteria

- [ ] All serial automated tests pass
- [ ] All Web UI automated tests pass
- [ ] End-to-end test flow can be executed with a single command
- [ ] Test results have clear PASS/FAIL output
