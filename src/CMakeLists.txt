#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake build script.
#
# Authors: David Hrdlička, <hrdlickadavid@outlook.com>
#          dob205
#
#          Copyright 2020-2022 David Hrdlička.
#          Copyright 2021 dob205.
#
if(APPLE)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
endif()

add_executable(86Box 86box.c config.c log.c random.c timer.c io.c acpi.c apm.c
    dma.c ddma.c nmi.c pic.c pit.c pit_fast.c port_6x.c port_92.c ppi.c pci.c
    mca.c usb.c fifo.c fifo8.c device.c nvr.c nvr_at.c nvr_ps2.c
    machine_status.c ini.c cJSON.c)

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    add_compile_definitions(_FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE=1 _LARGEFILE64_SOURCE=1)
endif()

if(PCL)
    target_compile_definitions(86Box PRIVATE USE_PCL)
endif()

if(CPPTHREADS)
    target_sources(86Box PRIVATE thread.cpp)
endif()

if(GDBSTUB)
    add_compile_definitions(USE_GDBSTUB)
    target_sources(86Box PRIVATE gdbstub.c)
endif()

if(NEW_DYNAREC)
    add_compile_definitions(USE_NEW_DYNAREC)
endif()

if(RELEASE)
    add_compile_definitions(RELEASE_BUILD)
endif()

if(DYNAREC)
    add_compile_definitions(USE_DYNAREC)
endif()

if(DEV_BRANCH)
    add_compile_definitions(DEV_BRANCH)
endif()

if(DISCORD)
    add_compile_definitions(DISCORD)
    target_sources(86Box PRIVATE discord.c)
endif()

if(DEBUGREGS486)
    add_compile_definitions(USE_DEBUG_REGS_486)
endif()

if(VNC)
    find_package(LibVNCServer)
    if(LibVNCServer_FOUND)
        add_compile_definitions(USE_VNC)
        add_library(vnc OBJECT vnc.c vnc_keymap.c)
        target_link_libraries(86Box vnc LibVNCServer::vncserver)
        if(WIN32)
            target_link_libraries(86Box ws2_32)
        endif()
    endif()
endif()

if(INSTRUMENT)
    add_compile_definitions(USE_INSTRUMENT)
endif()

target_link_libraries(86Box cpu chipset mch dev mem fdd game cdrom zip mo hdd
    net print scsi sio snd vid voodoo plat ui)

if(WIN32 AND ARCH STREQUAL "i386")
    if(MINGW)
        target_link_options(86Box PRIVATE "LINKER:--large-address-aware")
    else()
        target_link_options(86Box PRIVATE "LINKER:/LARGEADDRESSAWARE")
    endif()
endif()

if(STATIC_BUILD)
    if(MINGW OR UNIX)
        target_link_options(86Box PRIVATE "-static")
        set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    endif()
endif()

if(APPLE)
    # Force using the newest library if it's installed by homebrew
    set(CMAKE_FIND_FRAMEWORK LAST)

    # setting our compilation target to macOS 10.15 Catalina if targeting Qt6,
    # macOS 10.14 Mojave for vulkan support, 10.13 High Sierra otherwise
    if (USE_QT6)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")
    else()
        if(MOLTENVK)
            set(CMAKE_OSX_DEPLOYMENT_TARGET "10.14")
        else()
            set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13")
        endif()
    endif()
endif()

find_package(Freetype REQUIRED)
include_directories(${FREETYPE_INCLUDE_DIRS})
if(FREETYPE_INCLUDE_DIR_ft2build)
    include_directories(${FREETYPE_INCLUDE_DIR_ft2build})
endif()
if(APPLE)
    # Freetype is dynamically loaded by the emulator, however, we link it
    # on macOS so it gets copied to the bundle by the installation process
    target_link_libraries(86Box Freetype::Freetype)
