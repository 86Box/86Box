option(MINIZ_ARCHIVE_APIS "Enable miniz's ZIP file API" OFF)
option(MINIZ_DEFLATE_APIS "Enable miniz's compression API" OFF)
option(MINIZ_STDIO "Enable miniz's usage of file IO APIs" OFF)
option(MINIZ_TIME "Enable miniz's usage of time APIs" OFF)

add_library(miniz STATIC
  miniz.c
  miniz.h
)

set_target_properties(miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NOT MINIZ_ARCHIVE_APIS)
  target_compile_definitions(miniz PUBLIC MINIZ_NO_ARCHIVE_APIS)
endif()

if(NOT MINIZ_DEFLATE_APIS)
  target_compile_definitions(miniz PUBLIC MINIZ_NO_DEFLATE_APIS)
endif()

if(NOT MINIZ_STDIO)
  target_compile_definitions(miniz PUBLIC MINIZ_NO_STDIO)
endif()

if(NOT MINIZ_TIME)
  target_compile_definitions(miniz PUBLIC MINIZ_NO_TIME)
endif()
