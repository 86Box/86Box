include(${CMAKE_CURRENT_LIST_DIR}/flags-gcc.cmake)

set(CMAKE_C_FLAGS_INIT      "-march=armv8-a -mfloat-abi=hard ${CMAKE_C_FLAGS_INIT}")
set(CMAKE_CXX_FLAGS_INIT    "-march=armv8-a -mfloat-abi=hard ${CMAKE_CXX_FLAGS_INIT}")