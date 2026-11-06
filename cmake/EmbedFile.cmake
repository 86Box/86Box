#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake script embedding a file as a null-terminated C array.
#
#          Usage: cmake -DIN=<file> -DOUT=<header> -DNAME=<symbol> -P EmbedFile.cmake
#
# Authors: RichardG, <richardg867@gmail.com>
#
#          Copyright 2026 RichardG.
#

file(READ "${IN}" hex HEX)
string(REGEX REPLACE "(..)" "0x\\1," bytes "${hex}")
string(REPEAT "0x..," 16 line) # CMake regexes have no {n}
string(REGEX REPLACE "(${line})" "\\1\n    " bytes "${bytes}")
get_filename_component(in_name "${IN}" NAME)
file(WRITE "${OUT}.tmp" "/* Generated from ${in_name}, do not edit. */\nstatic const char ${NAME}[] = {\n    ${bytes}0x00\n};\n")
configure_file("${OUT}.tmp" "${OUT}" COPYONLY)
file(REMOVE "${OUT}.tmp")
