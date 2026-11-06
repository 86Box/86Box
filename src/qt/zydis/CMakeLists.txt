#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake build script for Zydis, the memory viewer's disassembler.
#
#          The decoder and formatter of Zydis v4.1.1 (commit a2278f1,
#          https://github.com/zyantific/zydis), with the parts of Zycore
#          (commit 0b2432c, https://github.com/zyantific/zycore-c) they
#          use; both MIT (zydis/LICENSE, zycore/LICENSE). Left out: the
#          encoder (Encoder.c, EncoderData.c, EncoderTables.inc,
#          GetRelInfo.inc and their headers), Zycore's OS APIs, lists, bit
#          sets and argument parser, and the tools, tests and examples.
#          The sources are otherwise as released; ZYDIS_VERSION in
#          Zydis.h says 4.1.0, as it does in the v4.1.1 release.
#

add_library(zydis STATIC
    zycore/src/Allocator.c
    zycore/src/Format.c
    zycore/src/String.c
    zycore/src/Vector.c
    zycore/src/Zycore.c

    zydis/src/Decoder.c
    zydis/src/DecoderData.c
    zydis/src/Disassembler.c
    zydis/src/Formatter.c
    zydis/src/FormatterATT.c
    zydis/src/FormatterBase.c
    zydis/src/FormatterBuffer.c
    zydis/src/FormatterIntel.c
    zydis/src/MetaInfo.c
    zydis/src/Mnemonic.c
    zydis/src/Register.c
    zydis/src/Segment.c
    zydis/src/SharedData.c
    zydis/src/String.c
    zydis/src/Utils.c
    zydis/src/Zydis.c
)
set_target_properties(zydis PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF C_STANDARD 11)
target_compile_definitions(zydis PUBLIC ZYCORE_STATIC_BUILD ZYDIS_STATIC_BUILD ZYDIS_DISABLE_ENCODER)
target_include_directories(zydis PRIVATE zydis/src)
target_include_directories(zydis SYSTEM PUBLIC zydis/include zycore/include)
