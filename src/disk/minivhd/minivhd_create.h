#ifndef MINIVHD_CREATE_H
#define MINIVHD_CREATE_H
#include <stdio.h>
#include "minivhd.h"

MVHDMeta* mvhd_create_fixed_raw(const char* path, FILE* raw_img, uint64_t size_in_bytes, MVHDGeom* geom, int* err, mvhd_progress_callback progress_callback);

#endif
