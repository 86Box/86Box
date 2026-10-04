cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(pc98_bios_patch_tests C)
    enable_testing()
endif()
add_executable(pc98_bios_patch_tests
    ../pc98_bios_patch_test.c ../../../src/machine/pc98_bios_patch.c)
target_include_directories(pc98_bios_patch_tests PRIVATE ../../../src/include)
target_compile_features(pc98_bios_patch_tests PRIVATE c_std_11)
add_test(NAME pc98_bios_patch COMMAND pc98_bios_patch_tests)
set(vx_rom_dir "${CMAKE_CURRENT_LIST_DIR}/../../../roms/machines/pc9801")
if(EXISTS "${vx_rom_dir}/nec_d27c256d-15_cpu_extboard_yll02.bin" AND
   EXISTS "${vx_rom_dir}/nec_d27c256d-15_cpu_extboard_yll04.bin")
    add_test(NAME pc98_bios_patch_supplied_rom COMMAND pc98_bios_patch_tests
        "${vx_rom_dir}")
    set_tests_properties(pc98_bios_patch_supplied_rom PROPERTIES SKIP_RETURN_CODE 77)
endif()
