extern int firstline_draw, lastline_draw;
extern int displine;
extern int sc;

extern uint32_t ma, ca;
extern int con, cursoron, cgablink;

extern int scrollcache;

extern uint8_t edatlookup[4][4];

void svga_render_blank(svga_t *svga);
void svga_render_text_40(svga_t *svga);
void svga_render_text_80(svga_t *svga);

void svga_render_2bpp_lowres(svga_t *svga);
void svga_render_2bpp_highres(svga_t *svga);
void svga_render_4bpp_lowres(svga_t *svga);
void svga_render_4bpp_highres(svga_t *svga);
void svga_render_8bpp_lowres(svga_t *svga);
void svga_render_8bpp_highres(svga_t *svga);
void svga_render_15bpp_lowres(svga_t *svga);
void svga_render_15bpp_highres(svga_t *svga);
void svga_render_16bpp_lowres(svga_t *svga);
void svga_render_16bpp_highres(svga_t *svga);
void svga_render_24bpp_lowres(svga_t *svga);
void svga_render_24bpp_highres(svga_t *svga);
void svga_render_32bpp_lowres(svga_t *svga);
void svga_render_32bpp_highres(svga_t *svga);
void svga_render_ABGR8888_highres(svga_t *svga);
void svga_render_RGBA8888_highres(svga_t *svga);

extern void (*svga_render)(svga_t *svga);
