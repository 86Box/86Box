include(${CMAKE_CURRENT_LIST_DIR}/flags-gcc.cmake)

set(CMAKE_C_FLAGS	"-m64 -march=x86-64 -msse2 -mfpmath=sse ${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS	"-m64 -march=x86-64 -msse2 -mfpmath=sse ${CMAKE_CXX_FLAGS}")