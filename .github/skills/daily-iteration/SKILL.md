---
name: daily-iteration
description: "Daily iteration workflow: morning planning, execution with build-flash-monitor cycle, evening review. Use when: starting a work day, planning tasks, reviewing progress."
---

# Skill: Daily Iteration Workflow

## Purpose

Define the AI-agent-driven daily development iteration process, including planning, task execution, progress verification, and daily report output.

**When to use:**
- Need a structured development cadence
- Project spans more than 1 day
- Need to track progress and milestones

**When not to use:**
- One-off small tasks
- Simple projects that don't require iteration

## Prerequisites

- Project requirements document is finalized (`requirements.md`)
- Workflow Agent is configured
- Development environment is ready

## Steps

### 1. Daily Iteration Model

```
┌─────────────────────────────────────────────────┐
│              Daily Iteration Loop                │
│                                                  │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐    │
│  │ Morning  │──▶│ Execute  │──▶│ Evening  │    │
│  │ Planning │   │ & Test   │   │ Review   │    │
│  └──────────┘   └──────────┘   └──────────┘    │
│       │                              │           │
│       └──────── Next Day ◀───────────┘           │
└─────────────────────────────────────────────────┘
```

### 2. Morning Planning

Execute at the start of each day:

```markdown
## Day N Plan

### Yesterday's Review
- Completed: [list completed tasks]
- Not completed: [list incomplete tasks and reasons]
- Blocked: [list blockers]

### Today's Goals
1. [Goal 1] - estimated time
2. [Goal 2] - estimated time
3. [Goal 3] - estimated time

### Risks & Dependencies
- [risk item]

### Acceptance Checkpoints
- [ ] [checkpoint 1]
- [ ] [checkpoint 2]
```

### 3. Execute & Test

Each task follows this workflow:

```
Write code → Build check → Flash & test → Serial verification → Web UI verification
    ↑                                                              │
    └────── Fix issues ◀───────────────────────────────────────────┘
```

Key rules:
- Run tests immediately after completing each task
- Test failures must be fixed before moving on to the next task
- Git commit after each milestone is completed

### 4. Evening Review

At the end of each day:

```markdown
## Day N Review

### Completion Status
| Task | Status | Notes |
|------|--------|-------|
| Task 1 | ✅ Done | |
| Task 2 | ⚠️ Partially done | Reason: ... |
| Task 3 | ❌ Not started | Blocked: ... |

### Code Quality
- Lines of code added: N
- Test pass rate: N%
- Known issues: [list]

### Tomorrow's Plan
- [priority tasks]

### Technical Notes
- [record key learnings from today]
```

### 5. Milestone Review

Conduct a milestone review every 3–5 days:

```markdown
## Milestone M: {{MILESTONE_NAME}}

### Goal Completion
- [x] Sub-goal 1
- [ ] Sub-goal 2 (70% progress)
- [ ] Sub-goal 3 (not started)

### Overall Progress: N%

### Plan Adjustment Needed: Yes/No
### Adjustment Details: ...
```

### 6. Refactoring Window

Schedule a refactoring session every 5 iteration days:
- Review code complexity
- Eliminate technical debt
- Optimize performance bottlenecks
- Update documentation

## Work Log Format

All logs are saved in the `docs/daily-logs/` directory:

```
docs/daily-logs/
├── day-001.md
├── day-002.md
├── ...
└── milestone-1-review.md
```

## Self-Test

> Verify the document generation and tracking mechanisms of the daily iteration workflow.

### Self-Test Steps

```bash
# Test 1: docs directory can be created
mkdir -p /tmp/__selftest_daily__/docs/daily-logs && \
  echo "SELF_TEST_PASS: docs_dir" || echo "SELF_TEST_FAIL: docs_dir"

# Test 2: Markdown template rendering
cat > /tmp/__selftest_daily__/docs/daily-logs/day-001.md << 'PLAN'
## Day 1 Plan
### Yesterday's Review
- Completed: project initialization
### Today's Goals
1. [x] Set up project framework
2. [ ] Implement WiFi connection
### Acceptance Checkpoints
- [x] Build passes
PLAN
grep -c '\[x\]' /tmp/__selftest_daily__/docs/daily-logs/day-001.md | \
  xargs -I{} bash -c '[ {} -ge 1 ] && echo "SELF_TEST_PASS: plan_format" || echo "SELF_TEST_FAIL: plan_format"'

# Test 3: Git is available for commit tracking
command -v git &>/dev/null && echo "SELF_TEST_PASS: git_available" || echo "SELF_TEST_FAIL: git_available"

rm -rf /tmp/__selftest_daily__
```

### Blind Test

**Test Prompt:**
```
You are an AI development assistant. Read this Skill, then generate
a complete Day 1 work plan document for a project called "test-project", including:
- Morning plan (3 specific goals)
- Execution log template
- Evening review template
The project goal is "build an ESP32 WiFi HTTP server."
Output the full Markdown document.
```

**Acceptance Criteria:**
- [ ] Agent generated a document containing all three sections
- [ ] Goals are specific and actionable (not vague)
- [ ] Document uses checkbox format
- [ ] Agent followed the template format from the Skill

## Success Criteria

- [ ] Daily plan and review documents have been generated
- [ ] Task execution has corresponding test verification
- [ ] Git commits are in sync with task completion
- [ ] Milestones are reviewed on schedule
- [ ] Refactoring windows are executed as planned
