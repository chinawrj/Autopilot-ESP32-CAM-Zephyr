#!/bin/bash
# self-test for tmux-multi-shell
# Run: bash .github/skills/tmux-multi-shell/self-test.sh

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
  exit 1
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
      [ "$CODE" = "1" ] && exit 0 || exit 1
    fi
    sleep 1
    ELAPSED=$((ELAPSED + 1))
  done
  exit 1
'

# --- Test 6: Full output capture (P0) ---
test_case "full_output_capture" bash -c '
  SESSION="__tmux_skill_test__"
  tmux send-keys -t $SESSION:win_test "for i in \$(seq 1 200); do echo \"LINE_\$i\"; done" C-m
  sleep 2
  OUTPUT=$(tmux capture-pane -t $SESSION:win_test -p -S -1000)
  echo "$OUTPUT" | grep -q "LINE_1" && echo "$OUTPUT" | grep -q "LINE_200"
'

# --- Test 7: Timeout abort (P1) ---
test_case "timeout_abort" bash -c '
  SESSION="__tmux_skill_test__"
  tmux send-keys -t $SESSION:win_test "sleep 60" C-m
  sleep 1
  tmux send-keys -t $SESSION:win_test C-c
  sleep 1
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
