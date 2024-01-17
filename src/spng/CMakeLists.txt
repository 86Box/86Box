add_library(spng OBJECT miniz.c spng.c)
target_compile_definitions(spng PRIVATE -DSPNG_USE_MINIZ=1)