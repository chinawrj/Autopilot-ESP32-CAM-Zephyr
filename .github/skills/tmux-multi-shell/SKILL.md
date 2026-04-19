---
name: tmux-multi-shell
description: "tmux multi-terminal management: build/flash/monitor windows, sentinel command execution, output capture. Use when: running terminal commands, managing build sessions, capturing command output."
---

# Skill: tmux Multi-Terminal Management (AI Agent Optimized)

## Purpose

Provide reliable multi-terminal automation capabilities for AI Agents. Core value: **Execute commands in parallel across multiple isolated windows, reliably wait for completion, capture full output, and determine success or failure.**

**When to use:**
- Need to run multiple terminal tasks simultaneously (build + flash + serial monitor)
- Need persistent terminal sessions (recoverable after Agent restart)
- Need to send commands and reliably capture results

**When not to use:**
- Single terminal task is sufficient
- Non-command-line environments

## Prerequisites

- `tmux` installed (macOS: `brew install tmux`, Linux: `apt install tmux`)
- Terminal supports tmux

## Steps

### 1. Idempotent Project Session Creation (P0)

> **Key:** Agent may restart or retry; must use `has-session` to avoid duplicate creation.

```bash
# Idempotent creation — skip if session exists, create if not
tmux has-session -t {{PROJECT_NAME}} 2>/dev/null || {
  tmux new-session -d -s {{PROJECT_NAME}}
  tmux rename-window -t {{PROJECT_NAME}}:0 'edit'
  tmux new-window -t {{PROJECT_NAME}} -n 'build'
  tmux new-window -t {{PROJECT_NAME}} -n 'flash'
  tmux new-window -t {{PROJECT_NAME}} -n 'monitor'
  echo "[tmux] Session '{{PROJECT_NAME}}' created with 4 windows"
}

# Verify session is ready
tmux list-windows -t {{PROJECT_NAME}} -F '#{window_index}:#{window_name}'
```

```bash
# Idempotent single window addition — skip if exists
tmux list-windows -t {{PROJECT_NAME}} -F '#{window_name}' | grep -q '^build$' || \
  tmux new-window -t {{PROJECT_NAME}} -n 'build'
```

### 2. Standard Window Layout

| Window # | Name | Purpose |
|---------|------|------|
| 0 | edit | Code editing / git operations |
| 1 | build | Compilation and building |
| 2 | flash | Firmware flashing |
| 3 | monitor | Serial log monitoring |

### 3. Send Commands to a Specific Window

```bash
# Run build in the build window
tmux send-keys -t {{PROJECT_NAME}}:build 'idf.py build' C-m

# Run flash in the flash window
tmux send-keys -t {{PROJECT_NAME}}:flash 'idf.py -p /dev/ttyUSB0 flash' C-m

# Start serial monitor in the monitor window
tmux send-keys -t {{PROJECT_NAME}}:monitor 'idf.py -p /dev/ttyUSB0 monitor' C-m
```

### 4. Wait for Command Completion + Exit Code Detection (P0)

> **Core pattern:** The AI Agent's operation loop — send command → wait for completion → read exit code → determine success/failure.

**Principle:** Wrap the actual command with a sentinel marker, then poll `capture-pane` to detect when the sentinel appears.

```bash
# === Send command with sentinel ===
# Format: actual command; then output sentinel + exit code
SENTINEL="__DONE_$(date +%s)__"
tmux send-keys -t {{PROJECT_NAME}}:build \
  "idf.py build; echo \"${SENTINEL}_EXIT_\$?\"" C-m

# === Poll for completion (with timeout) ===
TIMEOUT=120  # seconds
ELAPSED=0
INTERVAL=2   # check every 2 seconds
sleep 1      # wait for command to start executing
while [ $ELAPSED -lt $TIMEOUT ]; do
  OUTPUT=$(tmux capture-pane -t {{PROJECT_NAME}}:build -p -S -1000)
  if echo "$OUTPUT" | grep -q "${SENTINEL}_EXIT_[0-9]"; then
    # Extract exit code
    EXIT_CODE=$(echo "$OUTPUT" | grep -o "${SENTINEL}_EXIT_[0-9][0-9]*" | head -1 | grep -o '[0-9]*$')
    if [ "$EXIT_CODE" = "0" ]; then
      echo "[OK] Command succeeded (exit code 0)"
    else
      echo "[FAIL] Command failed (exit code $EXIT_CODE)"
    fi
    break
  fi
  sleep $INTERVAL
  ELAPSED=$((ELAPSED + INTERVAL))
done

if [ $ELAPSED -ge $TIMEOUT ]; then
  echo "[TIMEOUT] Command did not complete within ${TIMEOUT}s"
fi
```

