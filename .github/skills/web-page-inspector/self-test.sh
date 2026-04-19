#!/bin/bash
# self-test for web-page-inspector
# Run: bash .github/skills/web-page-inspector/self-test.sh

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

# --- Test 2: Page table data extraction ---
if $PYTHON -c "
from patchright.sync_api import sync_playwright
import os
pw = sync_playwright().start()
ctx = pw.chromium.launch_persistent_context(
    user_data_dir=os.path.expanduser('~/.patchright-userdata/selftest'),
    channel='chrome',
    headless=False,  # ⛔ headless forbidden
    no_viewport=True,
)
page = ctx.pages[0] if ctx.pages else ctx.new_page()
page.goto('data:text/html,<table><tr><td>Name</td><td>Value</td></tr><tr><td>Temp</td><td>25.5</td></tr></table>')
rows = page.query_selector_all('tr')
assert len(rows) == 2, f'Expected 2 rows, got {len(rows)}'
cells = rows[1].query_selector_all('td')
assert cells[0].text_content() == 'Temp'
assert cells[1].text_content() == '25.5'
ctx.close()
pw.stop()
" 2>/dev/null; then
  test_pass "page_data_extraction"
else
  test_fail "page_data_extraction"
fi

# --- Test 3: CSS selector query ---
if $PYTHON -c "
from patchright.sync_api import sync_playwright
import os
pw = sync_playwright().start()
ctx = pw.chromium.launch_persistent_context(
    user_data_dir=os.path.expanduser('~/.patchright-userdata/selftest'),
    channel='chrome',
    headless=False,  # ⛔ headless forbidden
    no_viewport=True,
)
page = ctx.pages[0] if ctx.pages else ctx.new_page()
page.goto('data:text/html,<div class=\"sensor\"><span class=\"label\">Humidity</span><span class=\"value\">60%</span></div>')
label = page.query_selector('.sensor .label')
value = page.query_selector('.sensor .value')
assert label.text_content() == 'Humidity'
assert value.text_content() == '60%'
ctx.close()
pw.stop()
" 2>/dev/null; then
  test_pass "css_selector_query"
else
  test_fail "css_selector_query"
fi

# --- Test 4: JavaScript execution to extract dynamic data ---
if $PYTHON -c "
from patchright.sync_api import sync_playwright
import os
pw = sync_playwright().start()
ctx = pw.chromium.launch_persistent_context(
    user_data_dir=os.path.expanduser('~/.patchright-userdata/selftest'),
    channel='chrome',
    headless=False,  # ⛔ headless forbidden
    no_viewport=True,
)
page = ctx.pages[0] if ctx.pages else ctx.new_page()
html = '<html><body><script>document.body.innerHTML=\"<div id=d>loaded</div>\";</script></body></html>'
page.goto(f'data:text/html,{html}')
page.wait_for_selector('#d')
text = page.evaluate('document.getElementById(\"d\").textContent')
assert text == 'loaded', f'Expected loaded, got {text}'
ctx.close()
pw.stop()
" 2>/dev/null; then
  test_pass "js_dynamic_extract"
else
  test_fail "js_dynamic_extract"
fi

# --- Summary ---
echo ""
echo "Results: $PASS passed, $FAIL failed, $SKIP skipped"
exit $FAIL
