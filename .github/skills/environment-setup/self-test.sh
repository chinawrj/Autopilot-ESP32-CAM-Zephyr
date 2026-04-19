#!/bin/bash
# self-test for environment-setup
# Run: bash .github/skills/environment-setup/self-test.sh

PASS=0
FAIL=0
SKIP=0

test_pass() { echo "SELF_TEST_PASS: $1"; PASS=$((PASS + 1)); }
test_fail() { echo "SELF_TEST_FAIL: $1"; FAIL=$((FAIL + 1)); }
skip_case() { echo "SELF_TEST_SKIP: $1 ($2)"; SKIP=$((SKIP + 1)); }

# --- Test 1: basic tools available ---
ALL_BASIC=true
for tool in git python3 bash; do
  command -v $tool &>/dev/null || { ALL_BASIC=false; break; }
done
$ALL_BASIC && test_pass "basic_tools" || test_fail "basic_tools"

# --- Test 2: Python yaml module ---
if python3 -c "import yaml" 2>/dev/null; then
  test_pass "pyyaml"
else
  skip_case "pyyaml" "pip install pyyaml"
fi

# --- Test 3: tmux is interactive ---
if command -v tmux &>/dev/null; then
  if tmux new-session -d -s __env_selftest__ 2>/dev/null && \
     tmux kill-session -t __env_selftest__ 2>/dev/null; then
    test_pass "tmux"
  else
    test_fail "tmux"
  fi
else
  skip_case "tmux" "tmux not installed"
fi

# --- Test 4: pip available ---
if command -v pip3 &>/dev/null || python3 -m pip --version &>/dev/null 2>&1; then
  test_pass "pip"
else
  skip_case "pip" "pip not installed"
fi

# --- Test 5: system info retrievable ---
if bash -c 'uname -s &>/dev/null && uname -m &>/dev/null' 2>/dev/null; then
  test_pass "system_info"
else
  test_fail "system_info"
fi

# --- Summary ---
echo ""
echo "Results: $PASS passed, $FAIL failed, $SKIP skipped"
exit $FAIL
