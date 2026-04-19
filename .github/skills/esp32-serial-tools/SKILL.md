---
name: esp32-serial-tools
description: "ESP32 serial communication and log monitoring: UART, log parsing, pattern matching. Use when: reading serial output, parsing ESP32 logs, debugging boot issues."
---

# Skill: ESP32 Serial Tools

## Purpose

Manage serial communication with ESP32 devices, including log monitoring, data parsing, and error detection.

**When to use:**
- Need to monitor serial output from an ESP32 device
- Need to parse key information from serial logs
- Need to detect runtime errors (panic, assert, watchdog)
- Need to send commands to the device via serial

**When not to use:**
- Projects that don't involve serial communication
- Scenarios that only require web interface interaction

## Prerequisites

- ESP-IDF installed with environment variables configured
- USB serial driver installed (CP2102/CH340)
- Device connected via USB

## Steps

### 1. Identify Serial Device

```bash
# macOS
ls /dev/tty.usb* /dev/cu.usb* 2>/dev/null

# Linux
ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null

# Typical result: /dev/ttyUSB0 or /dev/cu.usbserial-xxx
```

### 2. Start Serial Monitor

```bash
# Method 1: Use idf.py monitor (recommended)
idf.py -p /dev/ttyUSB0 monitor

# Method 2: Use minicom
minicom -D /dev/ttyUSB0 -b 115200

# Method 3: Use screen
screen /dev/ttyUSB0 115200
```

### 3. Log Capture and Analysis

In a tmux environment, you can automatically capture and analyze logs:

```bash
# Capture latest output from monitor window
tmux capture-pane -t {{PROJECT_NAME}}:monitor -p | tail -50

# Check for errors
tmux capture-pane -t {{PROJECT_NAME}}:monitor -p | grep -iE "(error|panic|assert|abort|watchdog)"

# Check WiFi connection status
tmux capture-pane -t {{PROJECT_NAME}}:monitor -p | grep -iE "(wifi|connected|ip addr|got ip)"

# Check HTTP server status
tmux capture-pane -t {{PROJECT_NAME}}:monitor -p | grep -iE "(httpd|server|listening|port)"
```

### 4. Common Log Pattern Recognition

| Log Pattern | Meaning | How to Handle |
|---------|------|---------|
| `Guru Meditation Error` | CPU exception (panic) | Check backtrace, locate crashing code |
| `Task watchdog got triggered` | Task watchdog timeout | Check for infinite loops or blocking |
| `Wi-Fi connected` | WiFi connected | Normal, note the IP address |
| `httpd_start: Started` | HTTP server started | Check port number |
| `cam_hal: cam_dma_config` | Camera initialization | Check resolution and frame rate |
| `ENOMEM` / `alloc failed` | Out of memory | Lower resolution or reduce buffers |

### 5. Serial Data Extraction Script

```python
import serial
import re
import time

def monitor_serial(port="/dev/ttyUSB0", baudrate=115200, timeout=30):
    """Monitor serial output and extract key information"""
    results = {
        "ip_address": None,
        "errors": [],
        "wifi_connected": False,
        "http_started": False,
    }

    ser = serial.Serial(port, baudrate, timeout=1)
    start = time.time()

    while time.time() - start < timeout:
        line = ser.readline().decode("utf-8", errors="ignore").strip()
        if not line:
            continue

        # Extract IP address
        ip_match = re.search(r"got ip:(\d+\.\d+\.\d+\.\d+)", line)
        if ip_match:
            results["ip_address"] = ip_match.group(1)

        # Check WiFi connection
        if "wifi connected" in line.lower() or "sta_connected" in line.lower():
            results["wifi_connected"] = True

        # Check HTTP service
        if "httpd" in line.lower() and "start" in line.lower():
            results["http_started"] = True

        # Collect errors
        if re.search(r"error|panic|assert|abort", line, re.IGNORECASE):
            results["errors"].append(line)

    ser.close()
    return results
```

## Self-Test

> Verify that serial tools and the Python serial library are available.

### Self-Test Steps

```bash
# Test 1: pyserial importable
python3 -c "import serial; print('SELF_TEST_PASS: pyserial')" 2>/dev/null || echo "SELF_TEST_FAIL: pyserial"

# Test 2: Serial device detection
PORTS=$(ls /dev/tty.usb* /dev/cu.usb* /dev/ttyUSB* /dev/ttyACM* 2>/dev/null)
if [ -n "$PORTS" ]; then
    echo "SELF_TEST_PASS: serial_device ($PORTS)"
else
    echo "SELF_TEST_WARN: serial_device (no device connected, hardware required)"
fi

# Test 3: idf.py monitor command exists (requires ESP-IDF environment)
command -v idf.py &>/dev/null && echo "SELF_TEST_PASS: idf_monitor" || echo "SELF_TEST_WARN: idf_monitor (ESP-IDF not loaded)"

# Test 4: Log pattern matching logic validation
python3 -c "
import re
test_lines = [
    'I (1234) wifi: got ip:192.168.1.100',
    'E (5678) app: Guru Meditation Error',
    'I (9999) httpd: httpd_start: Started',
]
for line in test_lines:
    if re.search(r'got ip:(\d+\.\d+\.\d+\.\d+)', line):
        print(f'  IP extracted: {re.search(r"got ip:(\d+\.\d+\.\d+\.\d+)", line).group(1)}')
    if re.search(r'error|panic|assert|abort', line, re.IGNORECASE):
        print(f'  Error detected: {line.strip()}')
print('SELF_TEST_PASS: pattern_matching')
" || echo "SELF_TEST_FAIL: pattern_matching"
```

### Expected Results

| Test Item | Expected Output | Failure Impact |
|--------|---------|----------|
| pyserial | `SELF_TEST_PASS` | Python serial scripts unavailable |
| serial_device | `SELF_TEST_PASS/WARN` | Hardware connection required |
| idf_monitor | `SELF_TEST_PASS/WARN` | Can use minicom as alternative |
| pattern_matching | `SELF_TEST_PASS` | Log analysis functionality broken |

### Blind Test

**Test Prompt:**
```
You are an AI development assistant. Please read this Skill, then:
1. Check if any serial devices are connected to the current system
2. Write a Python script that parses the following simulated serial output and extracts key information:
   - "I (1000) wifi: connected to ap SSID:MyWiFi"
   - "I (2000) wifi: got ip:192.168.1.50"
   - "I (3000) httpd: httpd_start: Started on port 80"
   - "E (4000) cam: cam_hal: failed to init"
3. Output the extracted IP address, WiFi status, HTTP status, and error list
```

**Acceptance Criteria:**
- [ ] Agent correctly uses regex patterns from the Skill
- [ ] Agent extracts IP: 192.168.1.50
- [ ] Agent identifies the cam_hal error
- [ ] Agent does not miss the httpd startup information

## Success Criteria

- [ ] Can correctly identify and connect to serial devices
- [ ] Serial log output is visible in real time
- [ ] Can automatically detect common error patterns
- [ ] Can extract key information (IP address, service status, etc.)
