---
name: cdp-web-inspector
description: "Chrome DevTools Protocol browser automation with Patchright (anti-detect Playwright). Use when: automating Chrome, inspecting web pages, extracting DOM data, taking screenshots."
---

# Skill: CDP Browser Inspector

## Purpose

Automate browser operations via Chrome DevTools Protocol (CDP) to inspect Web UIs, scrape page data, and verify front-end functionality. Uses Patchright (an anti-detect Playwright fork) as the underlying driver.

**When to use:**
- Need to automate access to a device Web UI (e.g., ESP-CAM video stream page)
- Need to extract data from a web page (e.g., sensor readings)
- Need to verify that a web front-end is working correctly
- Need to take screenshots or record web page state

**When not to use:**
- Pure API testing (use curl/httpie directly)
- Projects that don't involve a Web UI

## Prerequisites

- Python virtual environment: `~/patchright-env`
- Patchright installed: `pip install patchright`
- Browser driver installed: `python -m patchright install chromium`

## Steps

### 1. Launch a Browser Instance

Create a launch script `tools/start-browser.py`:

```python
from patchright.sync_api import sync_playwright
import os, signal, time

USER_DATA_DIR = os.path.expanduser("~/.patchright-userdata")

pw = sync_playwright().start()
context = pw.chromium.launch_persistent_context(
    user_data_dir=USER_DATA_DIR,
    channel="chrome",
    headless=False,
    no_viewport=True,
    args=[
        "--remote-debugging-port=9222",  # Enable CDP port
    ],
)
page = context.pages[0] if context.pages else context.new_page()

print(f"Browser launched, CDP port: 9222")
print(f"User data directory: {USER_DATA_DIR}")

# Keep running
signal.signal(signal.SIGINT, lambda *_: None)
signal.signal(signal.SIGTERM, lambda *_: None)
try:
    while True:
        time.sleep(3600)
except (KeyboardInterrupt, SystemExit):
    context.close()
    pw.stop()
```

### 2. Connect to the Device Web UI

```python
# Connect in a test script
from patchright.sync_api import sync_playwright

pw = sync_playwright().start()
browser = pw.chromium.connect_over_cdp("http://localhost:9222")
context = browser.contexts[0]
page = context.pages[0]

# Navigate to the device page
page.goto("http://{{DEVICE_IP}}/")
page.wait_for_load_state("networkidle")
```

### 3. Page Data Extraction

```python
# Get page title
title = page.title()

# Get sensor data
temperature = page.locator("#temperature").text_content()
humidity = page.locator("#humidity").text_content()

# Save a screenshot
page.screenshot(path="screenshots/device-ui.png")

# Check if the video stream has loaded
video_element = page.locator("img#stream, video#stream")
is_streaming = video_element.is_visible()
```

### 4. Automated Verification

```python
# Verify page elements exist
assert page.locator("#stream").is_visible(), "Video stream not displayed"
assert page.locator("#temperature").is_visible(), "Temperature data not displayed"
assert page.locator("#humidity").is_visible(), "Humidity data not displayed"

# Verify data format
temp_text = page.locator("#temperature").text_content()
assert "°C" in temp_text, f"Unexpected temperature format: {temp_text}"
```

## Notes

- **Must use Patchright** instead of Playwright to avoid being detected as an automation tool
- **Must use a persistent context** (`launch_persistent_context`)
- Do not set a custom user_agent
- The default CDP port is 9222; make sure it doesn't conflict with other services
- **Threading note**: The Patchright sync API uses greenlets (thread-local); `page.evaluate()` cannot be called across threads
- For multi-threaded usage, create an independent async CDP connection and use `asyncio.run_coroutine_threadsafe()`

## Self-Test

> Verify the Patchright installation, browser driver, and CDP connection capability.

### Self-Test Steps

```bash
# Test 1: Patchright is importable
python3 -c "from patchright.sync_api import sync_playwright; print('SELF_TEST_PASS: patchright_import')" 2>/dev/null || echo "SELF_TEST_FAIL: patchright_import"

# Test 2: Browser driver exists
ls ~/Library/Caches/ms-playwright/chromium-* &>/dev/null && \
  echo "SELF_TEST_PASS: chromium_driver" || echo "SELF_TEST_FAIL: chromium_driver"

# Test 3: Can launch a browser (visible mode verification)
python3 -c "
from patchright.sync_api import sync_playwright
import os
pw = sync_playwright().start()
ctx = pw.chromium.launch_persistent_context(
    user_data_dir=os.path.expanduser("~/.patchright-userdata/selftest"),
    channel='chrome',
    headless=False,  # ⛔ headless is forbidden
    no_viewport=True,
)
page = ctx.pages[0] if ctx.pages else ctx.new_page()
page.goto('data:text/html,<h1>test</h1>')
assert page.locator('h1').text_content() == 'test'
ctx.close()
pw.stop()
print('SELF_TEST_PASS: browser_launch')
" 2>/dev/null || echo "SELF_TEST_FAIL: browser_launch"
```

### Expected Results

| Test Item | Expected Output | Failure Impact |
|-----------|----------------|----------------|
| patchright_import | `SELF_TEST_PASS` | CDP functionality completely unavailable |
| chromium_driver | `SELF_TEST_PASS` | Cannot launch browser |
| browser_launch | `SELF_TEST_PASS` | Browser automation fails |

### Blind Test

**Test Prompt:**
```
You are an AI development assistant. Read this Skill, then:
1. Launch a Patchright browser instance (must use visible mode headless=False)
2. Navigate to https://example.com
3. Extract the page title and <h1> text
4. Save a screenshot to /tmp/cdp-test.png
5. Close the browser
Report the result of each step.
```

**Acceptance Criteria:**
- [ ] Agent uses Patchright (not Playwright)
- [ ] Agent uses `launch_persistent_context` (not `launch`)
- [ ] Agent successfully extracts page content
- [ ] Agent does not set a custom user_agent

**Common Failure Modes:**
- Agent uses `playwright` instead of `patchright` → Need to emphasize more in the Skill
- Agent uses `browser.launch()` instead of `launch_persistent_context` → Already noted in the Notes section

## Success Criteria

- [ ] Browser can connect via CDP
- [ ] Can access the target device Web UI
- [ ] Can extract data from the page (sensor readings, etc.)
- [ ] Can verify the presence and state of page elements
- [ ] Screenshot functionality works correctly
