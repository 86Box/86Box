/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the Olivetti XT-compatible machines.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *      EngiNerd <webmaster.crrc@yahoo.it>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2017-2019 Fred N. van Kempen.
 *		Copyright 2020 EngiNerd.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/ppi.h>
#include <86box/nmi.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/nvr.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/rom.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/gameport.h>
#include <86box/sound.h>
#include <86box/snd_speaker.h>
#include <86box/video.h>
#include <86box/machine.h>
#include <86box/vid_cga.h>
#include <86box/vid_ogc.h>
#include <86box/vid_colorplus.h>
#include <86box/vid_cga_comp.h>

#define STAT_PARITY     0x80
#define STAT_RTIMEOUT   0x40
#define STAT_TTIMEOUT   0x20
#define STAT_LOCK       0x10
#define STAT_CD         0x08
#define STAT_SYSFLAG    0x04
#define STAT_IFULL      0x02
#define STAT_OFULL      0x01

#define PLANTRONICS_MODE 1
#define OLIVETTI_OGC_MODE 0

#define CGA_RGB 0
#define CGA_COMPOSITE 1

typedef struct {
    /* Keyboard stuff. */
    int		wantirq;
    uint8_t	command;
    uint8_t	status;
    uint8_t	out;
    uint8_t	output_port;
    int		param,
		param_total;
    uint8_t	params[16];
    uint8_t	scan[7];

    /* Mouse stuff. */
    int		mouse_mode;
    int		x, y, b;
	pc_timer_t send_delay_timer;
} m24_kbd_t;

typedef struct {
	ogc_t ogc;
	colorplus_t colorplus;
	int mode;
} m19_vid_t;

static uint8_t	key_queue[16];
static int	key_queue_start = 0,
		key_queue_end = 0;

video_timings_t timing_m19_vid = {VIDEO_ISA, 8, 16, 32,   8, 16, 32};

#ifdef ENABLE_M24VID_LOG
int m24vid_do_log = ENABLE_M24VID_LOG;


static void
m24_log(const char *fmt, ...)
{
    va_list ap;

    if (m24vid_do_log) {
	va_start(ap, fmt);
	vfprintf(stdlog, fmt, ap);
	va_end(ap);
	fflush(stdlog);
    }
}
#else
#define m24_log(fmt, ...)
#endif


static void
m24_kbd_poll(void *priv)
{
    m24_kbd_t *m24_kbd = (m24_kbd_t *)priv;

    timer_advance_u64(&m24_kbd->send_delay_timer, 1000 * TIMER_USEC);
    if (m24_kbd->wantirq) {
	m24_kbd->wantirq = 0;
	picint(2);
#if ENABLE_KEYBOARD_LOG
	m24_log("M24: take IRQ\n");
#endif
    }

    if (!(m24_kbd->status & STAT_OFULL) && key_queue_start != key_queue_end) {
#if ENABLE_KEYBOARD_LOG
	m24_log("Reading %02X from the key queue at %i\n",
				m24_kbd->out, key_queue_start);
#endif
	m24_kbd->out = key_queue[key_queue_start];
	key_queue_start = (key_queue_start + 1) & 0xf;
	m24_kbd->status |=  STAT_OFULL;
	m24_kbd->status &= ~STAT_IFULL;
	m24_kbd->wantirq = 1;
    }
}


static void
m24_kbd_adddata(uint16_t val)
{
    key_queue[key_queue_end] = val;
    key_queue_end = (key_queue_end + 1) & 0xf;
}


static void
m24_kbd_adddata_ex(uint16_t val)
{
    kbd_adddata_process(val, m24_kbd_adddata);
}


