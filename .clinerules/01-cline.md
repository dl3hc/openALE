
---

# cline.md

## 1. Execution Pipeline (FEATURE CYCLE)

This is the mandatory execution lifecycle for any non-trivial task affecting code, architecture, or long-term state.

### 1.1 PLAN

1. Read all `memory-bank/*.md`
2. Extract:

   * architecture
   * dependencies
   * call chains
   * constraints
3. Build minimal, ordered implementation plan
4. Identify risks and failure points

Rules:

* Prefer semantic retrieval over file reading
* Minimize context expansion
* Do not assume missing information

---

### 1.2 ACT

1. Apply changes in small, atomic steps
2. Prefer single logical diff per iteration
3. Run validation after each meaningful step
4. Persist durable knowledge in `memory-bank`

Persist ONLY:

* interfaces / contracts
* architecture decisions
* dependencies
* non-trivial bug causes
* reusable patterns

Do NOT persist:

* logs
* temporary debugging
* trivial edits

---

### 1.3 VERIFY

1. Review diff and explain changes
2. Validate assumptions
3. Confirm runtime/build correctness
4. Ensure memory-bank consistency
5. Cross-check relationships

---

### 1.4 RESET (SYSTEM FUNCTION)

Triggered when a task is complete or context becomes noisy.

Actions:

1. Compress `memory-bank/activeContext.md` into a minimal state summary
2. Append outcome to `memory-bank/progress.md` (append-only)
3. Clear working assumptions
4. Start next task from fresh semantic retrieval

Rules:

* `activeContext.md` must remain minimal
* `progress.md` is immutable append-only log

---

## 2. Knowledge Model

Nodes:
function, concept, decision, bug, architecture element, interface

Relations:
calls, depends_on, implements, contradicts, supersedes

---

## 3. Memory Policy

Write only durable knowledge:

* architecture decisions
* root-cause bugs
* interfaces/contracts
* cross-module dependencies
* stable patterns

Do NOT write:

* logs
* syntax fixes
* temporary states

---

## 4. File-Level Work Discipline (memory-bank)

The system maintains a dual-layer state model:

* `memory-bank/` → operational working and semantic long-term memory

### Rules:

1. Always read `memory-bank/*.md` at task start
2. Update `activeContext.md` during execution
3. Append-only updates to `progress.md`
4. Do NOT overwrite historical progress
5. Prefer semantic retrieval over file re-reading
6. Reduce file context once Graphiti memory is sufficient

---

## 5. Factuality Enforcement (CRITICAL)

All system behavior MUST be strictly fact-based.

### Hard rules:

1. No hallucinated or inferred facts without evidence
2. All outputs MUST originate from:

   * code understanding
   * verified documentation files
3. If no evidence exists:

   * explicitly state uncertainty
   * do NOT fabricate information
4. Conflicting sources must be surfaced, not resolved arbitrarily
5. content retrieval MUST be used when memory is insufficient

> Every statement must be traceable to memory or external verified sources.

---

## 6. Quality Bar (Definition of Done)

Before finishing any workflow:

* task state is clearly resolved
* blockers are recorded or removed
* next actions are defined
* memory-bank reflects current state
* no unnecessary raw context remains loaded

---