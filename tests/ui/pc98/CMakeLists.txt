cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(pc98_status_tests VERSION 0.0.0 LANGUAGES CXX)
    enable_testing()
    set(EMU_BUILD_NUM 0)
    configure_file(../../../src/include/86box/version.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/include/86box/version.h)
    find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets REQUIRED)
    find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets REQUIRED)
elseif(USE_QT6)
    set(QT_VERSION_MAJOR 6)
else()
    set(QT_VERSION_MAJOR 5)
endif()
add_executable(pc98_status_tests ../pc98_sasi_status_test.cpp)
target_include_directories(pc98_status_tests PRIVATE ../../../src/include
    ${PROJECT_BINARY_DIR}/src/include ${CMAKE_CURRENT_BINARY_DIR}/include)
target_compile_features(pc98_status_tests PRIVATE cxx_std_17)
target_link_libraries(pc98_status_tests PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
set_property(TARGET pc98_status_tests PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
target_compile_options(pc98_status_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
    $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU,Clang,AppleClang>>:-O1>)
target_link_options(pc98_status_tests PRIVATE
    $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU,Clang,AppleClang>>:-O1>
    $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
    $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<CXX_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>)
add_test(NAME pc98_sasi_status COMMAND pc98_status_tests)
