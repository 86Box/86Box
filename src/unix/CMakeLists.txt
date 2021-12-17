add_library(plat OBJECT unix.c)

if (NOT CPPTHREADS)
    target_sources(plat PRIVATE unix_thread.c)
endif()

set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)
target_link_libraries(86Box Threads::Threads)

add_library(ui OBJECT unix_sdl.c unix_cdrom.c)
target_compile_definitions(ui PUBLIC _FILE_OFFSET_BITS=64)
target_link_libraries(ui dl)
