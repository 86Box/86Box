/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the PGC driver.
 *
 *
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		John Elliott, <jce@seasip.info>
 *
 *		Copyright 2019 Fred N. van Kempen.
 *		Copyright 2019 John Elliott.
 */
#ifndef VID_PGC_H
# define VID_PGC_H

#define PGC_ERROR_RANGE    0x01
#define PGC_ERROR_INTEGER  0x02
#define PGC_ERROR_MEMORY   0x03
#define PGC_ERROR_OVERFLOW 0x04
#define PGC_ERROR_DIGIT    0x05
#define PGC_ERROR_OPCODE   0x06
#define PGC_ERROR_RUNNING  0x07
#define PGC_ERROR_STACK    0x08
#define PGC_ERROR_TOOLONG  0x09
#define PGC_ERROR_AREA     0x0A
#define PGC_ERROR_MISSING  0x0B


struct pgc;

typedef struct pgc_cl {
    uint8_t	*list;
    uint32_t	listmax;
    uint32_t	wrptr;
    uint32_t	rdptr;
    uint32_t	repeat;
    struct pgc_cl *chain;
} pgc_cl_t;

typedef struct pgc_cmd {
    char	ascii[6];
    uint8_t	hex;
    void	(*handler)(struct pgc *);
    int		(*parser) (struct pgc *, pgc_cl_t *, int);
    int p;
} pgc_cmd_t;

typedef struct pgc {
    int8_t		type;			/* board type */
    int8_t		cga_enabled;
    int8_t		cga_selected;
    volatile int8_t	stopped;

    mem_mapping_t	mapping;
    mem_mapping_t	cga_mapping;

    pgc_cl_t	*clist,
		*clcur;
    const pgc_cmd_t *master,
		*commands;

    uint8_t	mapram[2048];		/* host <> PGC communication buffer */
    uint8_t	*cga_vram;
    uint8_t	*vram;
    char	asc_command[7];
    uint8_t	hex_command;
    uint32_t	palette[256];
    uint32_t	userpal[256];
    uint32_t	maxw, maxh;		/* maximum framebuffer size */
    uint32_t	visw, vish;		/* maximum screen size */
    uint32_t	screenw, screenh;
    int16_t	pan_x, pan_y;
    uint16_t	win_x1, win_x2, win_y1, win_y2;
    uint16_t	vp_x1, vp_x2, vp_y1, vp_y2;
    int16_t	fill_pattern[16];
    int16_t	line_pattern;
    uint8_t	draw_mode;
    uint8_t	fill_mode;
    uint8_t	color;
    uint8_t	tjust_h;		/* hor alignment 1=left 2=ctr 3=right*/
    uint8_t	tjust_v;		/* vert alignment 1=bottom 2=ctr 3=top*/
    int32_t	tsize;			/* horizontal spacing */

    int32_t	x, y, z;		/* drawing position */

    thread_t	*pgc_thread;
    event_t	*pgc_wake_thread;
    pc_timer_t	wake_timer;

    int		waiting_input_fifo;
    int		waiting_output_fifo;
    int		waiting_error_fifo;
    int		ascii_mode;
    int		result_count;

    int		fontbase;
    int		linepos,
		displine;
    int		vc;
    int		cgadispon;
    int		con, coff, cursoron, cgablink;
    int		vsynctime, vadj;
    uint16_t	ma, maback;
    int		oddeven;

    uint64_t	dispontime,
		dispofftime;
	pc_timer_t	timer;
	double native_pixel_clock;

    int		drawcursor;

    int		(*inputbyte)(struct pgc *, uint8_t *result);
} pgc_t;


/* I/O functions and worker thread handlers. */
extern void	pgc_out(uint16_t addr, uint8_t val, void *priv);
extern uint8_t	pgc_in(uint16_t addr, void *priv);
extern void	pgc_write(uint32_t addr, uint8_t val, void *priv);
extern uint8_t	pgc_read(uint32_t addr, void *priv);
extern void	pgc_recalctimings(pgc_t *);
extern void	pgc_poll(void *priv);
extern void	pgc_reset(pgc_t *);
extern void	pgc_wake(pgc_t *);
extern void	pgc_sleep(pgc_t *);
extern void	pgc_setdisplay(pgc_t *, int cga);
extern void	pgc_speed_changed(void *priv);
extern void	pgc_close_common(void *priv);
extern void	pgc_close(void *priv);
extern void	pgc_init(pgc_t *,
			 int maxw, int maxh, int visw, int vish,
			 int (*inpbyte)(pgc_t *, uint8_t *), double npc);

/* Misc support functions. */
extern void	pgc_sto_raster(pgc_t *, int16_t *x, int16_t *y);
extern void	pgc_ito_raster(pgc_t *, int32_t *x, int32_t *y);
extern void	pgc_dto_raster(pgc_t *, double *x, double *y);
//extern int	pgc_input_byte(pgc_t *, uint8_t *val);
//extern int	pgc_output_byte(pgc_t *, uint8_t val);
extern int	pgc_output_string(pgc_t *, const char *val);
//extern int	pgc_error_byte(pgc_t *, uint8_t val);
extern int	pgc_error_string(pgc_t *, const char *val);
extern int	pgc_error(pgc_t *, int err);

/* Graphics functions. */
extern uint8_t	*pgc_vram_addr(pgc_t *, int16_t x, int16_t y);
extern void	pgc_write_pixel(pgc_t *, uint16_t x, uint16_t y, uint8_t ink);
extern uint8_t	pgc_read_pixel(pgc_t *, uint16_t x, uint16_t y);
extern void	pgc_plot(pgc_t *, uint16_t x, uint16_t y);
extern uint16_t	pgc_draw_line_r(pgc_t *, int32_t x1, int32_t y1,
				int32_t x2, int32_t y2, uint16_t linemask);
extern void	pgc_fill_line_r(pgc_t *, int32_t x0, int32_t x1, int32_t y);
extern uint16_t	pgc_draw_line(pgc_t *, int32_t x1, int32_t y1,
			      int32_t x2, int32_t y2, uint16_t linemask);
extern void	pgc_draw_ellipse(pgc_t *, int32_t x, int32_t y);
extern void	pgc_fill_polygon(pgc_t *,
				 unsigned corners, int32_t *x, int32_t *y);

/* Command and parameter handling functions. */
extern int	pgc_clist_byte(pgc_t *, uint8_t *val);
extern int	pgc_cl_append(pgc_cl_t *, uint8_t v);
extern int	pgc_parse_bytes(pgc_t *, pgc_cl_t *, int p);
extern int	pgc_parse_words(pgc_t *, pgc_cl_t *, int p);
extern int	pgc_parse_coords(pgc_t *, pgc_cl_t *, int p);
extern int	pgc_param_byte(pgc_t *, uint8_t *val);
extern int	pgc_param_word(pgc_t *, int16_t *val);
extern int	pgc_param_coord(pgc_t *, int32_t *val);
extern int	pgc_result_byte(pgc_t *, uint8_t val);
extern int	pgc_result_word(pgc_t *, int16_t val);
extern int	pgc_result_coord(pgc_t *, int32_t val);

/* Special overload functions for non-IBM implementations. */
extern void	pgc_hndl_lut8(pgc_t *);
extern void	pgc_hndl_lut8rd(pgc_t *);

#endif	/*VID_PGC_H*/
