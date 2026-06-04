# Coding Standards

## General rules

- Write minimal, correct, maintainable code.
- Prefer clarity over cleverness.
- Avoid unnecessary abstractions.

## Structure

- Keep functions small and focused.
- One responsibility per module.
- Avoid deep nesting.
- Keep changes incremental.

## Naming

- Use descriptive, consistent names.
- Avoid abbreviations unless standard.

## Change discipline

Before modifying code:
- understand current behavior
- check memory-bank context

After modifying code:
- update working memory if relevant
- record durable discoveries only if they matter long-term

## Error handling

- Fail explicitly, not silently.
- Do not swallow errors.
- Provide meaningful messages.

## Refactoring

- Preserve behavior unless explicitly changing it.
- Avoid large refactors without clear reason.
- Break large changes into small diffs.

## Context awareness

- Do not rely on assumptions.
- Verify against memory-bank and codebase.
- Use `memory-bank` before guessing on non-trivial issues.

## Conditional rules

Large changes:
- consult memory
- check architecture patterns
- verify with tests after each meaningful step

Debugging:
- use `memory-bank` before guessing

New features:
- align with `systemPatterns.md`
- avoid introducing new patterns without justification