**Simplified version — wrapped as a function:**

```bash
# tmux_exec: Execute command in specified window, wait for completion, return exit code
# Usage: tmux_exec <session:window> <command> [timeout_seconds]
tmux_exec() {
  local target="$1"
  local cmd="$2"
  local timeout="${3:-120}"
  local sentinel="__DONE_${RANDOM}__"

  # Send command
  tmux send-keys -t "$target" "$cmd; echo \"${sentinel}_EXIT_\$?\"" C-m

  # Poll for completion
  local elapsed=0
  sleep 1  # wait for command to start executing
  while [ $elapsed -lt $timeout ]; do
    local output
    output=$(tmux capture-pane -t "$target" -p -S -1000)
    local match
    match=$(echo "$output" | grep -o "${sentinel}_EXIT_[0-9][0-9]*" | head -1)
    if [ -n "$match" ]; then
      echo "$output"  # output full content for caller to analyze
      local code
      code=$(echo "$match" | grep -o '[0-9]*$')
      return "${code:-1}"
    fi
    sleep 2
    elapsed=$((elapsed + 2))
  done

  echo "[TIMEOUT] after ${timeout}s"
  return 124  # consistent with GNU timeout
}

# Usage example
tmux_exec "{{PROJECT_NAME}}:build" "idf.py build" 300
if [ $? -eq 0 ]; then
  echo "Build succeeded, proceeding to flash..."
  tmux_exec "{{PROJECT_NAME}}:flash" "idf.py -p /dev/ttyUSB0 flash" 60
fi
```

### 5. Full Output Capture (P0)

> **Key:** Default `capture-pane -p` only captures the visible area (~50 lines). Build errors may be hundreds of lines back; the AI must read the full history.

```bash
# Capture last 1000 lines of history (covers most build output)
tmux capture-pane -t {{PROJECT_NAME}}:build -p -S -1000

# Capture entire history (from buffer start)
tmux capture-pane -t {{PROJECT_NAME}}:build -p -S -

# Capture and save to file (suitable for very long output analysis)
tmux capture-pane -t {{PROJECT_NAME}}:build -p -S - > /tmp/build-output.txt
wc -l /tmp/build-output.txt  # check line count

# Read only the last N lines (saves Agent context window)
tmux capture-pane -t {{PROJECT_NAME}}:build -p -S -1000 | tail -50
```

**Set a larger history buffer (before creating session):**

```bash
# Set scrollback buffer to 10000 lines (default is 2000)
tmux set-option -g history-limit 10000
```

### 6. Timeout Handling and Command Abort (P1)

> **Scenario:** Command hangs (e.g., waiting for user input, network timeout); AI needs to proactively abort and recover.

```bash
# Method 1: Send Ctrl+C to abort current command
tmux send-keys -t {{PROJECT_NAME}}:build C-c

# Method 2: Send Ctrl+C then wait for shell prompt recovery
tmux send-keys -t {{PROJECT_NAME}}:build C-c
sleep 1
# Check if shell prompt is back (test by sending an empty echo)
tmux send-keys -t {{PROJECT_NAME}}:build 'echo __SHELL_READY__' C-m
sleep 1
tmux capture-pane -t {{PROJECT_NAME}}:build -p | grep -q __SHELL_READY__ && \
  echo "Shell recovered" || echo "Shell still stuck"

# Method 3: Force kill window and recreate (last resort)
tmux kill-window -t {{PROJECT_NAME}}:build
tmux new-window -t {{PROJECT_NAME}} -n 'build'
```

**tmux_exec has built-in timeout handling** (see Section 4); timeout returns exit code 124. Typical handling chain:

```bash
tmux_exec "{{PROJECT_NAME}}:build" "idf.py build" 300
rc=$?
case $rc in
  0)   echo "Success" ;;
  124) echo "Timeout — sending Ctrl+C"
       tmux send-keys -t {{PROJECT_NAME}}:build C-c ;;
  *)   echo "Failed with exit code $rc" ;;
esac
```

### 7. Structured Output Parsing (P1)

> **Goal:** Extract structured information the AI can directly use from raw capture-pane text, rather than relying on grep guesswork.

