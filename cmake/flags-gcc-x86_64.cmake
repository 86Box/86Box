include(${CMAKE_CURRENT_LIST_DIR}/flags-gcc.cmake)

set(CMAKE_C_FLAGS_INIT      "-m64 -march=x86-64 -msse2 -mfpmath=sse ${CMAKE_C_FLAGS_INIT}")
set(CMAKE_CXX_FLAGS_INIT    "-m64 -march=x86-64 -msse2 -mfpmath=sse ${CMAKE_CXX_FLAGS_INIT}")