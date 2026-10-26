/* IBM PC Convertible LCD controller and optional CRT adapter. */
#ifndef VIDEO_IBM5140_H
#define VIDEO_IBM5140_H

#include <stdio.h>

typedef struct ibm5140_video_t ibm5140_video_t;

/* Displays: 0..2 LCD, 3 monochrome TTL CGA, 4 RGB CGA, 5 composite.
 * Composite profiles: 0 old CGA, 1 new CGA, 2 existing PCjr pipeline. */
ibm5140_video_t *ibm5140_video_create(int display, int composite);

/* Manufacturing port 80 bit 0 overlays the actual LCD regeneration SRAM
 * at physical 00000..03fff without changing the normal LCD aperture. */
void ibm5140_video_substitute(ibm5140_video_t *video, int enabled);

/* Power-on/reset clears controller state, never retained display/font SRAM. */
void ibm5140_video_reset(ibm5140_video_t *video);

/* The caller owns the retention-file header/version. Only powered SRAM is
 * saved/loaded; reset volatile controller state separately on power-on.
 * Font SRAM includes the firmware's suspend workspace at offset 1c00. */
int ibm5140_video_save(ibm5140_video_t *video, FILE *file);
int ibm5140_video_load(ibm5140_video_t *video, FILE *file);

#endif