```bash
# === ESP-IDF build output parsing ===
OUTPUT=$(tmux capture-pane -t {{PROJECT_NAME}}:build -p -S -1000)

# Extract error count
ERROR_COUNT=$(echo "$OUTPUT" | grep -c "^.*error:")
WARNING_COUNT=$(echo "$OUTPUT" | grep -c "^.*warning:")

# Extract build progress (ESP-IDF format: [nn/mm])
PROGRESS=$(echo "$OUTPUT" | grep -oE '\[[0-9]+/[0-9]+\]' | tail -1)

# Extract firmware size (if build succeeded)
FIRMWARE_SIZE=$(echo "$OUTPUT" | grep -oE 'Binary size: [0-9.]+ [KM]B' | tail -1)

# Summary report
echo "=== Build Summary ==="
echo "Errors:   $ERROR_COUNT"
echo "Warnings: $WARNING_COUNT"
echo "Progress: $PROGRESS"
echo "Firmware: $FIRMWARE_SIZE"
```

```bash
# === Generic command output parsing pattern ===
# Extract full context of the first error (3 lines before and after)
tmux capture-pane -t {{PROJECT_NAME}}:build -p -S -1000 | \
  grep -n "error:" | head -1 | cut -d: -f1 | \
  xargs -I{} sed -n "$(({}>=3?{}-3:1)),$(({} + 3))p" <(
    tmux capture-pane -t {{PROJECT_NAME}}:build -p -S -1000
  )
```

### 8. Parallel Command Coordination (P1)

> **Scenario:** Start tasks in multiple windows simultaneously, wait for all to complete before proceeding.

```bash
# === Execute in parallel, collect all results ===

# Start commands in each window (with their own sentinels)
S1="__DONE_build_${RANDOM}__"
S2="__DONE_test_${RANDOM}__"

tmux send-keys -t {{PROJECT_NAME}}:build "make build; echo \"${S1}_EXIT_\$?\"" C-m
tmux send-keys -t {{PROJECT_NAME}}:edit  "make test; echo \"${S2}_EXIT_\$?\"" C-m

# Wait for all commands to complete
TIMEOUT=300
ELAPSED=0
BUILD_DONE=0
TEST_DONE=0

while [ $ELAPSED -lt $TIMEOUT ] && { [ $BUILD_DONE -eq 0 ] || [ $TEST_DONE -eq 0 ]; }; do
  if [ $BUILD_DONE -eq 0 ]; then
    tmux capture-pane -t {{PROJECT_NAME}}:build -p -S -1000 | grep -q "${S1}_EXIT_" && BUILD_DONE=1
  fi
  if [ $TEST_DONE -eq 0 ]; then
    tmux capture-pane -t {{PROJECT_NAME}}:edit -p -S -1000 | grep -q "${S2}_EXIT_" && TEST_DONE=1
  fi
  sleep 2
  ELAPSED=$((ELAPSED + 2))
done

# Report results
echo "Build: $([ $BUILD_DONE -eq 1 ] && echo 'completed' || echo 'TIMEOUT')"
echo "Test:  $([ $TEST_DONE -eq 1 ] && echo 'completed' || echo 'TIMEOUT')"
```

### 9. Session Management

```bash
# List all sessions
tmux list-sessions

# Check if session exists (for use in scripts)
tmux has-session -t {{PROJECT_NAME}} 2>/dev/null && echo "exists" || echo "not found"

# Attach to session (for human debugging)
tmux attach-session -t {{PROJECT_NAME}}

# Safe shutdown — send Ctrl+C to all windows before destroying
for win in $(tmux list-windows -t {{PROJECT_NAME}} -F '#{window_name}'); do
  tmux send-keys -t "{{PROJECT_NAME}}:${win}" C-c 2>/dev/null
done
sleep 1
tmux kill-session -t {{PROJECT_NAME}}
```

## Self-Test

> Verify tmux is available and all P0/P1 capabilities work correctly.

### Self-Test Steps

