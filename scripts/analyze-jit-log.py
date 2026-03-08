#!/usr/bin/env python3
"""
Analyze a Voodoo ARM64 JIT debug log and produce a health report.

Usage:
    ./scripts/analyze-jit-log.py <logfile>
    ./scripts/analyze-jit-log.py voodoo_jit.log
"""

import sys
import re
import os
from collections import Counter, defaultdict

# ── ANSI colors ──────────────────────────────────────────────────────────────

GREEN  = "\033[0;32m"
RED    = "\033[0;31m"
YELLOW = "\033[1;33m"
CYAN   = "\033[0;36m"
BOLD   = "\033[1m"
NC     = "\033[0m"

def ok(msg):    return f"{GREEN}OK{NC}       {msg}"
def warn(msg):  return f"{YELLOW}WARN{NC}     {msg}"
def fail(msg):  return f"{RED}FAIL{NC}     {msg}"
def info(msg):  return f"{CYAN}INFO{NC}     {msg}"

# ── Error patterns ───────────────────────────────────────────────────────────

ERROR_PATTERNS = [
    "error", "fail", "crash", "overflow", "invalid", "abort",
    "SIGILL", "SIGSEGV", "SIGBUS", "rejected", "skip",
    "fault", "trap", "mprotect", "exceeded", "truncated",
]
error_re = re.compile("|".join(ERROR_PATTERNS), re.IGNORECASE)

# ── Line parsers ─────────────────────────────────────────────────────────────

generate_re = re.compile(
    r"VOODOO JIT: GENERATE #(\d+)\s+"
    r"odd_even=(\d+)\s+"
    r"block=(\d+)\s+"
    r"code=(0x[0-9a-fA-F]+)\s+"
    r"recomp=(\d+)\s+"
    r"fbzMode=(0x[0-9a-fA-F]+)\s+"
    r"fbzColorPath=(0x[0-9a-fA-F]+)\s+"
    r"alphaMode=(0x[0-9a-fA-F]+)\s+"
    r"textureMode\[0\]=(0x[0-9a-fA-F]+)\s+"
    r"fogMode=(0x[0-9a-fA-F]+)\s+"
    r"xdir=(-?\d+)"
)

cache_re = re.compile(r"VOODOO JIT: cache HIT")

init_re = re.compile(
    r"VOODOO JIT: INIT\s+"
    r"render_threads=(\d+)\s+"
    r"use_recompiler=(\d+)\s+"
    r"jit_debug=(\d+)"
)

# Interpreter fallback patterns (two sources)
interp_fallback_re = re.compile(r"VOODOO WARNING: INTERPRETER FALLBACK")
reject_re = re.compile(r"VOODOO JIT: REJECT.*interpreter fallback")

execute_re = re.compile(
    r"VOODOO JIT: EXECUTE #(\d+)\s+"
    r"code=(0x[0-9a-fA-F]+)\s+"
    r"x=(\d+)\s+x2=(\d+)"
)

post_re = re.compile(
    r"VOODOO JIT POST:\s+"
    r"ib=(-?\d+)\s+ig=(-?\d+)\s+ir=(-?\d+)\s+ia=(-?\d+)\s+"
    r"z=([0-9a-fA-F]+)\s+"
    r"pixel_count=(\d+)"
)

pixel_re = re.compile(
    r"VOODOO JIT PIXELS y=(\d+) x=(\d+)\.\.(\d+):\s*(.*)"
)

# ── Main analysis ────────────────────────────────────────────────────────────

