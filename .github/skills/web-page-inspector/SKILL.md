---
name: web-page-inspector
description: "Web page content inspection and data extraction with Patchright. Use when: checking device web UI, extracting sensor data from pages, verifying HTML rendering."
---

# Skill: Web Page Inspection & Data Extraction

## Purpose

Access device web pages via the browser to inspect page structure, extract displayed data, and verify live streaming media.

**When to use:**
- Need to check whether the device web UI renders correctly
- Need to extract sensor data, statistics, etc.
- Need to verify live video/audio stream status
- Need to retrieve dynamically updated content from the page

**When not to use:**
- The device has no web UI
- Only REST API interaction is needed (use curl directly)

## Prerequisites

- CDP browser tool is running (see `cdp-web-inspector` skill)
- Device web server is running
- Device IP address is known

## Steps

### 1. Explore Page Structure

```python
from patchright.sync_api import sync_playwright

def inspect_page(device_ip):
    """Explore the DOM structure of the target page"""
    pw = sync_playwright().start()
    browser = pw.chromium.connect_over_cdp("http://localhost:9222")
    page = browser.contexts[0].new_page()

    page.goto(f"http://{device_ip}/")
    page.wait_for_load_state("networkidle")

    # Get basic page info
    info = {
        "title": page.title(),
        "url": page.url,
    }

    # Get all visible text
    info["text_content"] = page.locator("body").text_content()

    # Get all image elements
    images = page.locator("img").all()
    info["images"] = [
        {"src": img.get_attribute("src"), "id": img.get_attribute("id")}
        for img in images
    ]

    # Get all table data
    tables = page.locator("table").all()
    info["tables"] = []
    for table in tables:
        rows = table.locator("tr").all()
        table_data = []
        for row in rows:
            cells = row.locator("td, th").all()
            table_data.append([cell.text_content() for cell in cells])
        info["tables"].append(table_data)

    page.close()
    pw.stop()
    return info
```

### 2. Sensor Data Extraction

```python
def extract_sensor_data(page):
    """Extract sensor data from the page"""
    data = {}

    # Strategy 1: Find by ID
    selectors = {
        "temperature": "#temperature, [data-sensor='temperature'], .temp-value",
        "humidity": "#humidity, [data-sensor='humidity'], .humidity-value",
        "pressure": "#pressure, [data-sensor='pressure'], .pressure-value",
    }

    for name, selector in selectors.items():
        el = page.locator(selector)
        if el.count() > 0 and el.first.is_visible():
            data[name] = el.first.text_content().strip()

    # Strategy 2: Extract via JavaScript
    js_data = page.evaluate("""() => {
        const result = {};
        // Find elements containing numbers and units
        document.querySelectorAll('[class*="sensor"], [class*="data"], [class*="value"]')
            .forEach(el => {
                const text = el.textContent.trim();
                const id = el.id || el.className;
                if (text && /\\d/.test(text)) {
                    result[id] = text;
                }
            });
        return result;
    }""")
    data["js_extracted"] = js_data

    return data
```

### 3. Video Stream Detection

```python
def check_video_stream(page):
    """Detect whether the video stream is working properly"""
    result = {
        "has_stream": False,
        "stream_type": None,
        "stream_url": None,
        "is_loading": False,
    }

    # Check <img> tag MJPEG streams
    mjpeg = page.locator("img[src*='stream'], img[src*='mjpeg'], img[src*=':81']")
    if mjpeg.count() > 0 and mjpeg.first.is_visible():
        result["has_stream"] = True
        result["stream_type"] = "MJPEG"
        result["stream_url"] = mjpeg.first.get_attribute("src")

    # Check <video> tags
    video = page.locator("video")
    if video.count() > 0 and video.first.is_visible():
        result["has_stream"] = True
        result["stream_type"] = "VIDEO"
        result["stream_url"] = video.first.get_attribute("src")

    # Check canvas (WebRTC, etc.)
    canvas = page.locator("canvas#stream, canvas.video-canvas")
    if canvas.count() > 0 and canvas.first.is_visible():
        result["has_stream"] = True
        result["stream_type"] = "CANVAS"

    # Verify stream is loading (by checking image dimension changes)
    if result["has_stream"] and result["stream_type"] == "MJPEG":
        size1 = page.evaluate("""(sel) => {
            const img = document.querySelector(sel);
            return img ? {w: img.naturalWidth, h: img.naturalHeight} : null;
        }""", mjpeg.first.evaluate("el => el.tagName + (el.id ? '#'+el.id : '')"))
        result["is_loading"] = size1 is not None and size1.get("w", 0) > 0

    return result
```

### 4. Periodic Data Collection