```bash
#!/bin/bash
# self-test for tmux-multi-shell
# Run: bash skills/tmux-multi-shell/self-test.sh

SESSION="__tmux_skill_test__"
PASS=0
FAIL=0

test_case() {
  local name=$1; shift
  if "$@"; then
    echo "SELF_TEST_PASS: $name"
    PASS=$((PASS + 1))
  else
    echo "SELF_TEST_FAIL: $name"
    FAIL=$((FAIL + 1))
  fi
}

cleanup() { tmux kill-session -t $SESSION 2>/dev/null; }
trap cleanup EXIT

# --- Test 1: tmux installed ---
test_case "tmux_installed" command -v tmux

# --- Test 2: Idempotent session creation (P0) ---
test_case "idempotent_session" bash -c '
  SESSION="__tmux_skill_test__"
  tmux kill-session -t $SESSION 2>/dev/null
  # First creation
  tmux has-session -t $SESSION 2>/dev/null || tmux new-session -d -s $SESSION
  # Repeated call should not error
  tmux has-session -t $SESSION 2>/dev/null || tmux new-session -d -s $SESSION
  # Verify only one exists
  COUNT=$(tmux list-sessions -F "#{session_name}" | grep -c "^${SESSION}$")
  [ "$COUNT" = "1" ]
'

# --- Test 3: Multi-window creation and command sending ---
test_case "multi_window" bash -c '
  SESSION="__tmux_skill_test__"
  tmux new-window -t $SESSION -n win_test 2>/dev/null
  tmux send-keys -t $SESSION:win_test "echo hello_tmux_test" C-m
  sleep 1
  tmux capture-pane -t $SESSION:win_test -p | grep -q hello_tmux_test
'

# --- Test 4: Command completion wait + exit code detection (P0) ---
test_case "wait_and_exit_code" bash -c '
  SESSION="__tmux_skill_test__"
  SENTINEL="__DONE_TEST_$$__"
  tmux send-keys -t $SESSION:win_test "true; echo \"${SENTINEL}_EXIT_\$?\"" C-m
  sleep 1
  ELAPSED=0
  while [ $ELAPSED -lt 10 ]; do
    OUTPUT=$(tmux capture-pane -t $SESSION:win_test -p -S -100)
    MATCH=$(echo "$OUTPUT" | grep -o "${SENTINEL}_EXIT_[0-9][0-9]*" | head -1)
    if [ -n "$MATCH" ]; then
      CODE=$(echo "$MATCH" | grep -o "[0-9]*$")
      [ "$CODE" = "0" ] && exit 0 || exit 1
    fi
    sleep 1
    ELAPSED=$((ELAPSED + 1))
  done
  exit 1  # timeout
'

# --- Test 5: Failed command exit code detection (P0) ---
test_case "detect_failure_exit_code" bash -c '
  SESSION="__tmux_skill_test__"
  SENTINEL="__DONE_FAIL_$$__"
  tmux send-keys -t $SESSION:win_test "false; echo \"${SENTINEL}_EXIT_\$?\"" C-m
  sleep 1
  ELAPSED=0
  while [ $ELAPSED -lt 10 ]; do
    OUTPUT=$(tmux capture-pane -t $SESSION:win_test -p -S -100)
    MATCH=$(echo "$OUTPUT" | grep -o "${SENTINEL}_EXIT_[0-9][0-9]*" | head -1)
    if [ -n "$MATCH" ]; then
      CODE=$(echo "$MATCH" | grep -o "[0-9]*$")
      [ "$CODE" = "1" ] && exit 0 || exit 1  # expect exit code=1
    fi
    sleep 1
    ELAPSED=$((ELAPSED + 1))
  done
  exit 1
'

# --- Test 6: Full output capture (P0) ---
test_case "full_output_capture" bash -c '
  SESSION="__tmux_skill_test__"
  # Generate 200 lines of output (beyond visible area)
  tmux send-keys -t $SESSION:win_test "for i in \$(seq 1 200); do echo \"LINE_\$i\"; done" C-m
  sleep 2
  # Capture with -S -1000, verify line 1 is visible
  OUTPUT=$(tmux capture-pane -t $SESSION:win_test -p -S -1000)
  echo "$OUTPUT" | grep -q "LINE_1" && echo "$OUTPUT" | grep -q "LINE_200"
'

# --- Test 7: Timeout abort (P1) ---
test_case "timeout_abort" bash -c '
  SESSION="__tmux_skill_test__"
  # Send a blocking command
  tmux send-keys -t $SESSION:win_test "sleep 60" C-m
  sleep 1
  # Ctrl+C to abort
  tmux send-keys -t $SESSION:win_test C-c
  sleep 1
  # Verify shell recovered — can execute new command
  tmux send-keys -t $SESSION:win_test "echo __RECOVERED__" C-m
  sleep 1
  tmux capture-pane -t $SESSION:win_test -p | grep -q __RECOVERED__
'

# --- Test 8: Parallel command coordination (P1) ---
test_case "parallel_coordination" bash -c '
  SESSION="__tmux_skill_test__"
  tmux new-window -t $SESSION -n win_para 2>/dev/null
  S1="__PARA1_$$__"
  S2="__PARA2_$$__"
  tmux send-keys -t $SESSION:win_test "sleep 1; echo \"${S1}_EXIT_\$?\"" C-m
  tmux send-keys -t $SESSION:win_para "sleep 1; echo \"${S2}_EXIT_\$?\"" C-m
  ELAPSED=0; DONE1=0; DONE2=0
  while [ $ELAPSED -lt 15 ] && { [ $DONE1 -eq 0 ] || [ $DONE2 -eq 0 ]; }; do
    [ $DONE1 -eq 0 ] && tmux capture-pane -t $SESSION:win_test -p -S -100 | grep -q "${S1}_EXIT_" && DONE1=1
    [ $DONE2 -eq 0 ] && tmux capture-pane -t $SESSION:win_para -p -S -100 | grep -q "${S2}_EXIT_" && DONE2=1
    sleep 1
    ELAPSED=$((ELAPSED + 1))
  done
  [ $DONE1 -eq 1 ] && [ $DONE2 -eq 1 ]
'

# --- Summary ---
echo ""
echo "Results: $PASS passed, $FAIL failed"
exit $FAIL
```

