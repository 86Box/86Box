#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake build script for bddisasm, the memory viewer's disassembler.
#
#          The decoder and formatter of bddisasm v3.1.0 (commit f0c0e90,
#          https://github.com/bitdefender/bddisasm), Apache-2.0 (LICENSE).
#          The emulator (bdshemu), tools and tests are left out.
#

add_library(bddisasm STATIC
    bddisasm/bddisasm_crt.c
    bddisasm/bdx86_decoder.c
    bddisasm/bdx86_formatter.c
    bddisasm/bdx86_helpers.c
    bddisasm/bdx86_idbe.c
    bddisasm/bdx86_operand.c
)
set_target_properties(bddisasm PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF C_STANDARD 11)
target_compile_definitions(bddisasm PUBLIC BDDISASM_HAS_VSNPRINTF BDDISASM_HAS_MEMSET)
target_include_directories(bddisasm PRIVATE bddisasm/include)
target_include_directories(bddisasm SYSTEM PUBLIC inc)
# Upstream code, left as it is.
target_compile_options(bddisasm PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-incompatible-pointer-types-discards-qualifiers>)
