#!/usr/bin/env bash
# Extract one CHANGELOG section into a Release-notes body.
#
# Usage: extract-release-notes.sh <changelog-path> <version>
#
# - For a stable version like "mac-0.1.0", emit the bullets under that
#   "## [<version>]" heading, stopping at the next "## [" heading or EOF.
#   Exit non-zero if no matching heading is found.
# - For a release-candidate version (contains "-rc"), emit the
#   "## [Unreleased]" section preceded by a one-line banner.
#
# Stdout is the body. No GitHub-specific formatting beyond Markdown.

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <changelog-path> <version>" >&2
    exit 2
fi

changelog="$1"
version="$2"

if [[ ! -f "$changelog" ]]; then
    echo "changelog not found: $changelog" >&2
    exit 2
fi

# Print the lines under "## [<heading>]" up to the next "## [" heading or
# "[x]: url" link reference, minus leading and trailing blank lines.
extract_section() {
    awk -v h="## [$1]" '
        !in_section { in_section = index($0, h) == 1; next }
        /^## \[/ || /^\[.*\]:/ { exit }
        NF { started = 1 }
        started { buf[++n] = $0; if (NF) last = n }
        END { for (i = 1; i <= last; i++) print buf[i] }
    ' "$changelog"
}

if [[ "$version" == *"-rc"* ]]; then
    body="$(extract_section "Unreleased")"
    if [[ -z "$body" ]]; then
        body="(no changes recorded in [Unreleased])"
    fi
    printf '> **This is a release candidate — not intended for production use.**\n\n%s\n' "$body"
    exit 0
fi

body="$(extract_section "$version")"
if [[ -z "$body" ]]; then
    echo "no CHANGELOG section for version: $version" >&2
    exit 1
fi
printf '%s\n' "$body"
