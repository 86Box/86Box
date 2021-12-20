include(${CMAKE_CURRENT_LIST_DIR}/flags-gcc-arm64.cmake)

set(CMAKE_C_COMPILER	clang)
set(CMAKE_CXX_COMPILER	clang++)
set(CMAKE_RC_COMPILER	rc)

set(CMAKE_C_COMPILER_TARGET	aarch64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET	aarch64-pc-windows-msvc)

set(CMAKE_SYSTEM_PROCESSOR	ARM64)