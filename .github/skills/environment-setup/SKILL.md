---
name: environment-setup
description: "Development environment verification: toolchain, drivers, dependencies check. Use when: first setup, verifying tools are installed, diagnosing missing dependencies."
---

# Skill: Development Environment Check & Setup

## Purpose

Verify the development environment is ready before starting a project. Check that the toolchain, drivers, Python packages, and other dependencies are correctly installed and configured.

**When to use:**
- First step of a new project (Day 0)
- After switching to a new development machine
- After a toolchain upgrade that requires re-verification
- CI environment initialization

**When not to use:**
- Environment has already been verified and nothing has changed
- Only performing a code review without building

## Prerequisites

- Operating system: macOS / Linux
- Basic command-line tools available (bash/zsh, which, python3)

## Steps

### 1. Basic Tool Check

```bash
#!/bin/bash
# check-env.sh - Development environment check script

echo "=== Development Environment Check ==="
ERRORS=0

check_cmd() {
    local cmd=$1
    local name=${2:-$1}
    if command -v "$cmd" &>/dev/null; then
        echo "  ✓ $name: $(command -v "$cmd")"
    else
        echo "  ✗ $name: not installed"
        ERRORS=$((ERRORS + 1))
    fi
}

# Basic tools
echo ""
echo "[Basic Tools]"
check_cmd git "Git"
check_cmd python3 "Python3"
check_cmd pip3 "pip3"
check_cmd tmux "tmux"

# ESP-IDF toolchain (if applicable)
echo ""
echo "[ESP-IDF Toolchain]"
if [ -f "$HOME/esp/esp-idf/export.sh" ]; then
    echo "  ✓ ESP-IDF: $HOME/esp/esp-idf"
    source "$HOME/esp/esp-idf/export.sh" 2>/dev/null
    check_cmd idf.py "idf.py"
    check_cmd xtensa-esp32-elf-gcc "xtensa compiler"
else
    echo "  ✗ ESP-IDF: not found (expected path: ~/esp/esp-idf/)"
    ERRORS=$((ERRORS + 1))
fi

# Serial port devices
echo ""
echo "[Serial Devices]"
PORTS=$(ls /dev/tty.usb* /dev/cu.usb* /dev/ttyUSB* /dev/ttyACM* 2>/dev/null)
if [ -n "$PORTS" ]; then
    echo "$PORTS" | while read p; do echo "  ✓ $p"; done
else
    echo "  ⚠ No USB serial devices detected (is the device connected?)"
fi

# Python packages
echo ""
echo "[Python Packages]"
for pkg in patchright pyyaml pyserial; do
    if python3 -c "import ${pkg//-/_}" 2>/dev/null; then
        echo "  ✓ $pkg"
    else
        echo "  ✗ $pkg: not installed (pip3 install $pkg)"
        ERRORS=$((ERRORS + 1))
    fi
done

# Patchright browser driver
echo ""
echo "[Browser Driver]"
CHROMIUM_DIR="$HOME/Library/Caches/ms-playwright"
if [ -d "$CHROMIUM_DIR" ] && ls "$CHROMIUM_DIR"/chromium-* &>/dev/null; then
    echo "  ✓ Chromium driver: $(ls -d "$CHROMIUM_DIR"/chromium-* | head -1)"
else
    echo "  ✗ Chromium driver not installed (python -m patchright install chromium)"
    ERRORS=$((ERRORS + 1))
fi

# Summary
echo ""
echo "================================"
if [ $ERRORS -eq 0 ]; then
    echo "✅ Environment check passed, ready to start development"
else
    echo "❌ Found $ERRORS issue(s), please fix before proceeding"
fi
exit $ERRORS
```

### 2. ESP-IDF Version Verification

```bash
# Check ESP-IDF version
idf.py --version

# Check supported chips
idf.py --list-targets
```

### 3. Python Virtual Environment

```bash
# If using Patchright, verify the virtual environment
source ~/patchright-env/bin/activate
python -c "import patchright; print(f'Patchright {patchright.__version__}')"
```

### 4. Network Connectivity

```bash
# Check if the device is reachable (if IP is known)
ping -c 3 {{DEVICE_IP}} 2>/dev/null && echo "✓ Device reachable" || echo "✗ Device unreachable"

# Check CDP port
curl -s http://localhost:9222/json/version && echo "✓ CDP available" || echo "⚠ CDP not running"
```

## Self-Test

> The following tests verify that the instructions in this Skill work correctly in the current environment. The AI Agent should run the self-test the first time it uses this Skill.

### Self-Test Steps

```bash
# Test 1: check-env.sh script can execute
bash -c 'command -v git && command -v python3 && echo "SELF_TEST_PASS: basic_tools"'

# Test 2: Python can import yaml
python3 -c "import yaml; print('SELF_TEST_PASS: pyyaml')" 2>/dev/null || echo "SELF_TEST_FAIL: pyyaml"

# Test 3: tmux is interactive
tmux new-session -d -s __selftest__ && tmux kill-session -t __selftest__ && echo "SELF_TEST_PASS: tmux" || echo "SELF_TEST_FAIL: tmux"
```

### Expected Results

| Test Item | Expected Output | Failure Impact |
|-----------|----------------|----------------|
| basic_tools | `SELF_TEST_PASS: basic_tools` | Missing basic tools, cannot proceed |
| pyyaml | `SELF_TEST_PASS: pyyaml` | Builder engine cannot run |
| tmux | `SELF_TEST_PASS: tmux` | Multi-window workflow unavailable |

### Blind Test

> The Blind Test simulates a scenario where the AI Agent reads this Skill for the first time and executes it independently, without additional context.

**Test Prompt:**
```
You are an AI development assistant. Read the following Skill and run
an environment check on the current machine.
Do not assume any tools are installed; verify each step as described in the Skill.
Summarize the results in a table, marking each item's status (✓/✗/⚠).
```

**Acceptance Criteria:**
- [ ] Agent correctly executes the check logic from check-env.sh
- [ ] Agent does not skip any check steps
- [ ] Agent correctly identifies missing tools and provides installation suggestions
- [ ] Agent outputs results in a clear table format

## Success Criteria

- [ ] All required tools are installed and executable
- [ ] ESP-IDF environment loads successfully
- [ ] Serial devices are detected (if connected)
- [ ] All Python dependency packages are installed
- [ ] Browser driver is available (if Web testing is needed)
