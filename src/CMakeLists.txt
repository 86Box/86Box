#
# 86Box     A hypervisor and IBM PC system emulator that specializes in
#           running old operating systems and software designed for IBM
#           PC systems and compatibles from 1981 through fairly recent
#           system designs based on the PCI bus.
#
#           This file is part of the 86Box distribution.
#
#           CMake build script.
#
# Authors:  David Hrdlička, <hrdlickadavid@outlook.com>
#           dob205
#
#           Copyright 2020,2021 David Hrdlička.
#           Copyright 2021 dob205.
#

# WIN32 marks us as a GUI app on Windows
add_executable(86Box WIN32 MACOSX_BUNDLE 86box.c config.c log.c random.c timer.c io.c acpi.c apm.c
    dma.c ddma.c discord.c nmi.c pic.c pit.c port_6x.c port_92.c ppi.c pci.c mca.c usb.c fifo8.c
    device.c nvr.c nvr_at.c nvr_ps2.c)

if(CPPTHREADS)
    target_sources(86Box PRIVATE thread.cpp)
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

if(VRAMDUMP)
    add_compile_definitions(ENABLE_VRAM_DUMP)
endif()

if(DEV_BRANCH)
    add_compile_definitions(DEV_BRANCH)
endif()

if(VNC)
    add_compile_definitions(USE_VNC)
    add_library(vnc OBJECT vnc.c vnc_keymap.c)
    target_link_libraries(86Box vnc vncserver)
    if (WIN32)
        target_link_libraries(86Box ws2_32)
    endif()
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

    # setting our compilation target to macOS 10.15 Catalina if targetting Qt6, macOS 10.13 High Sierra otherwise
    if (USE_QT6)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")
    else()
        set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13")
    endif()
endif()

if(QT)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  if (USE_QT6)
      set(QT_MAJOR 6)
  else()
      set(QT_MAJOR 5)
  endif()
  
  # if we want to use qt5-static to eliminate need for bundling qt dlls (windows)
  #set(QT_STATIC ON)
  # needed to build with qt5-static if both qt5 and qt5-static are installed
  #set(CMAKE_PREFIX_PATH "path/to/qt5-static")
  
  find_package(Qt${QT_MAJOR} COMPONENTS Core Widgets OpenGL REQUIRED)
  find_package(Qt${QT_MAJOR}LinguistTools REQUIRED)
endif()

find_package(Freetype REQUIRED)
include_directories(${FREETYPE_INCLUDE_DIRS})
if(APPLE)
    # Freetype is dynamically loaded by the emulator, however, we link it
    # on macOS so it gets copied to the bundle by the installation process
    target_link_libraries(86Box Freetype::Freetype)
endif()

if(OPENAL)
    find_package(OpenAL REQUIRED)
    include_directories(${OPENAL_INCLUDE_DIR})
    target_link_libraries(86Box ${OPENAL_LIBRARY})
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

if(VCPKG_TOOLCHAIN)
    # vcpkg includes a config file for rtmidi
    find_package(RtMidi REQUIRED)
    target_link_libraries(86Box RtMidi::rtmidi)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(RTMIDI REQUIRED IMPORTED_TARGET rtmidi)
    target_link_libraries(86Box PkgConfig::RTMIDI)

    if(WIN32)
        target_link_libraries(PkgConfig::RTMIDI INTERFACE winmm)
    endif()
endif()

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

# prepare everything for Qt5 bundle creation
if (APPLE AND QT)
    set(prefix "86Box.app/Contents")
    set(INSTALL_RUNTIME_DIR "${prefix}/MacOS")
    set(INSTALL_CMAKE_DIR "${prefix}/Resources")
endif()

# loads a macro to install Qt5 plugins on macOS
# based on https://stackoverflow.com/questions/35612687/cmake-macos-x-bundle-with-bundleutiliies-for-qt-application
macro(install_qt5_plugin _qt_plugin_name _qt_plugins_var _prefix)
    get_target_property(_qt_plugin_path "${_qt_plugin_name}" LOCATION)
    if(EXISTS "${_qt_plugin_path}")
        get_filename_component(_qt_plugin_file "${_qt_plugin_path}" NAME)
        get_filename_component(_qt_plugin_type "${_qt_plugin_path}" PATH)
        get_filename_component(_qt_plugin_type "${_qt_plugin_type}" NAME)
        set(_qt_plugin_dest "${_prefix}/PlugIns/${_qt_plugin_type}")
        install(FILES "${_qt_plugin_path}"
            DESTINATION "${_qt_plugin_dest}")
        set(${_qt_plugins_var}
            "${${_qt_plugins_var}};\$ENV{DEST_DIR}\${CMAKE_INSTALL_PREFIX}/${_qt_plugin_dest}/${_qt_plugin_file}")
    else()
        message(FATAL_ERROR "QT plugin ${_qt_plugin_name} not found")
    endif()
endmacro()

# Install our dependencies to the macOS bundle
if(APPLE)
    if (NOT QT)
        install(CODE "
            include(BundleUtilities)
            get_filename_component(CMAKE_INSTALL_PREFIX_ABSOLUTE \${CMAKE_INSTALL_PREFIX} ABSOLUTE)
            fixup_bundle(\"\${CMAKE_INSTALL_PREFIX_ABSOLUTE}/86Box.app\" \"\" \"\")"
        COMPONENT Runtime)
    endif()
    
    if(QT)
    # using the install_qt5_plugin to add Qt plugins into the macOS app bundle
    if (USE_QT6)
        install_qt5_plugin("Qt6::QCocoaIntegrationPlugin" QT_PLUGINS ${prefix})
    else()
        install_qt5_plugin("Qt5::QCocoaIntegrationPlugin" QT_PLUGINS ${prefix})
        install_qt5_plugin("Qt5::QMacStylePlugin" QT_PLUGINS ${prefix})
        install_qt5_plugin("Qt5::QICOPlugin" QT_PLUGINS ${prefix})
        install_qt5_plugin("Qt5::QICNSPlugin" QT_PLUGINS ${prefix})
    endif()
    
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/qt.conf"
        "[Paths]\nPlugins = ${_qt_plugin_dir}\n")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/qt.conf"
        DESTINATION "${INSTALL_CMAKE_DIR}")

    # Note Mac specific extension .app
    set(APPS "\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/86Box.app")

    # Directories to look for dependencies
    set(DIRS "${CMAKE_BINARY_DIR}")

    # Path used for searching by FIND_XXX(), with appropriate suffixes added
    if(CMAKE_PREFIX_PATH)
        foreach(dir ${CMAKE_PREFIX_PATH})
            list(APPEND DIRS "${dir}/bin" "${dir}/lib")
    endforeach()
    endif()

    # Append Qt's lib folder which is two levels above Qt5Widgets_DIR
    list(APPEND DIRS "${Qt5Widgets_DIR}/../..")

    include(InstallRequiredSystemLibraries)

    message(STATUS "APPS: ${APPS}")
    message(STATUS "QT_PLUGINS: ${QT_PLUGINS}")
    message(STATUS "DIRS: ${DIRS}")

    install(CODE "include(BundleUtilities)
        fixup_bundle(\"${APPS}\" \"${QT_PLUGINS}\" \"${DIRS}\")")
    endif()    
endif()


# Install our dependencies if using vcpkg
if(VCPKG_TOOLCHAIN)
    x_vcpkg_install_local_dependencies(TARGETS 86Box DESTINATION ".")
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
elseif(WIN32)
    add_subdirectory(win)
else()
    add_subdirectory(unix)
endif()
