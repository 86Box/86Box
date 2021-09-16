add_library(uicommon OBJECT imgui_tables.cpp imgui_widgets.cpp imgui.cpp
                      imgui_sdl.cpp imgui_impl_sdl.cpp imgui_demo.cpp imgui_draw.cpp imgui_ui.cpp)
#target_link_libraries(uicommon hdd zip mo)
target_compile_definitions(uicommon PUBLIC _FILE_OFFSET_BITS=64 _INCBIN_DIR="${CMAKE_CURRENT_SOURCE_DIR}")