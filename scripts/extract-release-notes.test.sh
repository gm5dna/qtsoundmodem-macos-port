#!/usr/bin/env bash
# Self-test for extract-release-notes.sh
# Run: bash scripts/extract-release-notes.test.sh
# Exits 0 on success, non-zero on first failure.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SCRIPT="$HERE/extract-release-notes.sh"

fixture="$(mktemp)"
trap 'rm -f "$fixture"' EXIT

cat >"$fixture" <<'EOF'
# Changelog

## [Unreleased]

- Work in progress.

## [mac-0.1.0] – 2026-05-10

### Added
- First release thing.

### Fixed
- A bug.

## [mac-0.0.5] – 2026-04-01

### Added
- Older thing.

[Unreleased]: https://example/compare
[mac-0.1.0]: https://example/tag/mac-0.1.0
EOF

assert_eq() {
    local label="$1" expected="$2" actual="$3"
    if [[ "$expected" != "$actual" ]]; then
        echo "FAIL: $label" >&2
        diff <(echo "$expected") <(echo "$actual") >&2 || true
        exit 1
    fi
    echo "ok - $label"
}

# Case 1: known version returns its section only
expected_1="### Added
- First release thing.

### Fixed
- A bug."
actual_1="$("$SCRIPT" "$fixture" "mac-0.1.0")"
assert_eq "known version returns its section" "$expected_1" "$actual_1"

# Case 2: rc version falls back to Unreleased + banner
actual_2="$("$SCRIPT" "$fixture" "mac-0.0.0-rc1")"
case "$actual_2" in
    *"release candidate"*"Work in progress"*) echo "ok - rc fallback contains banner + unreleased body" ;;
    *) echo "FAIL: rc fallback missing banner or body" >&2; echo "got: $actual_2" >&2; exit 1 ;;
esac

# Case 3: unknown stable version exits non-zero
if "$SCRIPT" "$fixture" "mac-9.9.9" >/dev/null 2>&1; then
    echo "FAIL: unknown stable version should have errored" >&2
    exit 1
fi
echo "ok - unknown stable version errors"

echo "All tests passed."