### Expected Results

| Test Item | Priority | Expected Output | Verified Capability |
|--------|--------|---------|----------|
| tmux_installed | - | `SELF_TEST_PASS` | tmux available |
| idempotent_session | P0 | `SELF_TEST_PASS` | No conflict on repeated creation |
| multi_window | - | `SELF_TEST_PASS` | Multi-window + send-keys |
| wait_and_exit_code | P0 | `SELF_TEST_PASS` | Sentinel wait + exit code=0 |
| detect_failure_exit_code | P0 | `SELF_TEST_PASS` | Detect command failure (exit code=1) |
| full_output_capture | P0 | `SELF_TEST_PASS` | -S -1000 reads full history |
| timeout_abort | P1 | `SELF_TEST_PASS` | Ctrl+C abort + shell recovery |
| parallel_coordination | P1 | `SELF_TEST_PASS` | Multi-window parallel wait |

### Blind Test

**Scenario Description:**
AI Agent needs to build a project in tmux, wait for completion, check if it succeeded, and extract error information on failure.

**Test Prompt:**
```
You are an AI development assistant. Please read this Skill, then:
1. Idempotently create a tmux session named "testproj" (do not recreate if it already exists)
2. Ensure there is a build window
3. Execute "echo compile-start && sleep 2 && echo compile-done" in the build window
4. Use sentinel mode to wait for command completion and get the exit code
5. Use full output capture (-S -1000) to verify both "compile-start" and "compile-done" are in the output
6. Execute a failing command "exit 42" (be careful not to kill the shell — use bash -c),
   and verify it detects a non-zero exit code
7. Finally, clean up the session
```

**Acceptance Criteria:**
- [ ] Agent uses `has-session` for idempotent creation (not bare `new-session`)
- [ ] Agent uses sentinel + polling mode to wait for completion (not `sleep` guessing)
- [ ] Agent correctly extracts exit code and determines success/failure
- [ ] Agent uses `-S -1000` or `-S -` for full output capture
- [ ] Agent detects non-zero exit code from the failing command
- [ ] Agent cleans up the test session after completion

**Common Failure Patterns:**
- Using `sleep 5` instead of sentinel wait → unreliable; need to emphasize sentinel mode
- Bare `new-session` without checking `has-session` → duplicate creation error
- `capture-pane -p` without `-S` parameter → incomplete output
- Running `exit 42` directly in the window causing shell to exit → need `bash -c "exit 42"`

## Success Criteria

- [ ] tmux session created idempotently with all required windows
- [ ] Each window can independently send and execute commands
- [ ] Can wait for command completion and get exit code via sentinel mode
- [ ] Can retrieve full output history via `capture-pane -S -1000`
- [ ] Can abort commands via Ctrl+C on timeout and recover the shell
- [ ] Multi-window parallel commands can be coordinated to wait for all to complete
- [ ] Session remains reusable after Agent restart
