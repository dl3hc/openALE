# Memory Bank — PC-ALE

My memory resets completely between sessions.
The Memory Bank is my only link to previous work.
I read it at the start of every task — no exceptions.

---

## File Structure

### Always read (every session, in this order)

| File | Content | ~Tokens |
|---|---|---|
| `AGENT_RULES.md` | Environment, build commands, execution pipeline, architecture rules, DoD | ~900 |
| `CONTEXT.md` | Project scope, architecture, module map, design decisions, current focus | ~1150 |
| `progress.md` | Append-only log of completed features and known issues | ~520 |

**Total mandatory read: ~2570 tokens.**

### Read on demand (per feature task only)

| Source | When | How |
|---|---|---|
| GitHub Issue (current) | After finding next actionable Issue | `gh issue view <N> --json body --jq ".body"` |
| Source files in `## 📁 Module` | Before touching any code | Only the files listed in the Issue |

**Do NOT read `REQUIREMENTS.md` — it is 71k tokens.**
**Do NOT read any `*_INDEX.yml`, `*_BACKLOG*.yml`, or `FEATURES_DESIGN.md` — Issues are the single source of truth.**

---

## Session Workflow

```
SESSION START (mandatory):
  1. Read AGENT_RULES.md
  2. Read CONTEXT.md
  3. Read progress.md
  → Total: ~2570 tokens. Now find your task.

FIND TASK:
  4. Follow Session Start steps in AGENT_RULES.md
     (repo setup, branch, Issue queue)

BEFORE WRITING CODE:
  5. Read source files listed in ## 📁 Module (those files only)
  6. State your plan: what you will create/modify, what you will NOT touch

DURING IMPLEMENTATION:
  7. One atomic change at a time
  8. Incremental build after each meaningful step (see AGENT_RULES.md)
  9. Never accumulate broken state

SESSION END (mandatory):
  10. Fresh build + full test run (see AGENT_RULES.md)
  11. Tick all AC checkboxes → close Issue
  12. Append ONE line to progress.md: [FEAT-xxx] done — <what changed>
  13. Update CONTEXT.md sections "Current Focus" and "Next Steps" only
      (surgical edit — do NOT rewrite the full file)
  14. Go back to step 4
```

---

## Update Rules

### What to update and where

| Discovery | Update target | When |
|---|---|---|
| Feature complete | Close GitHub Issue + tick all AC checkboxes | Immediately after DoD met |
| Significant milestone | `progress.md` (append one line) | After Issue closed |
| New architectural decision | `CONTEXT.md` → Design Decisions | Before closing session |
| Current focus changed | `CONTEXT.md` → Current Focus + Next Steps | Before closing session |
| New open design question | `CONTEXT.md` → Known Issues | Before closing session |
| Bug discovered | `gh issue create --label "blocked"` | Immediately — do not fix now |

### What NOT to update
- Do not rewrite `CONTEXT.md` from scratch — surgical edits only
- Do not add narrative to `progress.md` — one line per feature, no more
- Do not edit Issue bodies except to tick AC checkboxes at close
- Do not create local backlog or index files — Issues are the single source of truth
