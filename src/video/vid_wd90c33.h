/* WD90C33 drawing engine, command buffer and hardware cursor. */
#ifndef VIDEO_WD90C33_H
#define VIDEO_WD90C33_H

typedef struct wd90c33_t wd90c33_t;

wd90c33_t *wd90c33_init(svga_t *svga);
void       wd90c33_close(wd90c33_t *wd);
void       wd90c33_hwcursor_draw(wd90c33_t *wd, int displine);
int        wd90c33_mem_write(wd90c33_t *wd, uint32_t addr, uint16_t value, int size);

#endif
