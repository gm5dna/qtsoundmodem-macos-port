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

extract_section() {
    local heading="$1"
    # Escape regex metacharacters (dots are the common case in semver tags).
    local escaped
    escaped="$(printf '%s' "$heading" | sed 's/[.[\*^$]/\\&/g')"
    awk -v h="$escaped" '
        $0 ~ "^## \\[" h "\\]" { in_section = 1; next }
        in_section && /^## \[/  { exit }
        in_section && /^\[.*\]:/ { exit }
        in_section               { print }
    ' "$changelog"
}

trim_blank_edges() {
    awk '
        NF { if (!started) started = 1; buffer[++n] = $0; next }
        started { buffer[++n] = $0 }
        END {
            # Find last non-blank line
            last = n
            while (last > 0 && buffer[last] !~ /[^[:space:]]/) last--
            for (i = 1; i <= last; i++) print buffer[i]
        }
    '
}

if [[ "$version" == *"-rc"* ]]; then
    body="$(extract_section "Unreleased" | trim_blank_edges)"
    if [[ -z "$body" ]]; then
        body="(no changes recorded in [Unreleased])"
    fi
    printf '> **This is a release candidate — not intended for production use.**\n\n%s\n' "$body"
    exit 0
fi

body="$(extract_section "$version" | trim_blank_edges)"
if [[ -z "$body" ]]; then
    echo "no CHANGELOG section for version: $version" >&2
    exit 1
fi
printf '%s\n' "$body"
