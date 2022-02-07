# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Threads REQUIRED)

add_library(plat STATIC
    qt.c
    qt_main.cpp
    qt_platform.cpp
)

add_library(ui STATIC
    qt_ui.cpp
    qt_cdrom.c

    qt_mainwindow.cpp
    qt_mainwindow.hpp
    qt_mainwindow.ui
    qt_machinestatus.cpp
    qt_machinestatus.hpp
    qt_mediamenu.cpp
    qt_mediamenu.hpp
    qt_rendererstack.cpp
    qt_rendererstack.hpp
    qt_rendererstack.ui
    qt_renderercommon.cpp
    qt_renderercommon.hpp
    qt_softwarerenderer.cpp
    qt_softwarerenderer.hpp
    qt_hardwarerenderer.cpp
    qt_hardwarerenderer.hpp

    qt_settings.cpp
    qt_settings.hpp
    qt_settings.ui

    qt_settingsmachine.cpp
    qt_settingsmachine.hpp
    qt_settingsmachine.ui
    qt_settingsdisplay.cpp
    qt_settingsdisplay.hpp
    qt_settingsdisplay.ui
    qt_settingsinput.cpp
    qt_settingsinput.hpp
    qt_settingsinput.ui
    qt_settingssound.cpp
    qt_settingssound.hpp
    qt_settingssound.ui
    qt_settingsnetwork.cpp
    qt_settingsnetwork.hpp
    qt_settingsnetwork.ui
    qt_settingsports.cpp
    qt_settingsports.hpp
    qt_settingsports.ui
    qt_settingsstoragecontrollers.cpp
    qt_settingsstoragecontrollers.hpp
    qt_settingsstoragecontrollers.ui
    qt_settingsharddisks.cpp
    qt_settingsharddisks.hpp
    qt_settingsharddisks.ui
    qt_settingsfloppycdrom.cpp
    qt_settingsfloppycdrom.hpp
    qt_settingsfloppycdrom.ui
    qt_settingsotherremovable.cpp
    qt_settingsotherremovable.hpp
    qt_settingsotherremovable.ui
    qt_settingsotherperipherals.cpp
    qt_settingsotherperipherals.hpp
    qt_settingsotherperipherals.ui
    qt_settings_bus_tracking.cpp
    qt_settings_bus_tracking.hpp

    qt_deviceconfig.cpp
    qt_deviceconfig.hpp
    qt_deviceconfig.ui
    qt_joystickconfiguration.cpp
    qt_joystickconfiguration.hpp
    qt_joystickconfiguration.ui

    qt_filefield.cpp
    qt_filefield.hpp
    qt_filefield.ui
    qt_newfloppydialog.cpp
    qt_newfloppydialog.hpp
    qt_newfloppydialog.ui
    qt_harddiskdialog.cpp
    qt_harddiskdialog.hpp
    qt_harddiskdialog.ui

    qt_harddrive_common.cpp
    qt_harddrive_common.hpp
    qt_models_common.cpp
    qt_models_common.hpp

    qt_specifydimensions.h
    qt_specifydimensions.cpp
    qt_specifydimensions.ui
    qt_soundgain.hpp
    qt_soundgain.cpp
    qt_soundgain.ui

    qt_styleoverride.cpp
    qt_styleoverride.hpp
    qt_progsettings.hpp
    qt_progsettings.cpp
    qt_progsettings.ui
    qt_util.hpp
    qt_util.cpp

    ../qt_resources.qrc
)


if(WIN32)
    enable_language(RC)
    target_sources(86Box PUBLIC ../win/86Box-qt.rc)
    target_sources(plat PRIVATE win_joystick_rawinput.c)
    target_link_libraries(86Box hid)

    # CMake 3.22 messed this up for clang/clang++
    # See https://gitlab.kitware.com/cmake/cmake/-/issues/22611
    if(MSVC OR (NOT MINGW AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.22))
        # MSVC linker adds its own manifest to the executable, which fails if
        # we include ours in 86Box.rc. We therefore need to pass the manifest
        # directly as as a source file, so the linker can use that instead.
        set_property(SOURCE ../win/86Box-qt.rc PROPERTY COMPILE_DEFINITIONS NO_INCLUDE_MANIFEST)
        target_sources(86Box PRIVATE ../win/86Box.manifest)
    endif()
