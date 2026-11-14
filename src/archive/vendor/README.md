# LZMA SDK decoder

The 7z backend builds the vendored LZMA SDK 26.00 C decoder source subset from
[the upstream SDK](https://www.7-zip.org/a/lzma2600.7z). See
[`lzma-sdk/lzma-sdk.txt`](lzma-sdk/lzma-sdk.txt) for the public-domain notice.
The upstream source files are unmodified.

The ZIP backend links the installed minizip library through pkg-config.
