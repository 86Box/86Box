cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(pc98_sound_tests C CXX)
    enable_testing()
endif()
add_executable(pc98_sound_tests
    ../pc98_sound_test.c
    ../../../src/sound/snd_pc98_spb.c
    ../../../src/sound/snd_pc98_26k.c
    ../../../src/sound/snd_pc98_opn.cpp
    ../../../src/sound/ymfm/ymfm_opn.cpp
    ../../../src/sound/ymfm/ymfm_ssg.cpp
    ../../../src/sound/ymfm/ymfm_adpcm.cpp)
target_include_directories(pc98_sound_tests PRIVATE ../../../src/include)
target_compile_features(pc98_sound_tests PRIVATE c_std_11 cxx_std_17)
target_compile_definitions(pc98_sound_tests PRIVATE RELEASE_BUILD)
if(MINGW)
    # Keep the standalone harness runnable without host MinGW DLL paths.
    target_link_options(pc98_sound_tests PRIVATE -static)
endif()
add_test(NAME pc98_sound COMMAND pc98_sound_tests)
