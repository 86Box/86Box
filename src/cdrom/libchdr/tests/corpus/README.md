# Fuzz Corpus

Seed inputs for `chdr-fuzz` (built with `-DBUILD_FUZZER=ON`).

This directory is gitignored except for the scripts below. Populate it
before running the fuzzer.

## Generate synthetic CHDv5 samples (fast, reproducible)

Requires `chdman` (from MAME) in `$PATH`.

    ./generate.sh

Produces tiny CHDs (each < 100 KB) covering:

- CHD types: raw, hard disk, CD-ROM
- Compression codecs: none, zlib, lzma, huff, flac, zstd, cdzl, cdlz,
  cdfl, cdzs

All outputs are CHDv5 — modern `chdman` emits v5 exclusively.

## Fetch real CHDv4 samples (optional)

    ./fetch.sh

Downloads a small set of publicly redistributable CHDv4 test images.
Skipped if network access is unavailable.

Generated `.chd` files land under `tests/corpus/seeds/`, which is
gitignored. The scripts and this README live at `tests/corpus/` and
are tracked.

## Run the fuzzer

    cmake -B build -DBUILD_FUZZER=ON -DCMAKE_C_COMPILER=clang
    cmake --build build --target chdr-fuzz
    ./build/tests/chdr-fuzz -max_len=131072 tests/corpus/seeds
