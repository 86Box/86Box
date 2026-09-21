add_library(zstd STATIC
  zstd.h
  zstd_errors.h
  zstddeclib.c
)

set_target_properties(zstd PROPERTIES POSITION_INDEPENDENT_CODE ON)
