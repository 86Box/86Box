/* WD90C31 drawing engine and hardware cursor, shared by Paradise boards. */
#ifndef VIDEO_WD90C31_H
#define VIDEO_WD90C31_H

typedef struct wd90c31_t wd90c31_t;

wd90c31_t *wd90c31_init(svga_t *svga);
void       wd90c31_close(wd90c31_t *wd);
void       wd90c31_hwcursor_draw(wd90c31_t *wd, int displine);

#endif
