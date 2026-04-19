#!/bin/bash
# self-test for cdp-web-inspector
# Run: bash .github/skills/cdp-web-inspector/self-test.sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/../_common/detect-python.sh"

PASS=0
FAIL=0
SKIP=0

test_pass() { echo "SELF_TEST_PASS: $1"; PASS=$((PASS + 1)); }
test_fail() { echo "SELF_TEST_FAIL: $1"; FAIL=$((FAIL + 1)); }
skip_case() { echo "SELF_TEST_SKIP: $1 ($2)"; SKIP=$((SKIP + 1)); }

# --- Detect Python with patchright ---
PYTHON=$(detect_python "patchright.sync_api")
if [ -z "$PYTHON" ]; then
  skip_case "patchright_import" "patchright not found, set PATCHRIGHT_PYTHON or pip install patchright"
  echo ""; echo "Results: $PASS passed, $FAIL failed, $SKIP skipped"; exit $FAIL
fi
test_pass "patchright_import ($PYTHON)"

# --- Test 2: chromium driver exists ---
if ls ~/Library/Caches/ms-playwright/chromium-* &>/dev/null || \
   ls ~/.cache/ms-playwright/chromium-* &>/dev/null; then
  test_pass "chromium_driver"
else
  skip_case "chromium_driver" "patchright install chromium"
fi

# --- Test 3: visible-mode browser launch + page interaction ---
if $PYTHON -c "
from patchright.sync_api import sync_playwright
import os
pw = sync_playwright().start()
ctx = pw.chromium.launch_persistent_context(
    user_data_dir=os.path.expanduser('~/.patchright-userdata/selftest'),
    channel='chrome',
    headless=False,  # ⛔ headless is forbidden
    no_viewport=True,
)
page = ctx.pages[0] if ctx.pages else ctx.new_page()
page.goto('data:text/html,<h1 id=\"t\">hello</h1><p class=\"x\">world</p>')
assert page.locator('h1').text_content() == 'hello'
assert page.locator('p.x').text_content() == 'world'
ctx.close()
pw.stop()
" 2>/dev/null; then
  test_pass "browser_launch_and_query"
else
  test_fail "browser_launch_and_query"
fi

# --- Test 4: JavaScript evaluate ---
if $PYTHON -c "
from patchright.sync_api import sync_playwright
import os
pw = sync_playwright().start()
ctx = pw.chromium.launch_persistent_context(
    user_data_dir=os.path.expanduser('~/.patchright-userdata/selftest'),
    channel='chrome',
    headless=False,  # ⛔ headless is forbidden
    no_viewport=True,
)
page = ctx.pages[0] if ctx.pages else ctx.new_page()
page.goto('data:text/html,<title>Test Page</title>')
title = page.evaluate('document.title')
assert title == 'Test Page', f'Expected Test Page, got {title}'
ctx.close()
pw.stop()
" 2>/dev/null; then
  test_pass "js_evaluate"
else
  test_fail "js_evaluate"
fi

# --- Summary ---
echo ""
echo "Results: $PASS passed, $FAIL failed, $SKIP skipped"
exit $FAIL
