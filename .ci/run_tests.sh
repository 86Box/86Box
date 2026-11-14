#!/bin/sh
#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          Runs the tests in a build directory with CTest and compares the
#          failures with the lists in tests/known_failures (see the README
#          there). Fails if a test that isn't listed fails.
#
#          Usage: run_tests.sh <build directory> <os> <arch>
#          e.g.   run_tests.sh build linux x86_64
#

build=$(cd "$1" && pwd)
os=$2
arch=$3
lists=$(cd "$(dirname "$0")/../tests/known_failures" && pwd)

# A sweep that runs for hours
exclude='^Rage128\.JitHost$'

jobs=${NUMBER_OF_PROCESSORS:-$(getconf _NPROCESSORS_ONLN)}
ctest --test-dir "$build" --output-on-failure -j "$jobs" -E "$exclude"

# Sorted lists of the tests that ran, failed, and are known to fail here.
# Carriage returns are dropped, for Windows.
export LC_ALL=C
tmp=$(mktemp -d)
cd "$tmp" || exit 1
ctest --test-dir "$build" -N -E "$exclude" | tr -d '\r' |
    sed -n 's/^ *Test *#[0-9]*: //p' | sort -u > run
{ cut -d: -f2- "$build/Testing/Temporary/LastTestsFailed.log" 2>/dev/null || true; } |
    tr -d '\r' | sort -u > failed
for list in all "$os" "$arch" "$os-$arch"; do
    if [ -f "$lists/$list.txt" ]; then
        tr -d '\r' < "$lists/$list.txt"
    fi
done | sed -e 's/#.*//' -e 's/[[:space:]]*$//' -e 's/^[[:space:]]*//' -e '/^$/d' | sort -u > known
comm -23 failed known > new
comm -12 failed known > known_failed
comm -23 known failed | comm -12 - run > fixed
comm -23 known run > missing

# Annotations show 10 per type and step, so the known failures get one, and
# the summary lists all of them
lists_used="all.txt, $os.txt, $arch.txt or $os-$arch.txt in tests/known_failures"
while read -r test; do
    echo "::error title=New failure::$test"
done < new
if [ -s known_failed ]; then
    echo "::warning title=Known failures::$(wc -l < known_failed | tr -d ' ') listed tests failed, see the summary"
fi
while read -r test; do
    echo "::notice title=Known failure passes::$test passes, remove it from $lists_used"
done < fixed
while read -r test; do
    echo "::notice title=Not a test here::$test is listed but isn't a test in this job, remove it from $lists_used or move it to a list for the jobs that have it"
done < missing

section() {
    if [ -s "$2" ]; then
        printf '\n### %s\n\n' "$1"
        sed 's/.*/- `&`/' "$2"
    fi
}
summary=${GITHUB_STEP_SUMMARY:-/dev/stdout}
{
    echo "## $os $arch"
    echo
    echo "$(wc -l < run | tr -d ' ') tests, $(wc -l < failed | tr -d ' ') failed: $(wc -l < new | tr -d ' ') new, $(wc -l < known_failed | tr -d ' ') known"
    section "New failures" new
    section "Known failures that pass, to remove from the lists" fixed
    section "Listed tests that aren't tests in this job" missing
    section "Known failures" known_failed
} >> "$summary"

status=0
[ -s new ] && status=1
cd / && rm -rf "$tmp"
exit $status