static void
m24_kbd_write(uint16_t port, uint8_t val, void *priv)
{
    m24_kbd_t *m24_kbd = (m24_kbd_t *)priv;

#if ENABLE_KEYBOARD_LOG
    m24_log("M24: write %04X %02X\n", port, val);
#endif

#if 0
    if (ram[8] == 0xc3)
	output = 3;
#endif
    switch (port) {
	case 0x60:
		if (m24_kbd->param != m24_kbd->param_total) {
			m24_kbd->params[m24_kbd->param++] = val;
			if (m24_kbd->param == m24_kbd->param_total) {
				switch (m24_kbd->command) {
					case 0x11:
						m24_kbd->mouse_mode = 0;
						m24_kbd->scan[0] = m24_kbd->params[0];
						m24_kbd->scan[1] = m24_kbd->params[1];
						m24_kbd->scan[2] = m24_kbd->params[2];
						m24_kbd->scan[3] = m24_kbd->params[3];
						m24_kbd->scan[4] = m24_kbd->params[4];
						m24_kbd->scan[5] = m24_kbd->params[5];
						m24_kbd->scan[6] = m24_kbd->params[6];
						break;

					case 0x12:
						m24_kbd->mouse_mode = 1;
						m24_kbd->scan[0] = m24_kbd->params[0];
						m24_kbd->scan[1] = m24_kbd->params[1];
						m24_kbd->scan[2] = m24_kbd->params[2];
						break;
					
					default:
						m24_log("M24: bad keyboard command complete %02X\n", m24_kbd->command);
				}
			}
		} else {
			m24_kbd->command = val;
			switch (val) {
				case 0x01: /*Self-test*/
					break;

				case 0x05: /*Read ID*/
					m24_kbd_adddata(0x00);
					break;

				case 0x11:
					m24_kbd->param = 0;
					m24_kbd->param_total = 9;
					break;

				case 0x12:
					m24_kbd->param = 0;
					m24_kbd->param_total = 4;
					break;

				default:
					m24_log("M24: bad keyboard command %02X\n", val);
			}
		}
		break;

	case 0x61:
		ppi.pb = val;

		speaker_update();
		speaker_gated = val & 1;
		speaker_enable = val & 2;
		if (speaker_enable) 
			was_speaker_enable = 1;
		pit_ctr_set_gate(&pit->counters[2], val & 1);
		break;
    }
}


static uint8_t
m24_kbd_read(uint16_t port, void *priv)
{
    m24_kbd_t *m24_kbd = (m24_kbd_t *)priv;
    uint8_t ret = 0xff;

    switch (port) {
	case 0x60:
		ret = m24_kbd->out;
		if (key_queue_start == key_queue_end) {
			m24_kbd->status &= ~STAT_OFULL;
			m24_kbd->wantirq = 0;
		} else {
			m24_kbd->out = key_queue[key_queue_start];
			key_queue_start = (key_queue_start + 1) & 0xf;
			m24_kbd->status |= STAT_OFULL;
			m24_kbd->status &= ~STAT_IFULL;
			m24_kbd->wantirq = 1;	
		}
		break;

	case 0x61:
		ret = ppi.pb;
		break;

	case 0x64:
		ret = m24_kbd->status;
		m24_kbd->status &= ~(STAT_RTIMEOUT | STAT_TTIMEOUT);
		break;

	default:
		m24_log("\nBad M24 keyboard read %04X\n", port);
    }

    return(ret);
}


static void
m24_kbd_close(void *priv)
{
    m24_kbd_t *kbd = (m24_kbd_t *)priv;

    /* Stop the timer. */
    timer_disable(&kbd->send_delay_timer);
 
    /* Disable scanning. */
    keyboard_scan = 0;

    keyboard_send = NULL;

    io_removehandler(0x0060, 2,
		     m24_kbd_read, NULL, NULL, m24_kbd_write, NULL, NULL, kbd);
    io_removehandler(0x0064, 1,
		     m24_kbd_read, NULL, NULL, m24_kbd_write, NULL, NULL, kbd);

    free(kbd);
}


static void
m24_kbd_reset(void *priv)
{
    m24_kbd_t *m24_kbd = (m24_kbd_t *)priv;
 
    /* Initialize the keyboard. */
    m24_kbd->status = STAT_LOCK | STAT_CD;
    m24_kbd->wantirq = 0;
    keyboard_scan = 1;
    m24_kbd->param = m24_kbd->param_total = 0;
    m24_kbd->mouse_mode = 0;
    m24_kbd->scan[0] = 0x1c;
    m24_kbd->scan[1] = 0x53;
    m24_kbd->scan[2] = 0x01;
    m24_kbd->scan[3] = 0x4b;
    m24_kbd->scan[4] = 0x4d;
    m24_kbd->scan[5] = 0x48;
    m24_kbd->scan[6] = 0x50;   
}