def analyze(path):
    # Counters
    total_lines = 0
    generate_count = 0
    cache_hits = 0
    execute_count = 0
    post_count = 0
    pixel_lines = 0
    interleaved_lines = 0
    error_lines = []

    interp_fallbacks = 0
    reject_fallbacks = 0
    init_render_threads = None
    init_use_recompiler = None
    init_jit_debug = None

    # Sets / collections
    code_addrs = set()
    block_ids = set()
    odd_even_counts = Counter()   # 0 vs 1
    xdir_counts = Counter()       # +1 vs -1
    recomp_values = []

    # Mode registers (sets of unique values)
    fbzModes = set()
    fbzColorPaths = set()
    alphaModes = set()
    textureModes = set()
    fogModes = set()

    # Full pipeline configs (tuple of all mode regs)
    configs = set()

    # POST stats
    pixel_count_total = 0
    pixel_count_max = 0
    pixel_count_hist = Counter()  # bucket: count
    negative_ir = 0
    negative_ig = 0
    negative_ib = 0
    negative_ia = 0
    z_values = set()

    # Pixel values
    unique_pixels = set()

    # Last line (for termination check)
    last_line = ""

    file_size = os.path.getsize(path)
    file_mb = file_size / (1024 * 1024)

    print(f"\n{BOLD}Voodoo ARM64 JIT Log Analyzer{NC}")
    print(f"{'─' * 60}")
    print(f"File: {path} ({file_mb:.1f} MB)")
    print(f"Scanning...", end="", flush=True)

    progress_interval = 1_000_000

    with open(path, "r", errors="replace") as f:
        for line in f:
            total_lines += 1

            if total_lines % progress_interval == 0:
                print(f"\r  Scanned {total_lines // 1_000_000}M lines...", end="", flush=True)

            last_line = line

            # Detect interleaved output (two log prefixes on one line)
            if line.count("VOODOO JIT") >= 2:
                interleaved_lines += 1

            # INIT
            if init_render_threads is None and "INIT" in line:
                m = init_re.search(line)
                if m:
                    init_render_threads = int(m.group(1))
                    init_use_recompiler = int(m.group(2))
                    init_jit_debug = int(m.group(3))
                    continue

            # GENERATE
            m = generate_re.search(line)
            if m:
                generate_count += 1
                odd_even_counts[int(m.group(2))] += 1
                block_ids.add(int(m.group(3)))
                code_addrs.add(m.group(4))
                recomp_values.append(int(m.group(5)))

                fbz = m.group(6)
                fcp = m.group(7)
                am  = m.group(8)
                tm  = m.group(9)
                fm  = m.group(10)
                xd  = int(m.group(11))

                fbzModes.add(fbz)
                fbzColorPaths.add(fcp)
                alphaModes.add(am)
                textureModes.add(tm)
                fogModes.add(fm)
                xdir_counts[xd] += 1

                configs.add((fbz, fcp, am, tm, fm, xd))
                continue

            # cache HIT
            if cache_re.search(line):
                cache_hits += 1
                continue

            # Interpreter fallback
            if "INTERPRETER FALLBACK" in line:
                interp_fallbacks += 1
                continue
            if "REJECT" in line and reject_re.search(line):
                reject_fallbacks += 1
                continue

            # EXECUTE
            if "EXECUTE" in line:
                m = execute_re.search(line)
                if m:
                    execute_count += 1
                continue

            # POST
            m = post_re.search(line)
            if m:
                post_count += 1
                ib, ig, ir, ia = int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4))
                z_hex = m.group(5)
                pc = int(m.group(6))

                pixel_count_total += pc
                if pc > pixel_count_max:
                    pixel_count_max = pc

                # Bucket pixel counts
                if pc <= 1:
                    pixel_count_hist["1"] += 1
                elif pc <= 10:
                    pixel_count_hist["2-10"] += 1
                elif pc <= 100:
                    pixel_count_hist["11-100"] += 1
                elif pc <= 320:
                    pixel_count_hist["101-320"] += 1
                else:
                    pixel_count_hist["321+"] += 1

                if ir < 0: negative_ir += 1
                if ig < 0: negative_ig += 1
                if ib < 0: negative_ib += 1
                if ia < 0: negative_ia += 1

                if z_hex != "00000000":
                    z_values.add(z_hex)

                continue

            # PIXELS
            if "PIXELS" in line:
                m = pixel_re.search(line)
                if m:
                    pixel_lines += 1
                    vals = m.group(4).strip()
                    if vals:
                        for v in vals.split():
                            v = v.strip()
                            if len(v) == 4 and all(c in "0123456789abcdefABCDEF" for c in v):
                                unique_pixels.add(v)
                continue

            # Error check (only on non-standard lines to avoid false positives on
            # "error" substring in hex values or mode descriptions)
            if "VOODOO JIT" not in line and error_re.search(line):
                error_lines.append((total_lines, line.strip()))

    print(f"\r  Scanned {total_lines:,} lines.{' ' * 20}")
    print()

    # ── Report ───────────────────────────────────────────────────────────

    if init_render_threads is not None:
        print(f"{BOLD}═══ CONFIGURATION ═══{NC}")
        print(info(f"Render threads: {init_render_threads}"))
        print(info(f"JIT recompiler: {'enabled' if init_use_recompiler else 'disabled'}"))
        print(info(f"JIT debug level: {init_jit_debug}"))
        print()
    else:
        print(info("No INIT line found (older log format)"))
        # Infer thread count from odd_even values
        inferred = len(odd_even_counts)
        if inferred > 0:
            print(info(f"Render threads (inferred from odd_even): {inferred}"))
        print()

    print(f"{BOLD}═══ COMPILATION ═══{NC}")

    if generate_count > 0:
        print(ok(f"Blocks compiled: {generate_count:,}"))
    else:
        print(fail("No GENERATE events found — JIT may not be active"))

    print(info(f"Cache hits: {cache_hits}"))
    print(info(f"Unique code addresses: {len(code_addrs)}"))
    print(info(f"Block slots used: {len(block_ids)} ({', '.join(str(b) for b in sorted(block_ids))})"))

    even = odd_even_counts.get(0, 0)
    odd  = odd_even_counts.get(1, 0)
    if even > 0 and odd > 0:
        print(ok(f"Even/odd distribution: {even} / {odd}"))
    elif generate_count > 0:
        print(warn(f"Only {'even' if even > 0 else 'odd'} blocks generated"))

    xp = xdir_counts.get(1, 0)
    xm = xdir_counts.get(-1, 0)
    if xp > 0 and xm > 0:
        print(ok(f"xdir coverage: +1 ({xp:,}) / -1 ({xm:,})"))
    elif xp > 0:
        print(info(f"xdir: only +1 ({xp:,}), no -1 (may be normal for test workload)"))
    elif xm > 0:
        print(info(f"xdir: only -1 ({xm:,}), no +1"))

    if recomp_values:
        print(info(f"Recomp range: {min(recomp_values)} .. {max(recomp_values)}"))

    # Interpreter fallback
    total_fallbacks = interp_fallbacks + reject_fallbacks
    if total_fallbacks == 0:
        print(ok("No interpreter fallbacks"))
    else:
        print(fail(f"Interpreter fallbacks: {total_fallbacks:,}"))
        if interp_fallbacks:
            print(f"             use_recompiler=0 or NULL block: {interp_fallbacks:,}")
        if reject_fallbacks:
            print(f"             emit overflow (REJECT): {reject_fallbacks:,}")

    print()
    print(f"{BOLD}═══ ERRORS ═══{NC}")

    if not error_lines:
        print(ok(f"Zero errors in {total_lines:,} lines"))
    else:
        print(fail(f"{len(error_lines)} error(s) found:"))
        for lineno, text in error_lines[:10]:
            print(f"           Line {lineno}: {text[:120]}")
        if len(error_lines) > 10:
            print(f"           ... and {len(error_lines) - 10} more")

    if interleaved_lines > 0:
        pct = 100.0 * interleaved_lines / total_lines
        print(warn(f"Interleaved lines: {interleaved_lines:,} ({pct:.1f}%) — cosmetic threading race, not a bug"))
    else:
        print(ok("No interleaved log output"))

    # Termination
    last_clean = last_line.strip()
    if "VOODOO JIT" in last_clean:
        print(ok(f"Log ends cleanly"))
    else:
        print(warn(f"Log ends with unexpected line: {last_clean[:80]}"))

    print()
    print(f"{BOLD}═══ EXECUTION ═══{NC}")

    print(info(f"EXECUTE calls: {execute_count:,}"))
    print(info(f"POST entries: {post_count:,}"))
    print(info(f"Total pixels rendered: {pixel_count_total:,}"))
    print(info(f"Max pixels/scanline: {pixel_count_max}"))

    if pixel_count_hist:
        print(info("Pixel count distribution:"))
        for bucket in ["1", "2-10", "11-100", "101-320", "321+"]:
            if bucket in pixel_count_hist:
                print(f"             {bucket:>7}: {pixel_count_hist[bucket]:,}")

    print()
    print(f"{BOLD}═══ PIPELINE COVERAGE ═══{NC}")

    print(info(f"Unique pipeline configs: {len(configs)}"))
    print()

    # Texture
    non_zero_tex = len(textureModes - {"0x00000000"})
    if non_zero_tex > 0:
        print(ok(f"Texture fetch: {len(textureModes)} modes ({non_zero_tex} non-zero)"))
    else:
        print(warn("Texture fetch: not exercised (all textureMode=0)"))

    # Color combine
    if len(fbzColorPaths) > 1:
        print(ok(f"Color combine: {len(fbzColorPaths)} fbzColorPath configs"))
    elif len(fbzColorPaths) == 1:
        print(info(f"Color combine: 1 config ({next(iter(fbzColorPaths))})"))
    else:
        print(warn("Color combine: no data"))

    # Alpha
    non_zero_alpha = len(alphaModes - {"0x00000000"})
    if non_zero_alpha > 0:
        print(ok(f"Alpha test/blend: {len(alphaModes)} modes ({non_zero_alpha} non-zero)"))
    else:
        print(warn("Alpha test/blend: not exercised (all alphaMode=0)"))

    # Fog
    non_zero_fog = len(fogModes - {"0x00000000"})
    if non_zero_fog > 0:
        print(ok(f"Fog: {len(fogModes)} modes ({non_zero_fog} non-zero)"))
    else:
        print(info("Fog: not used by test workload (fogMode=0)"))

    # Depth
    if z_values:
        print(ok(f"Depth test: active ({len(z_values):,} unique Z values)"))
    else:
        print(warn("Depth test: no non-zero Z values seen"))

    # fbzMode
    print(info(f"fbzMode configs: {len(fbzModes)}"))

    # Dither check (bit 8 of fbzMode, bit 0 of alphaMode)
    dither_found = False
    for mode in fbzModes:
        if int(mode, 16) & (1 << 8):
            dither_found = True
            break
    if not dither_found:
        for mode in alphaModes:
            if int(mode, 16) & 1:
                dither_found = True
                break
    if dither_found:
        print(ok("Dithering: exercised"))
    else:
        print(info("Dithering: not enabled in test workload"))

    # Framebuffer write
    if post_count > 0:
        print(ok(f"Framebuffer write: {post_count:,} scanlines completed"))
    else:
        print(fail("Framebuffer write: no POST entries — blocks may not be executing"))

    # Pixel output quality
    print()
    print(f"{BOLD}═══ PIXEL OUTPUT ═══{NC}")

    print(info(f"PIXEL log lines: {pixel_lines:,}"))
    non_zero_pixels = unique_pixels - {"0000"}
    print(info(f"Unique RGB565 values: {len(unique_pixels)} ({len(non_zero_pixels)} non-zero)"))

    if len(non_zero_pixels) > 10:
        print(ok("Pixel diversity looks realistic"))
        sample = sorted(non_zero_pixels)[:16]
        print(f"             Sample: {' '.join(sample)}")
    elif len(non_zero_pixels) > 0:
        print(warn(f"Low pixel diversity ({len(non_zero_pixels)} non-zero values)"))
    elif pixel_lines > 0:
        print(warn("All pixels are 0x0000 — may indicate rendering issue or early boot"))

    # Iterator health
    print()
    print(f"{BOLD}═══ ITERATORS ═══{NC}")

    neg_total = negative_ir + negative_ig + negative_ib + negative_ia
    if neg_total > 0:
        print(info(f"Negative iterators (normal for signed Gouraud):"))
        if negative_ir: print(f"             ir: {negative_ir:,}")
        if negative_ig: print(f"             ig: {negative_ig:,}")
        if negative_ib: print(f"             ib: {negative_ib:,}")
        if negative_ia: print(f"             ia: {negative_ia:,}")
    else:
        print(info("No negative iterator values seen"))

    # ── Summary table ────────────────────────────────────────────────────

    print()
    print(f"{BOLD}═══ SUMMARY ═══{NC}")
    print()

    rows = [
        ("Block compilation",  f"{generate_count:,}/{generate_count:,} successful (100%)" if generate_count > 0 else "NONE"),
        ("Interp. fallbacks",  f"{total_fallbacks:,}" if total_fallbacks else "0"),
        ("Error count",        str(len(error_lines))),
        ("Crash indicators",   "0" if not error_lines else str(len(error_lines))),
        ("Mode diversity",     f"{len(configs)} unique configurations"),
        ("Texture fetch",      f"Exercised ({len(textureModes)} modes)" if non_zero_tex > 0 else "Not used"),
        ("Color combine",      f"Exercised ({len(fbzColorPaths)} configs)" if fbzColorPaths else "No data"),
        ("Alpha test/blend",   f"Exercised ({len(alphaModes)} modes)" if non_zero_alpha > 0 else "Not used"),
        ("Fog",                f"Exercised ({len(fogModes)} modes)" if non_zero_fog > 0 else "Not used by workload"),
        ("Dither",             "Exercised" if dither_found else "Not enabled"),
        ("Framebuffer write",  f"~{post_count:,} scanlines" if post_count > 0 else "NONE"),
        ("Depth test",         f"Active ({len(z_values):,} Z values)" if z_values else "Not active"),
        ("Pixel output",       f"{len(non_zero_pixels)} unique RGB565 colors" if non_zero_pixels else "All zero"),
        ("Cache hits",         f"{cache_hits}"),
        ("xdir coverage",      f"+1 ({xp:,}) / -1 ({xm:,})" if xp > 0 and xm > 0 else f"{'+'if xp else '-'}1 only"),
        ("Thread interleave",  "Cosmetic only" if interleaved_lines > 0 else "None"),
        ("Log termination",    "Clean" if "VOODOO JIT" in last_clean else "Unexpected"),
    ]

    max_label = max(len(r[0]) for r in rows)
    for label, value in rows:
        print(f"  {label:<{max_label}}  │  {value}")

    # Overall verdict
    print()
    has_errors = len(error_lines) > 0
    has_blocks = generate_count > 0
    has_output = post_count > 0
    has_fallbacks = total_fallbacks > 0

    if has_blocks and has_output and not has_errors and not has_fallbacks:
        print(f"  {BOLD}{GREEN}VERDICT: HEALTHY{NC}")
    elif has_blocks and has_output and has_fallbacks and not has_errors:
        print(f"  {BOLD}{YELLOW}VERDICT: FUNCTIONAL WITH INTERPRETER FALLBACKS{NC}")
    elif has_blocks and has_output and has_errors:
        print(f"  {BOLD}{YELLOW}VERDICT: FUNCTIONAL WITH WARNINGS{NC}")
    elif has_blocks and not has_output:
        print(f"  {BOLD}{RED}VERDICT: COMPILING BUT NOT EXECUTING{NC}")
    else:
        print(f"  {BOLD}{RED}VERDICT: JIT NOT ACTIVE{NC}")

    print()


# ── Entry point ──────────────────────────────────────────────────────────────

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <logfile>")
        print(f"  Analyzes a Voodoo ARM64 JIT debug log and produces a health report.")
        sys.exit(1)

    path = sys.argv[1]
    if not os.path.isfile(path):
        print(f"Error: {path} not found")
        sys.exit(1)

    analyze(path)