else()
    target_sources(plat PRIVATE sdl_joystick.cpp)
endif()

if(WIN32 AND NOT MINGW)
    target_sources(plat PRIVATE ../win/win_opendir.c)
endif()

if (APPLE)
    target_sources(ui PRIVATE macos_event_filter.mm)
endif()

if (WIN32)
    target_sources(ui PRIVATE
        qt_winrawinputfilter.hpp
        qt_winrawinputfilter.cpp
        qt_winmanagerfilter.hpp
        qt_winmanagerfilter.cpp
    )
endif()

target_link_libraries(
    plat
    PRIVATE
    Qt${QT_MAJOR}::Widgets
    Qt${QT_MAJOR}::Gui
    Threads::Threads
)

target_link_libraries(
    ui
    PRIVATE
    Qt${QT_MAJOR}::Widgets
    Qt${QT_MAJOR}::Gui
    Qt${QT_MAJOR}::OpenGL
    Threads::Threads
)

# needed for static builds
if (WIN32)
    qt_import_plugins(plat INCLUDE Qt${QT_MAJOR}::QWindowsIntegrationPlugin Qt${QT_MAJOR}::QICOPlugin QWindowsVistaStylePlugin)
endif()

if (UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    target_link_libraries(ui PRIVATE X11::X11)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBEVDEV IMPORTED_TARGET libevdev)
    if (LIBEVDEV_FOUND)
        target_compile_definitions(ui PRIVATE EVDEV_INPUT)
        target_link_libraries(ui PUBLIC PkgConfig::LIBEVDEV)
        target_sources(ui PRIVATE evdev_mouse.cpp)
    endif()

    find_package(ECM NO_MODULE)
    if (ECM_FOUND)
        list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
        find_package(Wayland COMPONENTS Client)
        if (Wayland_FOUND)
            target_link_libraries(ui PRIVATE Wayland::Client)
            find_package(WaylandScanner REQUIRED)
            if (WaylandScanner_FOUND)
                set(WL_SOURCE_VAR)
                ecm_add_wayland_client_protocol(WL_SOURCE_VAR PROTOCOL ${CMAKE_SOURCE_DIR}/wl_protocols/relative-pointer-unstable-v1.xml BASENAME relative-pointer-unstable-v1)
                ecm_add_wayland_client_protocol(WL_SOURCE_VAR PROTOCOL ${CMAKE_SOURCE_DIR}/wl_protocols/pointer-constraints-unstable-v1.xml BASENAME pointer-constraints-unstable-v1)
                target_include_directories(ui PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${Qt${QT_MAJOR}Gui_PRIVATE_INCLUDE_DIRS})
                target_sources(ui PRIVATE ${WL_SOURCE_VAR} wl_mouse.cpp)
                target_compile_definitions(ui PRIVATE WAYLAND)
            endif()
        endif()
    endif()
endif()
set(QM_FILES)
file(GLOB po_files "${CMAKE_CURRENT_SOURCE_DIR}/languages/*.po")
foreach(po_file ${po_files})
    get_target_property(LCONVERT_EXECUTABLE Qt${QT_MAJOR}::lconvert IMPORTED_LOCATION)
    get_filename_component(_lconvert_bin_dir "${LCONVERT_EXECUTABLE}" DIRECTORY)
    find_program(LCONVERT_EXECUTABLE lconvert HINTS "${_lconvert_bin_dir}")

    get_filename_component(PO_FILE_NAME ${po_file} NAME_WE)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/86box_${PO_FILE_NAME}.qm"
                       COMMAND ${LCONVERT_EXECUTABLE} -i ${po_file} -o ${CMAKE_CURRENT_BINARY_DIR}/86box_${PO_FILE_NAME}.qm
                       WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                       DEPENDS "${po_file}")
    list(APPEND QM_FILES "${CMAKE_CURRENT_BINARY_DIR}/86box_${PO_FILE_NAME}.qm")
    list(APPEND QM_FILES "${po_file}")
endforeach()
configure_file(qt_translations.qrc ${CMAKE_CURRENT_BINARY_DIR} COPYONLY)
target_sources(ui PRIVATE ${QM_FILES} ${CMAKE_CURRENT_BINARY_DIR}/qt_translations.qrc)
