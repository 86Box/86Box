# The ATI Rage 128 tests compile the device sources themselves, with host
# stubs for the 86Box services the device calls, so none of them links
# against the emulator. Every target here builds with -ffp-contract=off:
# the interpreter, the JIT and the GPU kernels round each float operation
# on its own, and a reference that fuses a multiply with an add disagrees
# with all three. The JIT harness runs the emitted code through GNU inline
# assembly, so the set needs a GCC or Clang front end.
if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    message(STATUS "Rage 128 tests: need GCC or Clang, skipped")
    return()
endif()

find_package(Threads REQUIRED)

# Command-line tools: plain executables, not the app bundles the emulator
# builds on macOS, and console programs on Windows, where the emulator's
# top-level CMakeLists.txt turns CMAKE_WIN32_EXECUTABLE on for its own GUI
# executable. A GUI-subsystem tool has no console, so every child it
# starts (the replay harness runs one zstd decoder per capture reader)
# opens a console window of its own, and its output goes nowhere when run
# by hand.
set(CMAKE_MACOSX_BUNDLE OFF)
set(CMAKE_WIN32_EXECUTABLE OFF)

# The GPU raster backend opens the Vulkan loader at run time, so the
# register and replay harnesses take the headers alone, the same probe the
# device build in src/video uses. gputri and fdivtest link the loader.
find_package(Vulkan QUIET)
find_path(R128_VULKAN_INCLUDE_DIR vulkan/vulkan.h HINTS ${Vulkan_INCLUDE_DIR})
find_program(R128_GLSLC glslc HINTS ${Vulkan_GLSLC_EXECUTABLE} $ENV{VULKAN_SDK}/bin)

set(R128_SRC ${PROJECT_SOURCE_DIR}/src)
set(R128_DEVICE_SOURCES
    ${R128_SRC}/video/vid_ati_rage128_vga.c
    ${R128_SRC}/video/vid_ati_rage128_display.c
    ${R128_SRC}/video/vid_ati_rage128_ov0.c
    ${R128_SRC}/video/vid_ati_rage128_2d.c
    ${R128_SRC}/video/vid_ati_rage128_pm4.c
    ${R128_SRC}/video/vid_ati_rage128_3d.c
    ${R128_SRC}/video/vid_ati_rage128_mpeg.c
    ${R128_SRC}/video/vid_ati_rage128_raster.c
    ${R128_SRC}/video/vid_ati_rage128_mem.c
    ${R128_SRC}/video/vid_ati_rage128_jit.c
    ${R128_SRC}/video/vid_ati_rage128_synctel.c
    ${R128_SRC}/video/vid_ati_rage128_census.c
    ${R128_SRC}/device/i2c.c
    ${R128_SRC}/device/i2c_gpio.c
    ${R128_SRC}/mem/i2c_eeprom.c
    ${R128_SRC}/video/vid_ddc.c
    ${R128_SRC}/unix/unix_thread.c
)

# Settings the register and replay harnesses share: host stubs in place of
# 86Box, the harness's own version header ahead of the generated one, and
# the device log compiled in.
function(r128_device_harness target)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/reg-harness/include
        ${R128_SRC}/include
        ${R128_SRC}/video
        ${R128_SRC}/cpu
        ${R128_SRC}
    )
    target_compile_definitions(${target} PRIVATE REG_HARNESS ENABLE_RAGE128_LOG=1)
    target_compile_options(${target} PRIVATE -g -ffp-contract=off -fno-common -Wno-unused-function)
    if(R128_VULKAN_INCLUDE_DIR)
        target_compile_definitions(${target} PRIVATE R128_GPU_HAVE_VULKAN)
        target_include_directories(${target} PRIVATE ${R128_VULKAN_INCLUDE_DIR})
    endif()
    target_link_libraries(${target} PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    if(NOT WIN32 AND NOT APPLE)
        target_link_libraries(${target} PRIVATE m)
    endif()
endfunction()

# Register harness: one process per lane. Its timing rows measure wall
# clock, so the three lanes run alone (RUN_SERIAL), each in its own
# directory for the probe files it writes.
add_executable(rage128_reg_harness
    reg-harness/reg_harness.c
    reg-harness/host_stubs.c
    reg-harness/gpu_probe.c
    ${R128_DEVICE_SOURCES}
)
set_target_properties(rage128_reg_harness PROPERTIES OUTPUT_NAME reg_harness)
r128_device_harness(rage128_reg_harness)
target_compile_options(rage128_reg_harness PRIVATE -O1)

foreach(lane IN ITEMS Stock JitVerify GpuVerify)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/reg-${lane})
    add_test(NAME Rage128.Registers.${lane} COMMAND rage128_reg_harness
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/reg-${lane})
    set_tests_properties(Rage128.Registers.${lane} PROPERTIES RUN_SERIAL TRUE)
endforeach()
set_tests_properties(Rage128.Registers.JitVerify PROPERTIES ENVIRONMENT R128_JIT=verify)
set_tests_properties(Rage128.Registers.GpuVerify PROPERTIES ENVIRONMENT R128_GPU=verify)

# Replay harness: a tool for captures a developer records, not a test.
add_executable(rage128_replay
    replay-harness/replay.c
    reg-harness/host_stubs.c
    ${R128_DEVICE_SOURCES}
    ${R128_SRC}/video/vid_ati_rage128_gpu.c
)
set_target_properties(rage128_replay PROPERTIES OUTPUT_NAME replay)
r128_device_harness(rage128_replay)
target_compile_options(rage128_replay PRIVATE -O2)

