#
# 86Box		A hypervisor and IBM PC system emulator that specializes in
#		running old operating systems and software designed for IBM
#		PC systems and compatibles from 1981 through fairly recent
#		system designs based on the PCI bus.
#
#		This file is part of the 86Box distribution.
#
#		CMake build script.
#
# Authors:	David Hrdlička, <hrdlickadavid@outlook.com>
#
#		Copyright 2020,2021 David Hrdlička.
#

# WIN32 marks us as a GUI app on Windows
add_executable(86Box WIN32 86box.c config.c random.c timer.c io.c acpi.c apm.c
	dma.c ddma.c nmi.c pic.c pit.c port_92.c ppi.c pci.c mca.c usb.c
	device.c nvr.c nvr_at.c nvr_ps2.c)

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
	target_link_libraries(86Box vnc vncserver ws2_32)
endif()

target_link_libraries(86Box cpu chipset mch dev mem fdd game cdrom zip mo hdd
	net print scsi sio snd vid voodoo plat ui)

find_package(Freetype REQUIRED)
include_directories(${FREETYPE_INCLUDE_DIRS})

find_package(OpenAL CONFIG REQUIRED)
include_directories(${OPENAL_INCLUDE_DIRS})
target_link_libraries(86Box OpenAL::OpenAL)

find_package(SDL2 CONFIG REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})
target_link_libraries(86Box SDL2::SDL2)

find_package(PNG REQUIRED)
include_directories(${PNG_INCLUDE_DIRS})
target_link_libraries(86Box PNG::PNG)

if(CMAKE_TARGET_ARCHITECTURES STREQUAL "i386")
	if(MSVC)
		set_target_properties(86Box PROPERTIES LINK_FLAGS "/LARGEADDRESSAWARE")
	elseif(MINGW)
		set_target_properties(86Box PROPERTIES LINK_FLAGS "-Wl,--large-address-aware")
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

install(TARGETS 86Box)
if(VCPKG_TOOLCHAIN)
	x_vcpkg_install_local_dependencies(TARGETS 86Box DESTINATION "bin")
endif()

if(MSVC)
	install(FILES $<TARGET_PDB_FILE:86Box>
		CONFIGURATIONS Debug RelWithDebInfo
		DESTINATION "bin")
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
add_subdirectory(win)