static int
ms_poll(int x, int y, int z, int b, void *priv)
{
    m24_kbd_t *m24_kbd = (m24_kbd_t *)priv;

    m24_kbd->x += x;
    m24_kbd->y += y;

    if (((key_queue_end - key_queue_start) & 0xf) > 14) return(0xff);

    if ((b & 1) && !(m24_kbd->b & 1))
	m24_kbd_adddata(m24_kbd->scan[0]);
    if (!(b & 1) && (m24_kbd->b & 1))
	m24_kbd_adddata(m24_kbd->scan[0] | 0x80);
    m24_kbd->b = (m24_kbd->b & ~1) | (b & 1);

    if (((key_queue_end - key_queue_start) & 0xf) > 14) return(0xff);

    if ((b & 2) && !(m24_kbd->b & 2))
	m24_kbd_adddata(m24_kbd->scan[2]);
    if (!(b & 2) && (m24_kbd->b & 2))
	m24_kbd_adddata(m24_kbd->scan[2] | 0x80);
    m24_kbd->b = (m24_kbd->b & ~2) | (b & 2);

    if (((key_queue_end - key_queue_start) & 0xf) > 14) return(0xff);

    if ((b & 4) && !(m24_kbd->b & 4))
	m24_kbd_adddata(m24_kbd->scan[1]);
    if (!(b & 4) && (m24_kbd->b & 4))
	m24_kbd_adddata(m24_kbd->scan[1] | 0x80);
    m24_kbd->b = (m24_kbd->b & ~4) | (b & 4);

    if (m24_kbd->mouse_mode) {
	if (((key_queue_end - key_queue_start) & 0xf) > 12) return(0xff);

	if (!m24_kbd->x && !m24_kbd->y) return(0xff);
	
	m24_kbd->y = -m24_kbd->y;

	if (m24_kbd->x < -127) m24_kbd->x = -127;
	if (m24_kbd->x >  127) m24_kbd->x =  127;
	if (m24_kbd->x < -127) m24_kbd->x = 0x80 | ((-m24_kbd->x) & 0x7f);

	if (m24_kbd->y < -127) m24_kbd->y = -127;
	if (m24_kbd->y >  127) m24_kbd->y =  127;
	if (m24_kbd->y < -127) m24_kbd->y = 0x80 | ((-m24_kbd->y) & 0x7f);

	m24_kbd_adddata(0xfe);
	m24_kbd_adddata(m24_kbd->x);
	m24_kbd_adddata(m24_kbd->y);

	m24_kbd->x = m24_kbd->y = 0;
    } else {
	while (m24_kbd->x < -4) {
		if (((key_queue_end - key_queue_start) & 0xf) > 14)
							return(0xff);
		m24_kbd->x += 4;
		m24_kbd_adddata(m24_kbd->scan[3]);
	}
	while (m24_kbd->x > 4) {
		if (((key_queue_end - key_queue_start) & 0xf) > 14)
							return(0xff);
		m24_kbd->x -= 4;
		m24_kbd_adddata(m24_kbd->scan[4]);
	}
	while (m24_kbd->y < -4) {
		if (((key_queue_end - key_queue_start) & 0xf) > 14)
							return(0xff);
		m24_kbd->y += 4;
		m24_kbd_adddata(m24_kbd->scan[5]);
	}
	while (m24_kbd->y > 4) {
		if (((key_queue_end - key_queue_start) & 0xf) > 14)
							return(0xff);
		m24_kbd->y -= 4;
		m24_kbd_adddata(m24_kbd->scan[6]);
	}
    }

    return(0);
}


