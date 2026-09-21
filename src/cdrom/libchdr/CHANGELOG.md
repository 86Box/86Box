# Changelog

All notable changes to libchdr are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versioning follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.3.0] - Unreleased

First formally tagged release. Project has been depended on by
RetroArch, DuckStation, Flycast, SwanStation and others for years;
this tag exists so Linux distros can ship a stable reference point
instead of a dated snapshot.

### Added

- CHDv5 read support (backported from MAME C++).
- Zstandard (zstd) compression codec.
- `CHDR_WANT_RAW_DATA_SECTOR`, `CHDR_WANT_SUBCODE`,
  `CHDR_VERIFY_BLOCK_CRC` CMake options for reduced-scope builds.
- `WITH_SYSTEM_ZLIB` and `WITH_SYSTEM_ZSTD` CMake options for distros.
- `BUILD_FUZZER` CMake option (libFuzzer + ASan) and `tests/fuzz.c`.
- `BUILD_LTO` CMake option.
- `chd_read_header_core_file_callbacks` public API.
- pkg-config file (`libchdr.pc`) installed alongside the library.
- Unity-build script (`unity.c`).

### Changed

- Library is now split across per-codec translation units
  (`src/libchdr_codec_*.c`) instead of one monolithic `libchdr_chd.c`.
- Internal codec headers moved from `include/libchdr/` to `src/` so
  they are no longer part of the installed header set (#144).
- `cd_codec_decompress` signature is now stable across builds; it no
  longer changes with the `WANT_SUBCODE` preprocessor flag (#144).
- `lzma_allocator` now embeds `ISzAlloc` as its first member, matching
  the LZMA SDK's expected layout (#144).
- Bundled `zlib` replaced with `miniz` 3.1.1 (single-file).
- Bundled `zstd` replaced with a single-file version of itself
  (1.5.7).
- Bundled LZMA updated to 25.01.
- Bundled `dr_flac` updated to 0.13.3 (includes fix for
  CVE-2025-14369 integer overflow).
- pkg-config `Version:` now emits the full `MAJOR.MINOR.PATCH`.
- Exported symbols are restricted to `chd_*` via a linker version
  script on ELF targets, `-exported_symbol _chd_*` on macOS.

### Fixed

- `chd_read_header_core_file_callbacks` now works for CHD versions
  below 4 (#146).
- v1/v2 header: `hunkbytes` is computed in `uint64_t` and rejected if
  the product of `seclen * obsolete_hunksize` overflows `uint32_t` or
  is zero (#148).
- `metadata_find_entry` caps traversal at 65536 entries, preventing
  unbounded seek+read loops on malformed CHDs with cyclic
  `next`-pointer chains (#148).
- `fseeko` / `ftello` are now declared under strict C11 builds
  (`-Werror=implicit-function-declaration`) and on Debian armhf
  `time64` rebuilds: feature test macros are defined before any
  system header include, and the `fseeko64`/`ftello64` branch has
  been removed in favor of LFS-aliased `fseeko`/`ftello` (#92, #117).
- `chd_get_metadata` fallback now uses `snprintf` instead of
  `sprintf`.
- `fseek` errors are now propagated as `CHDERR_READ_ERROR`.
- Only reads as much header data as the declared header length.
- Various memory and overflow bugs in hunk decoding (#132).

### Removed

- Bundled `zlib-1.3.1/` directory (dead since the switch to miniz).

### Known limitations

- **AVHuff codec is not implemented** (#69). CHDs produced with the
  `CHDCOMPRESSION_AV` codec (CHDv1-v4) or the `avhu` codec tag (CHDv5)
  return `CHDERR_UNSUPPORTED_FORMAT`. In practice this only affects
  laserdisc CHDs (Dragon's Lair, Space Ace, Time Traveler, and other
  MAME/Daphne/Hypseus Singe targets). All other consumers (PS1, PS2,
  Saturn, Dreamcast, arcade HD, etc.) are unaffected.

## ABI

`SOVERSION = PROJECT_VERSION_MAJOR`. Bumped only on ABI breaks. The
`v0.x` series ships as `libchdr.so.0`; a future `v1.0.0` would ship
as `libchdr.so.1`. The public header set is restricted to
`include/libchdr/` and the public symbol set to `chd_*`.

[0.3.0]: https://github.com/rtissera/libchdr/releases/tag/v0.3.0