endif()

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})
if(STATIC_BUILD AND TARGET SDL2::SDL2-static)
    target_link_libraries(86Box SDL2::SDL2-static)
elseif(TARGET SDL2::SDL2)
    target_link_libraries(86Box SDL2::SDL2)
else()
    target_link_libraries(86Box ${SDL2_LIBRARIES})
endif()

find_package(PNG REQUIRED)
include_directories(${PNG_INCLUDE_DIRS})
target_link_libraries(86Box PNG::PNG)

configure_file(include/86box/version.h.in include/86box/version.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)

include_directories(include)
if(NEW_DYNAREC)
    include_directories(cpu codegen_new)
else()
    include_directories(cpu codegen)
endif()

add_subdirectory(cdrom)
add_subdirectory(chipset)

add_subdirectory(cpu)
if(NEW_DYNAREC)
    add_subdirectory(codegen_new)
else()
    add_subdirectory(codegen)
endif()

if(MINITRACE)
    add_compile_definitions(MTR_ENABLED)
    add_library(minitrace OBJECT minitrace/minitrace.c)
    target_link_libraries(86Box minitrace)
endif()

if(WIN32 OR (APPLE AND CMAKE_MACOSX_BUNDLE))
    # Copy the binary to the root of the install prefix on Windows and macOS
    install(TARGETS 86Box DESTINATION ".")
else()
    # On Linux we want to copy the binary to the `bin` folder.
    install(TARGETS 86Box)
endif()


# Install our dependencies if using vcpkg
if(VCPKG_TOOLCHAIN)
    x_vcpkg_install_local_dependencies(TARGETS 86Box DESTINATION ".")
endif()


# Install other dependencies
if(WIN32)
    install(CODE "
        include(BundleUtilities)
        get_filename_component(CMAKE_INSTALL_PREFIX_ABSOLUTE \${CMAKE_INSTALL_PREFIX} ABSOLUTE)
        fixup_bundle(\"\${CMAKE_INSTALL_PREFIX_ABSOLUTE}/$<TARGET_FILE_NAME:86Box>\" \"\" \"\")"
    COMPONENT Runtime)
elseif(APPLE AND NOT QT)
    install(CODE "
        include(BundleUtilities)
        get_filename_component(CMAKE_INSTALL_PREFIX_ABSOLUTE \${CMAKE_INSTALL_PREFIX} ABSOLUTE)
        fixup_bundle(\"\${CMAKE_INSTALL_PREFIX_ABSOLUTE}/86Box.app\" \"\" \"\")"
    COMPONENT Runtime)
endif()


# Install the PDB file on Windows builds
if(MSVC)
    # CMake fully supports PDB files on MSVC-compatible compilers
    install(FILES $<TARGET_PDB_FILE:86Box>
        CONFIGURATIONS Debug RelWithDebInfo
        DESTINATION ".")
elseif(WIN32)
    # Other compilers/linkers (such as Clang in GCC-compatible mode) also
    # emit PDB files when targeting Windows, however, CMake only supports
    # the relevant properties with MSVC and clones. Try to install
    # the PDB file assuming it's in the same path as the EXE.
    install(FILES "$<TARGET_FILE_DIR:86Box>/$<TARGET_FILE_BASE_NAME:86Box>.pdb"
        CONFIGURATIONS Debug RelWithDebInfo
        DESTINATION "."
        OPTIONAL)
endif()


add_subdirectory(device)
add_subdirectory(disk)
add_subdirectory(floppy)
add_subdirectory(game)
add_subdirectory(machine)
add_subdirectory(mem)
add_subdirectory(network)
add_subdirectory(printer)
add_subdirectory(sio)
add_subdirectory(scsi)
add_subdirectory(sound)
add_subdirectory(video)
if (APPLE)
    add_subdirectory(mac)
endif()

if (QT)
    add_subdirectory(qt)
else()
    add_compile_definitions(USE_SDL_UI)
    add_subdirectory(unix)
endif()
