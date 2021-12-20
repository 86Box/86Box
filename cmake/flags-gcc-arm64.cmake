include(${CMAKE_CURRENT_LIST_DIR}/flags-gcc.cmake)

set(CMAKE_C_FLAGS	"-march=armv8-a -mfloat-abi=hard ${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS	"-march=armv8-a -mfloat-abi=hard ${CMAKE_CXX_FLAGS}")