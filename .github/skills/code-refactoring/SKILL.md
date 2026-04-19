---
name: code-refactoring
description: "Code refactoring strategy with trigger thresholds: compile warnings, file size, function length, duplicates. Use when: code quality review, refactoring day, reducing tech debt."
---

# Skill: Code Refactoring Strategy

## Purpose

Define a strategy and workflow for periodic code refactoring to maintain code quality during continuous development.

**When to use:**
- Perform a planned refactoring every 5 iteration days
- When code smells are discovered
- During the cleanup phase after completing a feature
- When code complexity exceeds thresholds

**When not to use:**
- Early prototyping phase (get the feature working first)
- During an urgent bug fix
- On modules that already have good code quality

## Prerequisites

- All current tests pass (must have a green light before refactoring)
- Git working tree is clean (no uncommitted changes)
- Refactoring goals are clearly defined

## Steps

### 1. Refactoring Trigger Conditions

Evaluate the following metrics before each refactoring:

| Metric | Threshold | Detection Method |
|--------|-----------|-----------------|
| Function line count | > 50 lines | `wc -l` |
| File line count | > 500 lines | `wc -l` |
| Duplicate code | > 3 occurrences | `grep` search |
| TODO/FIXME | > 5 items | `grep -rn "TODO\|FIXME"` |
| Nesting depth | > 4 levels | Code review |
| Global variables | > 10 items | `grep` search |

### 2. Refactoring Checklist

```markdown
## Refactoring Checklist - Day N

### Code Review
- [ ] Find and split overly long functions
- [ ] Extract duplicate code into shared functions
- [ ] Eliminate magic numbers by defining constants
- [ ] Check naming consistency
- [ ] Clean up unused #include / import statements
- [ ] Address all TODO/FIXME items

### Architecture Review
- [ ] Are inter-module dependencies reasonable?
- [ ] Are interfaces clear (explicit inputs/outputs)?
- [ ] Is error handling consistent?
- [ ] Is memory management correct (critical for embedded)?

### Documentation Review
- [ ] Is the README in sync with the code?
- [ ] Are key functions commented?
- [ ] Are configuration instructions complete?
```

### 3. Safe Refactoring Workflow

```bash
# Step 1: Ensure tests pass
idf.py build && python test_serial.py && echo "Green light ✅"

# Step 2: Create a refactoring branch
git checkout -b refactor/day-N-cleanup

# Step 3: Perform refactoring (small incremental commits)
# ... code changes ...
git add -A && git commit -m "refactor: extract WiFi init into standalone module"

# ... more changes ...
git add -A && git commit -m "refactor: eliminate magic numbers in main.c"

# Step 4: Verify after refactoring
idf.py build && python test_serial.py && echo "Still green after refactoring ✅"

# Step 5: Merge
git checkout main && git merge refactor/day-N-cleanup
```

### 4. ESP32/Embedded-Specific Refactoring

#### Memory Optimization
```c
// Before: Large buffer allocated on the stack
void handle_request() {
    char buf[4096];  // May cause stack overflow
    // ...
}

// After: Use heap allocation or static allocation
static char s_buf[4096];  // Or use malloc/free
void handle_request() {
    // Use s_buf
}
```

#### Modularization
```
// Before: All code in main.c
main.c (800 lines)

// After: Split by functionality
main.c          (50 lines)  - Entry point and init dispatch
wifi_manager.c  (150 lines) - WiFi management
http_server.c   (200 lines) - HTTP server
camera_ctrl.c   (150 lines) - Camera control
sensor_reader.c (100 lines) - Sensor reading
```

#### Unified Error Handling
```c
// Define a unified error handling macro
#define CHECK_ESP_ERR(x, tag) do { \
    esp_err_t err = (x); \
    if (err != ESP_OK) { \
        ESP_LOGE(tag, "%s failed: %s", #x, esp_err_to_name(err)); \
        return err; \
    } \
} while(0)
```

### 5. Refactoring Report

Generate a report after each refactoring:

```markdown
## Refactoring Report - Day N

### Change Summary
- Split files: main.c → 5 module files
- Eliminated duplication: 3 duplicate code blocks extracted into shared functions
- Cleanup: removed 8 TODOs, 2 unused variables

### Code Metric Changes
| Metric | Before | After |
|--------|--------|-------|
| Max function line count | 120 | 45 |
| Number of files | 2 | 6 |
| TODO count | 12 | 4 |

### Test Results
- Build: ✅ Passed
- Serial tests: ✅ 5/5 passed
- Web UI tests: ✅ 4/4 passed
```

## Self-Test

> Verify the tools and workflow for the refactoring process.

### Self-Test Steps

```bash
# Test 1: Git is available and supports branch operations
TMP_REPO=$(mktemp -d)
cd "$TMP_REPO" && git init -q && \
  echo "init" > file.txt && git add . && git commit -q -m "init" && \
  git checkout -q -b refactor/test && \
  echo "refactored" > file.txt && git add . && git commit -q -m "refactor: test" && \
  git checkout -q main && git merge -q refactor/test && \
  echo "SELF_TEST_PASS: git_branch_workflow" || echo "SELF_TEST_FAIL: git_branch_workflow"
rm -rf "$TMP_REPO"

# Test 2: Code analysis tools are available
command -v grep &>/dev/null && command -v wc &>/dev/null && \
  echo "SELF_TEST_PASS: code_analysis_tools" || echo "SELF_TEST_FAIL: code_analysis_tools"

# Test 3: TODO/FIXME detection logic
TMP_FILE=$(mktemp)
cat > "$TMP_FILE" << 'EOF'
// TODO: fix this
// FIXME: memory leak
void ok_function() {}
// TODO: another one
EOF
COUNT=$(grep -c 'TODO\|FIXME' "$TMP_FILE")
[ "$COUNT" -eq 3 ] && echo "SELF_TEST_PASS: todo_detection ($COUNT found)" || echo "SELF_TEST_FAIL: todo_detection (expected 3, got $COUNT)"
rm "$TMP_FILE"

# Test 4: Long function detection
TMP_SRC=$(mktemp --suffix=.c 2>/dev/null || mktemp)
for i in $(seq 1 60); do echo "line $i;" >> "$TMP_SRC"; done
LINES=$(wc -l < "$TMP_SRC")
[ "$LINES" -gt 50 ] && echo "SELF_TEST_PASS: long_function_detect ($LINES lines)" || echo "SELF_TEST_FAIL: long_function_detect"
rm "$TMP_SRC"
```

### Blind Test

**Test Prompt:**
```
You are an AI development assistant. Read this Skill, then perform a refactoring analysis on the following code:

void handle_everything() {
    // 60 lines of WiFi initialization code
    // 40 lines of HTTP server code
    // 30 lines of sensor reading code
    // 5 TODOs and 2 FIXMEs
    // 3 duplicate error handling blocks
}

1. Identify all refactoring trigger conditions (refer to the threshold table in the Skill)
2. Propose a concrete refactoring plan (which modules to split into)
3. Generate a refactoring report template
```

**Acceptance Criteria:**
- [ ] Agent identified the function is too long (130 lines > 50-line threshold)
- [ ] Agent identified TODO/FIXME count exceeds threshold (7 > 5)
- [ ] Agent identified duplicate code
- [ ] Agent proposed splitting into wifi_manager, http_server, sensor_reader
- [ ] Agent generated a refactoring report matching the Skill format

## Success Criteria

- [ ] All tests remain passing before and after refactoring
- [ ] Code metrics show clear improvement
- [ ] Git history is clean with small incremental commits per refactoring step
- [ ] Refactoring report has been generated
