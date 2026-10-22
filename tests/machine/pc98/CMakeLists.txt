cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(pc98_machine_tests C)
    enable_testing()
endif()

# Use the actual machine registrations, avoiding dependencies on every other
# motherboard in the emulator. A changed or missing profile fails configuration.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../../../src/machine/machine_table.c" table)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../src/machine/machine_table.c")
string(FIND "${table}" "/* NEC PC-9801VX:" profile_start)
string(FIND "${table}" "/* End of native PC-98 machine registrations. */" profile_end)
if(profile_start LESS 0 OR profile_end LESS profile_start)
    message(FATAL_ERROR "Cannot find native PC-98 machine registrations")
endif()
math(EXPR profile_size "${profile_end} - ${profile_start}")
string(SUBSTRING "${table}" ${profile_start} ${profile_size} profiles)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/pc98_profiles.inc"
    "const machine_t machines[] = {\n${profiles}\n{ .cpu = { .package = CPU_PKG_286, .min_bus = 8000000, .max_bus = 10000000 } }\n};\n")

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../../../src/machine/m_pc98.c" motherboard)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../src/machine/m_pc98.c")
string(FIND "${motherboard}" "static const device_config_t pc9801vx_config[]" config_start)
string(FIND "${motherboard}" "static const device_t pc9801vx_motherboard_device" config_end)
if(config_start LESS 0 OR config_end LESS config_start)
    message(FATAL_ERROR "Cannot find PC-9801VX configuration descriptor")
endif()
math(EXPR config_size "${config_end} - ${config_start}")
string(SUBSTRING "${motherboard}" ${config_start} ${config_size} config_descriptor)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/pc98_config.inc" "${config_descriptor}")

add_executable(pc98_machine_tests
    ../pc98_machine_test.c
    ../../../src/cpu/cpu.c
    ../../../src/cpu/cpu_table.c
    ../../../src/utils/ini.c
    ../../../src/machine/pc98_rom.c)
target_include_directories(pc98_machine_tests PRIVATE
    ../../../src/include ../../../src/cpu "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(pc98_machine_tests PRIVATE c_std_11)
target_compile_definitions(pc98_machine_tests PRIVATE
    "$<$<CONFIG:Release>:RELEASE_BUILD>"
    "$<$<BOOL:${DEV_BRANCH}>:PC98_EXPERIMENTAL>")
set_property(TARGET pc98_machine_tests PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
target_compile_options(pc98_machine_tests PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
    # Windows LTO must optimize away CPU execution entry points for this host
    # adapter. Keep Debug's logging policy and symbols while doing that.
    $<$<AND:$<CONFIG:Debug>,$<C_COMPILER_ID:GNU,Clang,AppleClang>>:-O1>)
target_link_options(pc98_machine_tests PRIVATE
    $<$<AND:$<CONFIG:Debug>,$<C_COMPILER_ID:GNU,Clang,AppleClang>>:-O1>
    $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
    $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<C_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>)
add_test(NAME pc98_machine COMMAND pc98_machine_tests)

add_executable(pc98_video_mapping_tests ../pc98_video_mapping_test.c
    ../../../src/machine/pc98_rom.c
    ../../../src/machine/pc98_bios_patch.c
    ../../../src/video/vid_pc98.c ../../../src/video/vid_upd7220.c)
target_include_directories(pc98_video_mapping_tests PRIVATE ../../../src/include)
target_include_directories(pc98_video_mapping_tests PRIVATE ../../../src/cpu)
set_property(TARGET pc98_video_mapping_tests PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
target_compile_options(pc98_video_mapping_tests PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
    $<$<AND:$<CONFIG:Debug>,$<C_COMPILER_ID:GNU,Clang,AppleClang>>:-O1>)
target_link_options(pc98_video_mapping_tests PRIVATE
    $<$<AND:$<CONFIG:Debug>,$<C_COMPILER_ID:GNU,Clang,AppleClang>>:-O1>
    $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
    $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<C_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>)
target_compile_features(pc98_video_mapping_tests PRIVATE c_std_11)
target_compile_definitions(pc98_video_mapping_tests PRIVATE "$<$<CONFIG:Release>:RELEASE_BUILD>")
add_test(NAME pc98_video_mapping COMMAND pc98_video_mapping_tests)