# The synthetic captures in replay-harness/synth are the way to try the
# replay harness without a capture of one's own; this test keeps them
# replaying to the expected image. The generators need Python 3.
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME Rage128.ReplaySynth
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/replay-harness/synth/selftest.py
                     $<TARGET_FILE:rage128_replay> ${CMAKE_CURRENT_BINARY_DIR}/replay-synth)
else()
    message(STATUS "Rage 128 tests: Python 3 not found, Rage128.ReplaySynth skipped")
endif()

# JIT host harness: the span emitters' output against the C reference.
# The full fuzz gate runs about 20 minutes on one core of an arm64 Mac and
# about 35 on an x86-64 workstation.
add_executable(rage128_jit_host_test jit-harness/jit_host_test.c)
set_target_properties(rage128_jit_host_test PROPERTIES OUTPUT_NAME jit_host_test)
target_include_directories(rage128_jit_host_test PRIVATE ${R128_SRC}/include)
target_compile_definitions(rage128_jit_host_test PRIVATE ENABLE_RAGE128_LOG=1)
target_compile_options(rage128_jit_host_test PRIVATE -O2 -ffp-contract=off)
target_link_libraries(rage128_jit_host_test PRIVATE Threads::Threads)
if(NOT WIN32 AND NOT APPLE)
    target_link_libraries(rage128_jit_host_test PRIVATE m)
endif()
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/jit)
# jit_host_test runs its 3.3 million rows in one process against the
# interpreter reference, so the row is long on every host: about 1255 s
# on an arm64 Mac and up to 2160 s on a 14-core Windows workstation with
# other tests running beside it. The timeout leaves a slower or busier
# machine room to finish rather than report a kill with no result.
add_test(NAME Rage128.JitHost COMMAND rage128_jit_host_test
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/jit)
set_tests_properties(Rage128.JitHost PROPERTIES TIMEOUT 7200)

# z-ladder closed-form check: plain C, no GPU.
add_executable(rage128_ladfuzz gpu/ladfuzz.c)
set_target_properties(rage128_ladfuzz PROPERTIES OUTPUT_NAME ladfuzz)
target_compile_options(rage128_ladfuzz PRIVATE -O2 -ffp-contract=off)
if(NOT WIN32 AND NOT APPLE)
    target_link_libraries(rage128_ladfuzz PRIVATE m)
endif()

# gputri drives the production SPIR-V kernels on a real Vulkan device
# against a C copy of the interpreter; fdivtest checks the kernels'
# correctly rounded divide on its own. Both need the loader and glslc.
if(NOT Vulkan_FOUND OR NOT R128_GLSLC)
    message(STATUS "Rage 128 tests: Vulkan loader or glslc not found, gputri and fdivtest skipped")
    return()
endif()

foreach(kernel IN ITEMS f64test fdivtest)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${kernel}.spv
        COMMAND ${R128_GLSLC} -O --target-env=vulkan1.2 -I ${R128_SRC}/video
                -o ${CMAKE_CURRENT_BINARY_DIR}/${kernel}.spv
                ${CMAKE_CURRENT_SOURCE_DIR}/gpu/${kernel}.comp
        DEPENDS gpu/${kernel}.comp ${R128_SRC}/video/vid_ati_rage128_gpu_f64.glsl
                ${R128_SRC}/video/vid_ati_rage128_gpu_fdiv.glsl
        VERBATIM
    )
endforeach()
add_custom_target(rage128_gpu_spv DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/f64test.spv
    ${CMAKE_CURRENT_BINARY_DIR}/fdivtest.spv
)

add_executable(rage128_gputri gpu/gputri.c)
set_target_properties(rage128_gputri PROPERTIES OUTPUT_NAME gputri)
target_include_directories(rage128_gputri PRIVATE ${R128_SRC}/include)
target_compile_options(rage128_gputri PRIVATE -O2 -ffp-contract=off)
target_link_libraries(rage128_gputri PRIVATE Vulkan::Vulkan Threads::Threads)
add_dependencies(rage128_gputri rage128_gpu_spv)

add_executable(rage128_fdivtest gpu/fdivtest.c)
set_target_properties(rage128_fdivtest PROPERTIES OUTPUT_NAME fdivtest)
target_compile_options(rage128_fdivtest PRIVATE -O2 -ffp-contract=off)
target_link_libraries(rage128_fdivtest PRIVATE Vulkan::Vulkan)
add_dependencies(rage128_fdivtest rage128_gpu_spv)

# gputri reads f64test.spv from its working directory and exits 77 when
# the host has no Vulkan device, which CTest reports as skipped.
#
# The timeout is sized for the first run on a host whose Vulkan driver
# compiles pipelines one at a time. gputri primes 288 fold pipelines per
# seed, and each of its three seeds uses its own set, so a cold run pays
# three full compiles before the pipeline cache it writes at exit makes
# the next run short. Measured on a Windows AMD host: 4831 s cold against
# 911 s warm, so 3600 s cut the first run and left no cache behind. 7200 s
# is 1.5 times that cold run; a warm run on any host finishes well inside
# the first quarter of it.
add_test(NAME Rage128.GpuKernels COMMAND rage128_gputri
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(Rage128.GpuKernels PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 7200)