static void
m24_kbd_init(m24_kbd_t *kbd)
{
	
    /* Initialize the keyboard. */
    io_sethandler(0x0060, 2,
		  m24_kbd_read, NULL, NULL, m24_kbd_write, NULL, NULL, kbd);
    io_sethandler(0x0064, 1,
		  m24_kbd_read, NULL, NULL, m24_kbd_write, NULL, NULL, kbd);
    keyboard_send = m24_kbd_adddata_ex;
    m24_kbd_reset(kbd);
    timer_add(&kbd->send_delay_timer, m24_kbd_poll, kbd, 1);

    /* Tell mouse driver about our internal mouse. */
    mouse_reset();
    mouse_set_poll(ms_poll, kbd);

    keyboard_set_table(scancode_xt);
    keyboard_set_is_amstrad(0);
}


static void
m19_vid_out(uint16_t addr, uint8_t val, void *priv)
{
    m19_vid_t *vid = (m19_vid_t *)priv;
    int oldmode = vid->mode;

    /* activating plantronics mode */
    if (addr == 0x3dd) {
	/* already in graphics mode */
	if ((val & 0x30) && (vid->ogc.cga.cgamode & 0x2))
		vid->mode = PLANTRONICS_MODE;
	else
		vid->mode = OLIVETTI_OGC_MODE;
    /* setting graphics mode */
    } else if (addr == 0x3d8) {
	if ((val & 0x2) && (vid->colorplus.control & 0x30))
		vid->mode = PLANTRONICS_MODE;
	else
		vid->mode = OLIVETTI_OGC_MODE;
    }
    /* video mode changed */
    if (oldmode != vid->mode) {
	/* activate Plantronics emulation */
	if (vid->mode == PLANTRONICS_MODE){
		timer_disable(&vid->ogc.cga.timer);
		timer_set_delay_u64(&vid->colorplus.cga.timer, 0);
	/* return to OGC mode */
	} else {
		timer_disable(&vid->colorplus.cga.timer);
		timer_set_delay_u64(&vid->ogc.cga.timer, 0);
	}

	colorplus_recalctimings(&vid->colorplus);
	ogc_recalctimings(&vid->ogc);
    }

    colorplus_out(addr, val, &vid->colorplus);
    ogc_out(addr, val, &vid->ogc);
}


static uint8_t
m19_vid_in(uint16_t addr, void *priv)
{
    m19_vid_t *vid = (m19_vid_t *)priv;

    if (vid->mode == PLANTRONICS_MODE)
	return colorplus_in(addr, &vid->colorplus);
    else
	return ogc_in(addr, &vid->ogc);
}


static uint8_t
m19_vid_read(uint32_t addr, void *priv)
{
    m19_vid_t *vid = (m19_vid_t *)priv;

    vid->colorplus.cga.mapping = vid->ogc.cga.mapping;
    if (vid->mode == PLANTRONICS_MODE)
	return colorplus_read(addr, &vid->colorplus);
    else
	return ogc_read(addr, &vid->ogc);
}


static void
m19_vid_write(uint32_t addr, uint8_t val, void *priv)
{
    m19_vid_t *vid = (m19_vid_t *)priv;

    colorplus_write(addr, val, &vid->colorplus);
    ogc_write(addr, val, &vid->ogc);
}


static void
m19_vid_close(void *priv)
{
    m19_vid_t *vid = (m19_vid_t *)priv;

    free(vid->ogc.cga.vram);
    free(vid->colorplus.cga.vram);
    free(vid);
}


static void
m19_vid_speed_changed(void *priv)
{
    m19_vid_t *vid = (m19_vid_t *)priv;

    colorplus_recalctimings(&vid->colorplus);
    ogc_recalctimings(&vid->ogc);
}


