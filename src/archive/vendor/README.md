# Archive decoders

The sources in this directory are checked in so the archive backend can build offline.

- miniz 3.1.2, read-only source subset from https://github.com/richgel999/miniz/releases/tag/3.1.2. See `miniz/LICENSE`.
- LZMA SDK 26.00, C decoder source subset from https://www.7-zip.org/a/lzma2600.7z. See `lzma-sdk/lzma-sdk.txt` for the public-domain notice.

The upstream source files are unmodified. The parent CMake file generates miniz's static export header locally.
