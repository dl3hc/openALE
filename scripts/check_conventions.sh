#!/usr/bin/env bash
# PAL convention checker — run as git pre-commit hook or in CI.
#
# Install as hook:
#   cp scripts/check_conventions.sh .git/hooks/pre-commit && chmod +x .git/hooks/pre-commit
#
# Run manually against all production files:
#   bash scripts/check_conventions.sh
#
# Run on staged files only (hook mode, called automatically by git):
#   bash scripts/check_conventions.sh --staged

set -euo pipefail

FAIL=0
STAGED_ONLY=false
[[ "${1:-}" == "--staged" ]] && STAGED_ONLY=true

# Collect files to check
if $STAGED_ONLY; then
    mapfile -t FILES < <(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cpp|h)$' || true)
else
    mapfile -t FILES < <(find src apps include ale_monitor -name '*.cpp' -o -name '*.h' 2>/dev/null || true)
fi

[[ ${#FILES[@]} -eq 0 ]] && exit 0

# ── Check 1: no raw printf / fprintf in production source ──────────────────
# Exemptions:
#   src/PAL/         — logger.cpp IS the abstraction point; mock_radio.h is a PAL test helper
#   apps/radio_mock  + test_hamlib_mock — standalone diagnostic tools, not part of the binary
#   Lines starting with // or * (comments, including doc-comment examples)
#   Lines containing NOLINT(pal-logger) — explicitly exempted (e.g. print_usage() bodies)
PRINTF_VIOLATIONS=$(grep -nE '\b(printf|fprintf)\s*\(' "${FILES[@]}" 2>/dev/null \
    | grep -v 'src/PAL/'            \
    | grep -v 'include/PAL/'        \
    | grep -v 'apps/radio_mock.cpp' \
    | grep -v 'apps/test_hamlib_mock.cpp' \
    | grep -vE ':[[:space:]]*[/*]'  \
    | grep -v 'NOLINT(pal-logger)'  \
    || true)

if [[ -n "$PRINTF_VIOLATIONS" ]]; then
    echo "CONVENTION VIOLATION — use pal::get_logger()->log_info/warn/error() instead of printf/fprintf:"
    echo "$PRINTF_VIOLATIONS"
    echo ""
    FAIL=1
fi

# ── Check 2: no new std::function on_* callback fields outside PAL ─────────
# All cross-component notifications must go through pal::IEventHandler.
#
# Exemptions:
#   include/PAL/   — IEventHandler itself lives here
#   Trailing-underscore names (on_foo_) — private implementation callbacks, not API surface
#   include/App/voice_path_manager.h — on_speaker_pcm/on_ptt_activity carry binary PCM/HW
#     signals that cannot pass through the void* event bus efficiently
ON_STAR_FILES=$(printf '%s\n' "${FILES[@]}" | grep -E '\.h$' \
    | grep -v 'include/PAL/' \
    | grep -v 'include/App/voice_path_manager.h' \
    || true)

if [[ -n "$ON_STAR_FILES" ]]; then
    # Match on_<name> where name does NOT end with underscore (public API fields only)
    ON_STAR_VIOLATIONS=$(echo "$ON_STAR_FILES" | xargs grep -nE \
        'std::function\s*<[^>]+>\s+on_[a-z][a-z_]*[a-z]\s*[;={]' 2>/dev/null \
        | grep -v 'NOLINT(pal-event)' \
        || true)
    if [[ -n "$ON_STAR_VIOLATIONS" ]]; then
        echo "CONVENTION VIOLATION — use pal::get_event_handler()->on(EventType, cb) instead of std::function on_* fields:"
        echo "$ON_STAR_VIOLATIONS"
        echo ""
        FAIL=1
    fi
fi

exit $FAIL