static void
m19_vid_init(m19_vid_t *vid)
{
    /* int display_type; */
    vid->mode = OLIVETTI_OGC_MODE;

    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_m19_vid);

    /* display_type = device_get_config_int("display_type"); */

    /* OGC emulation part begin */
    loadfont_ex("roms/machines/m19/BIOS.BIN", 1, 90);
    /* composite is not working yet */
    vid->ogc.cga.composite = 0; // (display_type != CGA_RGB);
    /* vid->ogc.cga.snow_enabled = device_get_config_int("snow_enabled"); */

    vid->ogc.cga.vram = malloc(0x8000);

    /* cga_comp_init(vid->ogc.cga.revision); */

    /* vid->ogc.cga.rgb_type = device_get_config_int("rgb_type"); */
    /* cga_palette = (vid->ogc.cga.rgb_type << 1); */
    cga_palette = 0;
    cgapal_rebuild();
    ogc_mdaattr_rebuild();

    /* color display */
    /* if (device_get_config_int("rgb_type")==0 || device_get_config_int("rgb_type") == 4)  */
    vid->ogc.mono_display = 1;
    /* else */
    /* 	vid->ogc.mono_display = 1;     */
    /* OGC emulation part end */

    /* Plantronics emulation part begin*/
    /* composite is not working yet */
    vid->colorplus.cga.composite = 0; //(display_type != CGA_RGB);
    /* vid->colorplus.cga.snow_enabled = device_get_config_int("snow_enabled"); */

    vid->colorplus.cga.vram = malloc(0x8000);

    /* vid->colorplus.cga.cgamode = 0x1; */
    /* Plantronics emulation part end*/

    timer_add(&vid->ogc.cga.timer, ogc_poll, &vid->ogc, 1);
    timer_add(&vid->colorplus.cga.timer, colorplus_poll, &vid->colorplus, 1);
    timer_disable(&vid->colorplus.cga.timer);
    mem_mapping_add(&vid->ogc.cga.mapping, 0xb8000, 0x08000, m19_vid_read, NULL, NULL, m19_vid_write, NULL, NULL,  NULL, MEM_MAPPING_EXTERNAL, vid);
    io_sethandler(0x03d0, 0x0010, m19_vid_in, NULL, NULL, m19_vid_out, NULL, NULL, vid);

    vid->mode = OLIVETTI_OGC_MODE;
}


const device_t m24_kbd_device = {
    "Olivetti M24 keyboard and mouse",
    0,
    0,
    NULL,
    m24_kbd_close,
    m24_kbd_reset,
    { NULL }, NULL, NULL
};

const device_t m19_vid_device = {
    "Olivetti M19 graphics card",
    0, 0,
    NULL, m19_vid_close, NULL,
    { NULL },
    m19_vid_speed_changed,
    NULL,
    NULL
};

const device_t *
m19_get_device(void)
{
    return &m19_vid_device;
}


static uint8_t
m24_read(uint16_t port, void *priv)
{
    uint8_t ret = 0x00;
    int i, fdd_count = 0; 

    switch (port) {
	/* 
	 * port 66:
	 * DIPSW-0 on mainboard (off=present=1)
	 * bit 7 - 2764 (off) / 2732 (on) ROM (BIOS < 1.36)
	 * bit 7 - Use (off) / do not use (on) memory bank 1 (BIOS >= 1.36)
	 * bit 6 - n/a
	 * bit 5 - 8530 (off) / 8250 (on) SCC
	 * bit 4 - 8087 present
	 * bits 3-0 - installed memory
	 */
	case 0x66:
		/* Switch 5 - 8087 present */
		if (hasfpu)
			ret |= 0x10;
		/* 
		 * Switches 1, 2, 3, 4 - installed memory 
		 * Switch 8 - Use memory bank 1
		 */
		switch (mem_size) {
			case 128:
				ret |= 0x1;
				break;
			case 256:
				ret |= 0x2|0x80;
				break;
			case 384:
				ret |= 0x1|0x2|0x80;
				break;
			case 512:
				ret |= 0x8;
				break;
			case 640:
			default:
				ret |= 0x1|0x8|0x80;
				break;
        }
	/* 
	 * port 67:
	 * DIPSW-1 on mainboard (off=present=1)
	 * bits 7-6 - number of drives
	 * bits 5-4 - display adapter
	 * bit 3 - video scroll CPU (on) / slow scroll (off)
	 * bit 2 - BIOS HD on mainboard (on) / on controller (off)
	 * bit 1 - FDD fast (off) / slow (on) start drive
	 * bit 0 - 96 TPI (720 KB 3.5") (off) / 48 TPI (360 KB 5.25") FDD drive
	 * 
	 * Display adapter:
	 * off off 80x25 mono
	 * off on  40x25 color
	 * on off  80x25 color
	 * on on   EGA/VGA (works only for BIOS ROM 1.43)
	 */
	case 0x67:
		for (i = 0; i < FDD_NUM; i++) {
			if (fdd_get_flags(i))
				fdd_count++;
		}

		/* Switches 7, 8 - floppy drives. */
		if (!fdd_count)
			ret |= 0x00;
		else
			ret |= ((fdd_count - 1) << 6);
        
		/* Switches 5, 6 - monitor type */
		if (video_is_mda())
			ret |= 0x30;
		else if (video_is_cga())
			ret |= 0x20;	/* 0x10 would be 40x25 */
		else
			ret |= 0x0;
		
		/* Switch 3 - Disable internal BIOS HD */
		ret |= 0x4;
		
		/* Switch 2 - Set fast startup */
		ret |= 0x2;
    }

    return(ret);
}


