#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake script generating a polkit policy, with its <description>
#          and <message> translated from individual language .po files.
#
#          Usage: cmake -DIN=<template> -DOUT=<policy> -DPO_DIR=<dir>
#                       -DHELPER=<path> -P PolkitPolicy.cmake
#
# Authors: RichardG, <richardg867@gmail.com>
#
#          Copyright 2026 RichardG.
#

# Look a message ID up in a .po file's contents, giving its translation or nothing.
function(po_lookup po msgid out)
    set(${out} "" PARENT_SCOPE)
    string(FIND "${po}" "\nmsgid \"${msgid}\"\nmsgstr " pos)
    if(pos EQUAL -1)
        return()
    endif()
    string(LENGTH "\nmsgid \"${msgid}\"\nmsgstr " len)
    math(EXPR pos "${pos} + ${len}")
    string(SUBSTRING "${po}" ${pos} -1 rest)

    # The translation is one or more quoted strings, one per line.
    set(str "")
    while(rest MATCHES "^\"(([^\"\\\\]|\\\\.)*)\"\n?")
        string(APPEND str "${CMAKE_MATCH_1}")
        string(LENGTH "${CMAKE_MATCH_0}" len)
        string(SUBSTRING "${rest}" ${len} -1 rest)
    endwhile()

    # Undo the C escapes, then escape for XML.
    string(REPLACE "\\\"" "\"" str "${str}")
    string(REPLACE "\\n" " " str "${str}")
    string(REPLACE "\\\\" "\\" str "${str}")
    string(REPLACE "&" "&amp;" str "${str}")
    string(REPLACE "<" "&lt;" str "${str}")
    string(REPLACE ">" "&gt;" str "${str}")
    set(${out} "${str}" PARENT_SCOPE)
endfunction()

file(READ "${IN}" policy)
file(GLOB po_files "${PO_DIR}/*.po")
list(SORT po_files)

foreach(tag description message)
    if(NOT policy MATCHES "( *)<${tag}>([^<]*)</${tag}>\n")
        message(FATAL_ERROR "No <${tag}> in ${IN}")
    endif()
    set(element "${CMAKE_MATCH_0}")
    set(indent "${CMAKE_MATCH_1}")
    set(text "${CMAKE_MATCH_2}")

    set(translated "${element}")
    foreach(po_file ${po_files})
        file(READ "${po_file}" po)
        if(NOT po MATCHES "\"X-Language: ([A-Za-z_]+)\\\\n\"")
            continue()
        endif()
        set(lang "${CMAKE_MATCH_1}")
        po_lookup("${po}" "${text}" str)
        if(NOT str STREQUAL "")
            string(APPEND translated "${indent}<${tag} xml:lang=\"${lang}\">${str}</${tag}>\n")
        endif()
    endforeach()
    string(REPLACE "${element}" "${translated}" policy "${policy}")
endforeach()

string(REPLACE "@VFIO_SETUP_HELPER@" "${HELPER}" policy "${policy}")
file(WRITE "${OUT}.tmp" "${policy}")
configure_file("${OUT}.tmp" "${OUT}" COPYONLY)
file(REMOVE "${OUT}.tmp")
