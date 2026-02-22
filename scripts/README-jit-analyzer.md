# Voodoo JIT Log Analyzer

> **Location:** [`scripts/analyze-jit-log.c`](analyze-jit-log.c) (Linux/macOS) and [`scripts/analyze-jit-log-win32.c`](analyze-jit-log-win32.c) (Windows).
> Both are single-file C programs — no build system needed, just compile and run.

Standalone tool that parses `voodoo_jit.log` files and produces a health report covering block compilation, execution stats, pipeline coverage, pixel output, and JIT-vs-interpreter verify mismatches.

## Where to find

```
scripts/analyze-jit-log.c          # POSIX (Linux, macOS)
scripts/analyze-jit-log-win32.c    # Windows (MSVC or MinGW)
```

Both are single-file C programs with no external dependencies beyond the platform SDK.

## Building

**Linux / macOS:**
```bash
cc -O2 -o analyze-jit-log scripts/analyze-jit-log.c -lpthread
```

**Windows (MSVC):**
```cmd
cl /O2 /std:c11 scripts\analyze-jit-log-win32.c
```

**Windows (MinGW):**
```bash
gcc -O2 -o analyze-jit-log-win32.exe scripts/analyze-jit-log-win32.c
```

## Generating a log file

1. In the 86Box UI, open **Settings > Display > Voodoo** (or Banshee/Voodoo 3).
2. Enable **JIT Debug Logging**.
3. Run a 3D workload (game, benchmark, etc.).
4. Close the VM. The log file is written to your VM directory:
   ```
   <vm_dir>/voodoo_jit.log
   ```

### Debug levels

The `jit_debug` config option in the VM's `.cfg` file accepts:

| Value | Behavior |
|-------|----------|
| `0`   | Off (default). No log file, no performance impact. |
| `1`   | Logging only. Writes EXECUTE/POST entries with pixel dumps. |
| `2`   | Verify mode. Runs both JIT and interpreter per scanline, compares pixel-by-pixel, logs VERIFY MISMATCH on any difference. Slower but finds correctness bugs. |

To set level 2 manually, edit the VM's `.cfg` file:
```ini
[3Dfx Voodoo Graphics]
jit_debug = 2
```

## Running the analyzer

```bash
./analyze-jit-log /path/to/voodoo_jit.log
```

The tool uses all available CPU cores to parse the log in parallel. For a typical log (1-100 GB), expect ~30-180 seconds depending on file size and hardware.

## Report sections

| Section | What it shows |
|---------|---------------|
| **Configuration** | Init params (render threads, recompiler, jit_debug level) |
| **Compilation** | Block generate count, unique code addresses, recompilation range |
| **Errors** | Reject/fallback counts, stored error lines |
| **JIT Verify** | Mismatch count, per-fogMode breakdown, per-pipeline-config top 10, diff magnitude distribution |
| **Execution** | Execute/post counts, pixel histograms, scanline coverage |
| **Pipeline Coverage** | Unique fbzMode, fbzColorPath, alphaMode, textureMode, fogMode values |
| **Pixel Output** | Unique RGB565 colors, Z-value diversity |
| **Iterators** | Negative iterator counts (ir, ig, ib, ia) |
| **Summary** | One-line verdict: HEALTHY, FUNCTIONAL WITH WARNINGS, VERIFY MISMATCH, etc. |

## Example output

```
Voodoo JIT Log Analyzer
------------------------------------------------------------
File: voodoo_jit.log (42301.8 MB)
Threads: 8
  Scanned 1,342,567,890 lines in 164.2s (merge 0.3s)

  ...section details...

  VERDICT: HEALTHY
```

## Interpreting verify mismatches

If you see `VERIFY MISMATCH` entries, check:

- **diff magnitude 0-1**: Rounding differences, usually harmless
- **diff magnitude 2-3**: Minor precision issues, may be visible
- **diff magnitude 4+**: Likely a real bug in the JIT codegen

The per-fogMode and per-pipeline-config breakdowns help narrow down which pipeline configuration triggers the mismatch. Cross-reference with the `fbzMode`, `alphaMode`, `textureMode`, and `fogMode` register values to identify the codegen path.
