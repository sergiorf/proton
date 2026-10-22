---
name: task
description: Implement one numbered task (M<milestone>.<n>) from PLAN.md, e.g. "/task M1.2". Use when the user asks to execute a plan task.
disable-model-invocation: true
---

Implement task $ARGUMENTS from PLAN.md, following the "Working on PLAN.md tasks"
rules in CLAUDE.md.

## Preconditions
Stop and tell me if:
- No task ID was given, or it doesn't exist in PLAN.md.
- The task is already marked done.
- An earlier task is not done (tasks go in order); ask whether to proceed anyway.
- The working tree is not clean.

## When to ask me
Stop and ask before writing code if any of these apply:
- The task, its "Done when" line, or ARCHITECTURE.md is ambiguous or
  contradicts itself, or two reasonable readings would lead to different code.
- The task needs a design choice the plan and ARCHITECTURE.md don't settle
  (API shape, threading model, data format, new dependency).
- It depends on an open decision in the PLAN.md Decisions table.
- You would have to deviate from the plan, or touch code outside the task's scope.

Ask with concrete options and your recommended default, so I can answer
in one line. Don't ask about things the plan or ARCHITECTURE.md already decide,
or about naming and minor local choices: decide those yourself and mention them
in the summary.

## Steps
1. Read CLAUDE.md, the task's section in PLAN.md, and the ARCHITECTURE.md
   sections it references. Check the questions above before going further.
2. Update main from origin and create branch m<id>-<short-name> from it.
3. Plan briefly and show me the plan. Wait for approval if it contains any
   design choice; otherwise continue.
4. Write tests for the "Done when" line first. Add only the minimal stubs needed
   to compile, and show the tests failing on their CHECKs, not on a build error.
5. Implement until the tests pass. Build and run the full Windows debug preset
   locally, warning-free.
6. Update ARCHITECTURE.md / ROADMAP.md if the code now differs from them.
7. Show me the diff and a summary: what changed, every choice you made that the
   plan didn't specify, and any deviation. Wait for my OK.
8. On my OK: mark the heading *(done locally YYYY-MM-DD, CI pending)*, commit,
   push, and open a PR.
9. Once CI is green on all presets, change the marker to *(done YYYY-MM-DD)*
   and push that commit. If CI fails, fix it on the same branch and repeat.
10. Stop. Do not begin the next task or merge the PR.
