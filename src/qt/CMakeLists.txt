# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(PkgConfig)
find_package(Threads REQUIRED)

add_library(plat STATIC qt.c qt_main.cpp qt_platform.cpp cpp11_thread.cpp)
add_library(ui STATIC
    qt_ui.cpp
    qt_cdrom.c
    qt_sdl.c

    qt_mainwindow.cpp
    qt_mainwindow.hpp
    qt_mainwindow.ui
    qt_machinestatus.cpp
    qt_machinestatus.hpp
    qt_mediamenu.cpp
    qt_mediamenu.hpp
    qt_gleswidget.cpp
    qt_gleswidget.hpp

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

    qt_deviceconfig.cpp
    qt_deviceconfig.hpp
    qt_deviceconfig.ui

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

    ../qt_resources.qrc
)
if (APPLE)
    target_sources(ui PRIVATE macos_event_filter.mm)
endif()

target_link_libraries(
    plat
    PRIVATE
    Qt5::Widgets
    Qt5::Gui
    Threads::Threads
)

target_link_libraries(
    ui
    PRIVATE
    Qt5::Widgets
    Qt5::Gui
    Threads::Threads
)

if (PkgConfig_FOUND)
    pkg_check_modules(RTMIDI rtmidi)
    if (RTMIDI_FOUND)
        target_include_directories(plat PRIVATE ${RTMIDI_INCLUDE_DIRS})
        target_link_directories(plat PRIVATE ${RTMIDI_LIBRARY_DIRS})
        target_link_libraries(plat PRIVATE ${RTMIDI_LIBRARIES})
        target_sources(plat PRIVATE rtmidi_midi.cpp)
    else()
        target_sources(plat PRIVATE qt_midi.cpp)
    endif()
else()
    target_sources(plat PRIVATE qt_midi.cpp)
endif()

if (UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    target_link_libraries(ui PRIVATE X11::X11)

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
                target_include_directories(ui PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${Qt5Gui_PRIVATE_INCLUDE_DIRS})
                target_sources(ui PRIVATE ${WL_SOURCE_VAR} wl_mouse.cpp)
                target_compile_definitions(ui PRIVATE WAYLAND)
            endif()
        endif()
    endif()
endif()
