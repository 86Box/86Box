add_library(chdr-lzma STATIC
  include/LzmaDec.h
  src/LzmaDec.c
)

set_target_properties(chdr-lzma PROPERTIES POSITION_INDEPENDENT_CODE ON)

option(WITH_LZMA_ASM "Use lzma asm" ON)
if(WITH_LZMA_ASM)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(CheckSymbolExists)
    check_symbol_exists("__aarch64__" "" CPU_ARM64)
    if(CPU_ARM64)
      get_filename_component(_lzma_c_compiler_name "${CMAKE_C_COMPILER}" NAME_WE)
      if(_lzma_c_compiler_name STREQUAL "ccache" AND CMAKE_C_COMPILER_ARG1)
        if((NOT CMAKE_ASM_COMPILER AND "$ENV{ASM}" STREQUAL "") OR
           (CMAKE_ASM_COMPILER STREQUAL CMAKE_C_COMPILER AND
            (NOT CMAKE_ASM_COMPILER_ARG1 OR CMAKE_ASM_COMPILER_ARG1 STREQUAL CMAKE_C_COMPILER_ARG1)))
          # ASM vendor detection does not pass COMPILER_ARG1 to the compiler.
          get_filename_component(_lzma_asm_compiler "${CMAKE_C_COMPILER_ARG1}"
                                 PROGRAM PROGRAM_ARGS _lzma_asm_compiler_args)
          if(NOT _lzma_asm_compiler)
            message(FATAL_ERROR "Cannot find the ASM compiler in '${CMAKE_C_COMPILER_ARG1}'")
          endif()
          set(CMAKE_ASM_COMPILER "${_lzma_asm_compiler}" CACHE FILEPATH "ASM compiler" FORCE)
          set(CMAKE_ASM_COMPILER_ARG1 "${_lzma_asm_compiler_args}" CACHE STRING "ASM compiler arguments" FORCE)
        endif()
      endif()
      enable_language(ASM)
      set_source_files_properties(src/LzmaDec.c PROPERTIES COMPILE_DEFINITIONS Z7_LZMA_DEC_OPT)
      target_sources(chdr-lzma PRIVATE Asm/arm64/LzmaDecOpt.S)
      set_source_files_properties(Asm/arm64/LzmaDecOpt.S PROPERTIES LANGUAGE ASM)
    endif()
  elseif(WIN32)
    include(CheckSymbolExists)
    check_symbol_exists("_M_AMD64" "" CPU_X64)
    if(CPU_X64)
      enable_language(ASM_MASM)
      set_source_files_properties(src/LzmaDec.c PROPERTIES COMPILE_DEFINITIONS Z7_LZMA_DEC_OPT)
      target_sources(chdr-lzma PRIVATE Asm/x86/LzmaDecOpt.asm)
      set_source_files_properties(Asm/x86/LzmaDecOpt.asm PROPERTIES LANGUAGE ASM_MASM)
    endif()
  endif()
endif()
