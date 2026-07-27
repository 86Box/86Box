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
