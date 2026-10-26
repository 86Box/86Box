#!/usr/bin/env bash
# Optional: fetch small publicly redistributable CHDv4 samples.
# The modern chdman only emits CHDv5, so this supplements generate.sh
# with legacy-format coverage for the fuzz corpus.
set -euo pipefail

CORPUS="$(cd "$(dirname "$0")" && pwd)/seeds"
mkdir -p "$CORPUS"

fetch () {
    local url="$1" out="$2"
    if [ -f "$CORPUS/$out" ]; then
        echo "  $out: already present"
        return
    fi
    if curl -fsSL --max-time 30 -o "$CORPUS/$out" "$url"; then
        echo "  $out: ok ($(stat -c%s "$CORPUS/$out") bytes)"
    else
        echo "  $out: fetch failed — skipping"
        rm -f "$CORPUS/$out"
    fi
}

# Add trusted small-CHDv4 URLs here when identified.
# Intentionally empty at the start; contributors can add sources that
# are small (< 1 MiB), publicly redistributable, and pin a known hash.
echo "(no CHDv4 sources configured yet — edit tests/corpus/fetch.sh)"
