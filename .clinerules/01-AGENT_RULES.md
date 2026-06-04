# PC-ALE Agent Rules

## Environment (read this first — never guess)
- **OS:** Windows 11
- **Shell:** CMD (cmd.exe) — use only CMD syntax, never bash/PowerShell/Linux commands
- **Repo root:** `E:\repos\PC-ALE`
- **Build dir:** `E:\repos\PC-ALE\build` — always delete and recreate fresh before verify
- **CMake generator:** cmake (version 4.1.0)
- **Forbidden:** `ls`, `rm`, `mkdir -p`, `touch`, `/` path separators — use `dir`, `rmdir /s /q`, `mkdir`, `\`

## CMD Command Reference (use exactly these forms)

```cmd
:: Navigate
cd E:\repos\PC-ALE

:: Delete build dir
rmdir /s /q E:\repos\PC-ALE\build

:: Create and enter build dir
mkdir E:\repos\PC-ALE\build
cd E:\repos\PC-ALE\build

:: Configure (cmake)
cmake ..

:: Build
cmake --build . --config Release

:: Test
ctest -C Release --output-on-failure
```

If `ctest` is not found, run: `cmake --build . --config Release --target RUN_TESTS`

## Scope
Work EXCLUSIVELY on `PC-ALE` (Core/Domain). No platform adapters, no application layer.
Core = pure domain logic, no OS/hardware APIs, no `#ifdef _WIN32` outside `src/platform/`.

## Session Start (mandatory, in order)
1. Ensure correct repo and branch:
   ```cmd
   cd E:\repos\PC-ALE
   gh repo set-default dl3hc/PC-ALE
   git checkout develop
   git pull origin develop
   ```
2. Read `CONTEXT.md` → project state + architecture
3. Read `progress.md` → what's done, what's broken
4. Find next actionable Issue (no `blocked` label, lowest number):
   ```cmd
   gh issue list --state open --json number,title,labels
   ```
5. Read that Issue fully:
   ```cmd
   gh issue view <N> --json body --jq ".body"
   ```
6. Read ONLY the files listed in `## 📁 Module` of the Issue
7. Implement. Test. Verify.

## Feature Cycle (one Issue at a time, no exceptions)

### PLAN (before touching code)
- Extract: data structures, function signatures, algorithms from the Issue spec section
- Identify: which files to create/modify, which existing code to preserve
- State explicitly: what you will do and what you will NOT touch

### ACT (atomic steps)
- One logical change per step (one function, one struct, one test)
- Build after each meaningful step:
```cmd
cd E:\repos\PC-ALE
rmdir /s /q build
mkdir build
cd build
cmake .. 
cmake --build .
```
- If a step fails: fix it before moving on — never accumulate broken state

### VERIFY (before marking done)
Fresh build — always, no exceptions:
```cmd
cd E:\repos\PC-ALE
rmdir /s /q build
mkdir build
cd build
cmake ..
cmake --build .
ctest -C Release --output-on-failure
```
- Every AC checkbox in the Issue has a passing test
- No TODO/PLACEHOLDER in produced code
- No compiler warnings (/W4 or equivalent)
- `depends_on` features still pass their tests (no regression)

### CLOSE
- Tick all AC checkboxes in the Issue:
  ```cmd
  gh issue edit <N> --body "<updated body with all - [x]>"
  ```
- Close the Issue:
  ```cmd
  gh issue close <N> --comment "done: alle ACs verified, ctest grün"
  ```
- Append one line to `progress.md`: `[FEAT-xxx] done — <one sentence what changed>`
- Update `CONTEXT.md` sections "Current Focus" and "Next Steps" only
- Go back to Session Start step 4

## Architecture Rules (hard constraints)
- Core communicates with platform ONLY via PAL interfaces (dependency injection)
- PCM buffer boundary: Core outputs `int16_t*`, consumes `float*` — nothing else
- Forbidden in Core: ALSA, WASAPI, Win32, POSIX threads, file I/O, sockets
- All tests run without audio hardware — no `IAudioDriver` initialization in tests
- New patterns require explicit justification in `CONTEXT.md`

## Autonomous Decision Rules
These decisions require NO user confirmation:
- Choosing implementation approach within the Issue spec
- Naming local variables, helper functions
- Adding `const`, `[[nodiscard]]`, defensive asserts
- Writing additional test cases beyond the minimum ACs
- Fixing compiler warnings in files you already touch

These require a STOP and explicit user confirmation:
- Modifying a file NOT listed in `## 📁 Module` of the current Issue
- Changing a public interface (header) used by other features
- Adding a new dependency (CMakeLists.txt)
- Deviating from a spec requirement stated in the Issue
- A feature is blocked by a missing dependency not yet done

## Factuality Rules
- Never infer what code does — read it
- Never assume a function exists — check the header
- If Issue spec and existing code conflict: report as finding, do not silently fix
- If existing code contradicts the AC: report it, stop, ask

## Bug Handling
Bugs discovered during work → create a new Issue immediately, do not fix now:
```cmd
gh issue create --title "[BUG] <description>" --body "<what, which file, which line>" --label "blocked"
```

## Definition of Done
☑ All AC checkboxes in the Issue ticked
☑ Fresh build passes (`rmdir /s /q build` → `cmake` → `cmake --build` → `ctest`)
☑ No TODO/PLACEHOLDER in new code
☑ No new compiler warnings
☑ Issue closed via `gh issue close`
☑ `progress.md` updated (one line appended)