const device_t *
m24_get_device(void)
{
    return &ogc_m24_device;
}


int
machine_xt_m24_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/m24/olivetti_m24_version_1.43_low.bin",
				"roms/machines/m24/olivetti_m24_version_1.43_high.bin",
				0x000fc000, 16384, 0);

    if (bios_only || !ret)
	return ret;

    if (gfxcard == VID_INTERNAL)
    	device_add(&ogc_m24_device);

    m24_kbd_t *m24_kbd;

    m24_kbd = (m24_kbd_t *) malloc(sizeof(m24_kbd_t));
    memset(m24_kbd, 0x00, sizeof(m24_kbd_t));

    machine_common_init(model);

    /* On-board FDC can be disabled only on M24SP */
    if (fdc_type == FDC_INTERNAL)
	device_add(&fdc_xt_device);

    /* Address 66-67 = mainboard dip-switch settings */
    io_sethandler(0x0066, 2, m24_read, NULL, NULL, NULL, NULL, NULL, NULL);

    m24_kbd_init(m24_kbd);
    device_add_ex(&m24_kbd_device, m24_kbd);

    /* FIXME: make sure this is correct?? */
    device_add(&at_nvr_device);

    standalone_gameport_type = &gameport_device;

    nmi_init();

    return ret;
}

/*
 * Current bugs: 
 * - handles only 360kb floppy drives (drive type and capacity selectable with jumpers mapped to unknown memory locations)
 */
int
machine_xt_m240_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/m240/olivetti_m240_pch6_2.04_low.bin",
				"roms/machines/m240/olivetti_m240_pch5_2.04_high.bin",
				0x000f8000, 32768, 0);

    if (bios_only || !ret)
	return ret;

    machine_common_init(model);

    pit_ctr_set_out_func(&pit->counters[1], pit_refresh_timer_xt);

    /* 
     * port 60: should return jumper settings only under unknown conditions
     * SWB on mainboard (off=1)
     * bit 7 - use BIOS HD on mainboard (on) / on controller (off)
     * bit 6 - use OCG/CGA display adapter (on) / other display adapter (off)
     */
    device_add(&keyboard_at_olivetti_device);

    /* FIXME: make sure this is correct?? */
    device_add(&at_nvr_device);

    if (fdc_type == FDC_INTERNAL)
	device_add(&fdc_xt_device);

    if (joystick_type)
	device_add(&gameport_device);

    nmi_init();

    return ret;
}


/*
 * Current bugs: 
 * - 640x400x2 graphics mode not supported (bit 0 of register 0x3de cannot be set)
 * - optional mouse emulation missing
 * - setting CPU speed at 4.77MHz sometimes throws a timer error. If the machine is hard-resetted, the error disappears.
 */
int
machine_xt_m19_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/m19/BIOS.BIN",
			   0x000fc000, 16384, 0);

    if (bios_only || !ret)
	return ret;

    m19_vid_t *vid;

    /* Do not move memory allocation elsewhere. */
    vid = (m19_vid_t *) malloc(sizeof(m19_vid_t));
    memset(vid, 0x00, sizeof(m19_vid_t));

    machine_common_init(model);

    /* On-board FDC cannot be disabled */
    device_add(&fdc_xt_device);

    m19_vid_init(vid);
    device_add_ex(&m19_vid_device, vid);

    device_add(&keyboard_xt_olivetti_device);

    nmi_init();

    return ret;

}
