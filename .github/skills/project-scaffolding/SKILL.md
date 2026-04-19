---
name: project-scaffolding
description: "Project scaffolding generator: directory structure, CMakeLists.txt, HTML templates. Use when: creating new ESP32 project, generating boilerplate, initializing project structure."
---

# Skill: Project Scaffolding Generator

## Purpose

Generate initial directory structure, build configuration files (CMakeLists.txt), HTML templates, and basic source code framework for the target project. This is the first hands-on coding step in project development.

**When to use:**
- Project starts from scratch and needs directory and file initialization
- Need a standard ESP-IDF project structure
- Need a compilable "Hello World" starting point

**When not to use:**
- Project already has a code structure
- Developing based on an existing template/example project

## Prerequisites

- Development environment verified via `environment-setup` skill
- ESP-IDF environment loaded
- Target chip model determined

## Steps

### 1. ESP-IDF Project Standard Structure

```
{{PROJECT_NAME}}/
├── CMakeLists.txt              # Top-level build file
├── sdkconfig.defaults          # Default configuration
├── partitions.csv              # Partition table (needed for large projects)
├── main/
│   ├── CMakeLists.txt          # main component build file
│   ├── main.c                  # Entry file
│   ├── wifi_manager.c          # WiFi manager
│   ├── wifi_manager.h
│   ├── http_server.c           # HTTP server
│   ├── http_server.h
│   └── Kconfig.projbuild       # menuconfig custom options
├── components/                  # Custom components
│   └── virtual_sensor/
│       ├── CMakeLists.txt
│       ├── virtual_sensor.c
│       └── include/
│           └── virtual_sensor.h
├── frontend/                    # Web frontend resources
│   ├── index.html
│   ├── style.css
│   └── app.js
├── tools/                       # Development utilities
│   ├── start-browser.py
│   └── check-env.sh
├── tests/                       # Test scripts
│   ├── test_serial.py
│   └── test_web_ui.py
└── docs/
    └── daily-logs/             # Daily work logs
```

### 2. Top-Level CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)

# Include ESP-IDF build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project({{PROJECT_NAME}})
```

### 3. main/CMakeLists.txt

```cmake
idf_component_register(
    SRCS "main.c" "wifi_manager.c" "http_server.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_http_server nvs_flash esp_netif
    PRIV_REQUIRES virtual_sensor
)

# Embed frontend files into firmware
spiffs_create_partition_image(storage ../frontend FLASH_IN_PROJECT)
```

### 4. Minimal Runnable main.c

```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "wifi_manager.h"
#include "http_server.h"

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting {{PROJECT_NAME}}...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize WiFi
    wifi_init_sta();

    // Start HTTP server
    start_webserver();

    ESP_LOGI(TAG, "{{PROJECT_NAME}} started successfully");
}
```

### 5. Basic HTML Template

```html
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{{PROJECT_NAME}}</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #1a1a2e; color: #eee; }
        .container { max-width: 800px; margin: 0 auto; }
        .card { background: #16213e; border-radius: 8px; padding: 16px; margin: 12px 0; }
        .sensor-value { font-size: 2em; font-weight: bold; color: #0f3460; }
        #stream { width: 100%; border-radius: 8px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>{{PROJECT_NAME}}</h1>
        <div class="card">
            <h2>Camera Stream</h2>
            <img id="stream" src="/stream" alt="Camera Stream">
        </div>
        <div class="card">
            <h2>Sensors</h2>
            <p>Temperature: <span id="temperature" class="sensor-value">--</span> °C</p>
            <p>Humidity: <span id="humidity" class="sensor-value">--</span> %</p>
        </div>
    </div>
    <script>
        async function updateSensors() {
            try {
                const resp = await fetch('/api/sensors');
                const data = await resp.json();
                document.getElementById('temperature').textContent = data.temperature.toFixed(1);
                document.getElementById('humidity').textContent = data.humidity.toFixed(1);
            } catch(e) { console.error('Sensor update failed:', e); }
        }
        setInterval(updateSensors, 5000);
        updateSensors();
    </script>
</body>
</html>
```

### 6. Scaffolding Generation Commands

```bash
# Create project directories
mkdir -p {{PROJECT_NAME}}/{main,components/virtual_sensor/include,frontend,tools,tests,docs/daily-logs}

# Set target chip
cd {{PROJECT_NAME}}
idf.py set-target esp32

# Initial build verification
idf.py build
```

## Self-Test

> Verify that the scaffolded project structure compiles correctly.

### Self-Test Steps

```bash
# Test 1: Directory structure completeness
DIRS="main components frontend tools tests docs/daily-logs"
ALL_OK=true
for d in $DIRS; do
    [ -d "{{PROJECT_NAME}}/$d" ] || { echo "SELF_TEST_FAIL: missing dir $d"; ALL_OK=false; }
done
$ALL_OK && echo "SELF_TEST_PASS: directory_structure"

# Test 2: CMakeLists.txt syntax check
grep -q "project({{PROJECT_NAME}})" {{PROJECT_NAME}}/CMakeLists.txt && \
    echo "SELF_TEST_PASS: cmake_syntax" || echo "SELF_TEST_FAIL: cmake_syntax"

# Test 3: Build passes (requires ESP-IDF environment)
cd {{PROJECT_NAME}} && idf.py build 2>&1 | tail -5
[ ${PIPESTATUS[0]} -eq 0 ] && echo "SELF_TEST_PASS: build" || echo "SELF_TEST_FAIL: build"
```

### Blind Test

**Test Prompt:**
```
You are an AI development assistant. Please read this Skill, then generate
a complete scaffolding for an ESP32 project named "test-project".
The project needs WiFi connectivity and a simple HTTP server.
After generating all required files, run idf.py build to verify it compiles.
```

**Acceptance Criteria:**
- [ ] Agent generates all required directories and files
- [ ] CMakeLists.txt structure is correct
- [ ] main.c includes basic initialization flow
- [ ] HTML template contains necessary UI elements
- [ ] Initial build succeeds (or errors are only due to missing hardware components)

## Success Criteria

- [ ] Project directory structure is complete
- [ ] `idf.py build` compiles successfully (or only has expected component-missing warnings)
- [ ] HTML page renders correctly in the browser
- [ ] All source files have correct include/import paths
