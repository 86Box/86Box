#!/usr/bin/env bash
#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          ATI Rage 128 Pro -- runs one test harness as parallel child
#          processes and sums their results. The shard.sh in reg-harness
#          and in jit-harness call it with the mode (reg or jit), the
#          harness directory, the process count and the harness's own
#          arguments. In reg mode each child runs a share of the vector
#          groups; in jit mode each child runs one slice of the fuzz
#          rows. Each child writes its own log, and the script prints one
#          summary line and fails when any child fails or does not report
#          its summary exactly once.
#
# Authors: skiretic.
#
#          Copyright 2026 skiretic.
#

# Each child inherits the caller's lane settings. Separate working directories
# isolate probe files, and logs remain available when a child exits or crashes.
set -uo pipefail
mode=$1
harness_dir=$2
shift 2
count=${1:-}
if [ -n "$count" ]; then
    shift
else
    count=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null ||
            getconf _NPROCESSORS_ONLN 2>/dev/null || printf '%s\n' "${NUMBER_OF_PROCESSORS:-1}")
fi
case "$count" in ''|*[!0-9]*|0*) echo "shard count must be a positive integer" >&2; exit 2;; esac
if [ "${#count}" -gt 10 ] || [ "$count" -gt 2147483647 ]; then
    echo "shard count is too large" >&2
    exit 2
fi
if [ "$mode" = jit ]; then
    binary=$harness_dir/jit_host_test
    if [ "$#" -gt 1 ]; then
        echo "usage: shard.sh [N [seed]]" >&2
        exit 2
    fi
    if ! [[ ${1:-0} =~ ^(0[xX][[:xdigit:]]+|[0-9]+)$ ]]; then
        echo "shard.sh runs the fuzz gate; run standalone probes directly" >&2
        exit 2
    fi
    if [ -n "${JHT_BIGPX_REPRO+x}" ]; then
        echo "run the standalone repro probe directly" >&2
        exit 2
    fi
else
    binary=$harness_dir/reg_harness
    if [ "$#" -gt 1 ] || { [ "$#" -eq 1 ] && [ "$1" != -v ]; }; then
        echo "usage: shard.sh [N [-v]]" >&2
        exit 2
    fi
    [ -x "$binary" ] || { echo "build the harness first: $binary" >&2; exit 2; }
    units=$("$binary" --list-groups) || exit $?
    groups=()
    i=0
    # A MinGW build writes the list in text mode, so each line ends in CR LF;
    # the CR would become part of the unit name the child is asked to run.
    while IFS= read -r unit; do
        unit=${unit%$'\r'}
        slot=$((i % count))
        groups[$slot]=${groups[$slot]:+${groups[$slot]},}$unit
        i=$((i + 1))
    done <<< "$units"
fi
[ -x "$binary" ] || { echo "build the harness first: $binary" >&2; exit 2; }
log_dir=${SHARD_LOG_DIR:-$(mktemp -d "${TMPDIR:-/tmp}/rage128-$mode-shards.XXXXXX")}
mkdir -p "$log_dir" || exit 2
log_dir=$(cd "$log_dir" && pwd) || exit 2
echo "shard logs: $log_dir" >&2
pids=()
stop_children() {
    for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null || true; done
    for pid in "${pids[@]}"; do wait "$pid" 2>/dev/null || true; done
    exit 130
}
trap stop_children INT TERM HUP
i=0
while [ "$i" -lt "$count" ]; do
    mkdir -p "$log_dir/work-$i" || { stop_children; }
    if [ "$mode" = jit ]; then
        (cd "$log_dir/work-$i" && JHT_SHARD=$i/$count "$binary" "$@") >"$log_dir/shard-$i.log" 2>&1 &
    else
        (cd "$log_dir/work-$i" && R128_GROUPS=${groups[$i]:-} "$binary" "$@") >"$log_dir/shard-$i.log" 2>&1 &
    fi
    pids[$i]=$!
    i=$((i + 1))
done
status=0
logs=()
i=0
while [ "$i" -lt "$count" ]; do
    if wait "${pids[$i]}"; then
        printf '0\n' >"$log_dir/shard-$i.status"
    else
        rc=$?
        printf '%s\n' "$rc" >"$log_dir/shard-$i.status"
        echo "shard $i exited $rc: $log_dir/shard-$i.log" >&2
        status=1
    fi
    logs[$i]=$log_dir/shard-$i.log
    i=$((i + 1))
done
trap - INT TERM HUP
# Exact summary matches exclude vector-local counters. Every child must report
# once; a missing summary is a failed gate even if the process exits zero.
awk -v mode="$mode" -v count="$count" '
    FNR == 1 { file++ }
    mode == "reg" && /^pass=[0-9]+ fail=[0-9]+\r?$/ {
        split($1, a, "="); split($2, b, "=")
        total += a[2]; fail += b[2]; seen[FILENAME]++; next
    }
    mode == "jit" && /^rows=[0-9]+ fail=[0-9]+\r?$/ {
        split($1, a, "="); split($2, b, "=")
        total += a[2]; fail += b[2]; seen[FILENAME]++; next
    }
    mode == "jit" && /^REF-ONLY / { next }
    mode == "reg" || file == 1 { print; next }
    /^MISMATCH|^REPLAY|^PROBE|^case .*failed|^case .*rejected/ { print > "/dev/stderr" }
    END {
        for (i = 1; i < ARGC; i++)
            if (seen[ARGV[i]] != 1) {
                print "missing or duplicate summary: " ARGV[i] > "/dev/stderr"
                bad = 1
            }
        printf "%s=%.0f fail=%.0f\n", (mode == "jit" ? "rows" : "pass"), total, fail
        if (bad || fail) exit 1
    }
' "${logs[@]}" || status=1
exit "$status"