```python
import time
import json

def collect_data_over_time(page, interval=5, duration=60):
    """Periodically collect page data"""
    samples = []
    start = time.time()

    while time.time() - start < duration:
        sample = {
            "timestamp": time.time(),
            "data": extract_sensor_data(page),
            "stream": check_video_stream(page),
        }
        samples.append(sample)
        print(f"[{len(samples)}] {json.dumps(sample['data'], ensure_ascii=False)}")
        time.sleep(interval)

    return samples
```

### 5. Page Screenshots & Reports

```python
def generate_page_report(page, output_dir="reports"):
    """Generate a page inspection report"""
    import os
    os.makedirs(output_dir, exist_ok=True)

    # Full-page screenshot
    page.screenshot(path=f"{output_dir}/full-page.png", full_page=True)

    # Specific area screenshot
    stream_el = page.locator("img#stream, video#stream").first
    if stream_el.is_visible():
        stream_el.screenshot(path=f"{output_dir}/stream.png")

    # Generate text report
    sensor_data = extract_sensor_data(page)
    stream_status = check_video_stream(page)

    report = f"""# Page Inspection Report

## Basic Info
- URL: {page.url}
- Title: {page.title()}
- Time: {time.strftime('%Y-%m-%d %H:%M:%S')}

## Sensor Data
{json.dumps(sensor_data, indent=2, ensure_ascii=False)}

## Video Stream Status
{json.dumps(stream_status, indent=2, ensure_ascii=False)}

## Screenshots
- Full page: full-page.png
- Video stream: stream.png
"""
    with open(f"{output_dir}/report.md", "w") as f:
        f.write(report)

    return report
```

## Self-Test

> Verify page inspection and data extraction capabilities.

### Self-Test Steps

```bash
# Test 1: Patchright available (same as cdp-web-inspector)
python3 -c "from patchright.sync_api import sync_playwright; print('SELF_TEST_PASS: patchright')" 2>/dev/null || echo "SELF_TEST_FAIL: patchright"

# Test 2: Page data extraction logic validation (using local HTML)
python3 -c "
from patchright.sync_api import sync_playwright
import os

html = '''
<html><body>
<h1>Test Device</h1>
<span id='temperature'>25.3</span> °C
<span id='humidity'>60.1</span> %
<img id='stream' src='data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7'>
</body></html>
'''

pw = sync_playwright().start()
ctx = pw.chromium.launch_persistent_context(
    user_data_dir=os.path.expanduser("~/.patchright-userdata/selftest"),
    channel='chrome', headless=False, no_viewport=True,  # ⛔ headless forbidden
)
page = ctx.pages[0] if ctx.pages else ctx.new_page()
page.set_content(html)

assert page.locator('#temperature').text_content() == '25.3'
assert page.locator('#humidity').text_content() == '60.1'
assert page.locator('#stream').is_visible()

ctx.close()
pw.stop()
print('SELF_TEST_PASS: data_extraction')
" 2>/dev/null || echo "SELF_TEST_FAIL: data_extraction"

# Test 3: Screenshot functionality
python3 -c "
from patchright.sync_api import sync_playwright
import os
pw = sync_playwright().start()
ctx = pw.chromium.launch_persistent_context(
    user_data_dir=os.path.expanduser("~/.patchright-userdata/selftest"),
    channel='chrome', headless=False, no_viewport=True)  # ⛔ headless forbidden
page = ctx.pages[0] if ctx.pages else ctx.new_page()
page.set_content('<h1>Screenshot Test</h1>')
page.screenshot(path='/tmp/__selftest_screenshot__.png')
ctx.close(); pw.stop()
import os
assert os.path.getsize('/tmp/__selftest_screenshot__.png') > 0
os.remove('/tmp/__selftest_screenshot__.png')
print('SELF_TEST_PASS: screenshot')
" 2>/dev/null || echo "SELF_TEST_FAIL: screenshot"
```

### Blind Test

**Test Prompt:**
```
You are an AI development assistant. Read this Skill, then:
1. Create a local HTML file simulating a device page with temperature (22.5°C) and humidity (45.0%)
2. Open the page using Patchright
3. Extract the temperature and humidity data
4. Detect whether video stream elements exist
5. Take a screenshot and generate a Markdown inspection report
```

**Acceptance Criteria:**
- [ ] Agent used Patchright instead of Playwright
- [ ] Agent correctly used locators to extract data
- [ ] Agent generated a report containing data and screenshot paths
- [ ] Agent correctly handled the case where video stream elements do not exist

## Success Criteria

- [ ] Can correctly access device web pages
- [ ] Can extract sensor data from pages
- [ ] Can detect video stream status
- [ ] Can periodically collect data
- [ ] Can generate inspection reports with screenshots
