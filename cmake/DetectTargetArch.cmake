include_guard(GLOBAL)

# Detect the target architecture by trying to compile `src/arch_detect.c`
try_compile(RESULT_VAR ${CMAKE_BINARY_DIR} "${CMAKE_CURRENT_SOURCE_DIR}/src/arch_detect.c" OUTPUT_VARIABLE ARCH)
string(REGEX MATCH "ARCH ([a-zA-Z0-9_]+)" ARCH "${ARCH}")
string(REPLACE "ARCH " "" ARCH "${ARCH}")

if(NOT ARCH)
    set(ARCH unknown)
endif()

if (ARCH STREQUAL "x86_64")
    set(ARCH_X64 1)
elseif (ARCH STREQUAL "arm64")
    set(ARCH_ARM64 1)
endif()
