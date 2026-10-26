/* Disable unused features of miniz (but allow
   them to be restored by dependent projects). */
#ifndef MINIZ_ARCHIVE_APIS
#define MINIZ_NO_ARCHIVE_APIS
#endif

#ifndef MINIZ_DEFLATE_APIS
#define MINIZ_NO_DEFLATE_APIS
#endif

#ifndef MINIZ_STDIO
#define MINIZ_NO_STDIO
#endif

#ifndef MINIZ_TIME
#define MINIZ_NO_TIME
#endif

#include "deps/lzma-25.01/src/LzmaDec.c"
#include "deps/miniz-3.1.1/miniz.c"
#include "deps/zstd-1.5.7/zstddeclib.c"

#include "src/libchdr_bitstream.c"
#include "src/libchdr_cdrom.c"
#include "src/libchdr_chd.c"
#include "src/libchdr_codec_cdfl.c"
#include "src/libchdr_codec_cdlz.c"
#include "src/libchdr_codec_cdzl.c"
#include "src/libchdr_codec_cdzs.c"
#include "src/libchdr_codec_flac.c"
#include "src/libchdr_codec_huff.c"
#include "src/libchdr_codec_lzma.c"
#include "src/libchdr_codec_zlib.c"
#include "src/libchdr_codec_zstd.c"
#include "src/libchdr_flac.c"
#include "src/libchdr_huffman.c"
