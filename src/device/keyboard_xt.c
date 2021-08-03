/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the XT-style keyboard.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *      EngiNerd, <webmaster.crrc@yahoo.it>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2017-2019 Fred N. van kempen.
 *      Copyright 2020 EngiNerd.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define HAVE_STDARG_H
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/machine.h>
#include <86box/m_xt_t1000.h>
#include <86box/cassette.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/ppi.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/sound.h>
#include <86box/snd_speaker.h>
#include <86box/video.h>
#include <86box/keyboard.h>


#define STAT_PARITY     0x80
#define STAT_RTIMEOUT   0x40
#define STAT_TTIMEOUT   0x20
#define STAT_LOCK       0x10
#define STAT_CD         0x08
#define STAT_SYSFLAG    0x04
#define STAT_IFULL      0x02
#define STAT_OFULL      0x01


typedef struct {
    int want_irq;        
    int blocked;
    int tandy;

    uint8_t pa, pb, pd;
    uint8_t key_waiting;
    uint8_t type;

    pc_timer_t send_delay_timer;
} xtkbd_t;


/*XT keyboard has no escape scancodes, and no scancodes beyond 53*/
const scancode scancode_xt[512] = {
    { {0},       {0}       }, { {0x01, 0}, {0x81, 0} },
    { {0x02, 0}, {0x82, 0} }, { {0x03, 0}, {0x83, 0} },
    { {0x04, 0}, {0x84, 0} }, { {0x05, 0}, {0x85, 0} },
    { {0x06, 0}, {0x86, 0} }, { {0x07, 0}, {0x87, 0} },
    { {0x08, 0}, {0x88, 0} }, { {0x09, 0}, {0x89, 0} },
    { {0x0a, 0}, {0x8a, 0} }, { {0x0b, 0}, {0x8b, 0} },
    { {0x0c, 0}, {0x8c, 0} }, { {0x0d, 0}, {0x8d, 0} },
    { {0x0e, 0}, {0x8e, 0} }, { {0x0f, 0}, {0x8f, 0} },
    { {0x10, 0}, {0x90, 0} }, { {0x11, 0}, {0x91, 0} },
    { {0x12, 0}, {0x92, 0} }, { {0x13, 0}, {0x93, 0} },
    { {0x14, 0}, {0x94, 0} }, { {0x15, 0}, {0x95, 0} },
    { {0x16, 0}, {0x96, 0} }, { {0x17, 0}, {0x97, 0} },
    { {0x18, 0}, {0x98, 0} }, { {0x19, 0}, {0x99, 0} },
    { {0x1a, 0}, {0x9a, 0} }, { {0x1b, 0}, {0x9b, 0} },
    { {0x1c, 0}, {0x9c, 0} }, { {0x1d, 0}, {0x9d, 0} },
    { {0x1e, 0}, {0x9e, 0} }, { {0x1f, 0}, {0x9f, 0} },
    { {0x20, 0}, {0xa0, 0} }, { {0x21, 0}, {0xa1, 0} },
    { {0x22, 0}, {0xa2, 0} }, { {0x23, 0}, {0xa3, 0} },
    { {0x24, 0}, {0xa4, 0} }, { {0x25, 0}, {0xa5, 0} },
    { {0x26, 0}, {0xa6, 0} }, { {0x27, 0}, {0xa7, 0} },
    { {0x28, 0}, {0xa8, 0} }, { {0x29, 0}, {0xa9, 0} },
    { {0x2a, 0}, {0xaa, 0} }, { {0x2b, 0}, {0xab, 0} },
    { {0x2c, 0}, {0xac, 0} }, { {0x2d, 0}, {0xad, 0} },
    { {0x2e, 0}, {0xae, 0} }, { {0x2f, 0}, {0xaf, 0} },
    { {0x30, 0}, {0xb0, 0} }, { {0x31, 0}, {0xb1, 0} },
    { {0x32, 0}, {0xb2, 0} }, { {0x33, 0}, {0xb3, 0} },
    { {0x34, 0}, {0xb4, 0} }, { {0x35, 0}, {0xb5, 0} },
    { {0x36, 0}, {0xb6, 0} }, { {0x37, 0}, {0xb7, 0} },
    { {0x38, 0}, {0xb8, 0} }, { {0x39, 0}, {0xb9, 0} },
    { {0x3a, 0}, {0xba, 0} }, { {0x3b, 0}, {0xbb, 0} },
    { {0x3c, 0}, {0xbc, 0} }, { {0x3d, 0}, {0xbd, 0} },
    { {0x3e, 0}, {0xbe, 0} }, { {0x3f, 0}, {0xbf, 0} },
    { {0x40, 0}, {0xc0, 0} }, { {0x41, 0}, {0xc1, 0} },
    { {0x42, 0}, {0xc2, 0} }, { {0x43, 0}, {0xc3, 0} },
    { {0x44, 0}, {0xc4, 0} }, { {0x45, 0}, {0xc5, 0} },
    { {0x46, 0}, {0xc6, 0} }, { {0x47, 0}, {0xc7, 0} },
    { {0x48, 0}, {0xc8, 0} }, { {0x49, 0}, {0xc9, 0} },
    { {0x4a, 0}, {0xca, 0} }, { {0x4b, 0}, {0xcb, 0} },
    { {0x4c, 0}, {0xcc, 0} }, { {0x4d, 0}, {0xcd, 0} },
    { {0x4e, 0}, {0xce, 0} }, { {0x4f, 0}, {0xcf, 0} },
    { {0x50, 0}, {0xd0, 0} }, { {0x51, 0}, {0xd1, 0} },
    { {0x52, 0}, {0xd2, 0} }, { {0x53, 0}, {0xd3, 0} },
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*054*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*058*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*05c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*060*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*064*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*068*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*06c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*070*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*074*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*078*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*07c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*080*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*084*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*088*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*08c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*090*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*094*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*098*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*09c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0a0*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0a4*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0a8*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0ac*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0b0*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0b4*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0b8*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0bc*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0c0*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0c4*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0c8*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0cc*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0d0*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0d4*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0d8*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0dc*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0e0*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0e4*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0e8*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0ec*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0f0*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0f4*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0f8*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0fc*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*100*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*104*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*108*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*10c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*110*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*114*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*118*/
    { {0x1c, 0}, {0x9c, 0} }, { {0x1d, 0}, {0x9d, 0} },
    { {0},             {0} }, { {0},             {0} },	/*11c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*120*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*124*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*128*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*12c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*130*/
    { {0},             {0} }, { {0x35, 0}, {0xb5, 0} },
    { {0},             {0} }, { {0x37, 0}, {0xb7, 0} },	/*134*/
    { {0x38, 0}, {0xb8, 0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*138*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*13c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*140*/
    { {0},             {0} }, { {0},             {0} },
    { {0x46, 0}, {0xc6, 0} }, { {0x47, 0}, {0xc7, 0} },	/*144*/
    { {0x48, 0}, {0xc8, 0} }, { {0x49, 0}, {0xc9, 0} },
    { {0},             {0} }, { {0x4b, 0}, {0xcb, 0} },	/*148*/
    { {0},             {0} }, { {0x4d, 0}, {0xcd, 0} },
    { {0},             {0} }, { {0x4f, 0}, {0xcf, 0} },	/*14c*/
    { {0x50, 0}, {0xd0, 0} }, { {0x51, 0}, {0xd1, 0} },
    { {0x52, 0}, {0xd2, 0} }, { {0x53, 0}, {0xd3, 0} },	/*150*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*154*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*158*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*15c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*160*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*164*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*168*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*16c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*170*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*174*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*148*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*17c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*180*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*184*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*88*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*18c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*190*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*194*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*198*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*19c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1a0*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1a4*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1a8*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1ac*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1b0*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1b4*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1b8*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1bc*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1c0*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1c4*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1c8*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1cc*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1d0*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1d4*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1d8*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1dc*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1e0*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1e4*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1e8*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1ec*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1f0*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1f4*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1f8*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} }	/*1fc*/
};


static uint8_t	key_queue[16];
static int	key_queue_start = 0,
		key_queue_end = 0;
static int	is_tandy = 0, is_t1x00 = 0,
		is_amstrad = 0;


#ifdef ENABLE_KEYBOARD_XT_LOG
int keyboard_xt_do_log = ENABLE_KEYBOARD_XT_LOG;


static void
kbd_log(const char *fmt, ...)
{
    va_list ap;

    if (keyboard_xt_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define kbd_log(fmt, ...)
#endif

static uint8_t
get_fdd_switch_settings(){
    
    int i, fdd_count = 0;
    
    for (i = 0; i < FDD_NUM; i++) {
                if (fdd_get_flags(i))
                    fdd_count++;
    }

    if (!fdd_count)
        return 0x00;
    else
        return ((fdd_count - 1) << 6) | 0x01;    

}

static uint8_t
get_videomode_switch_settings(){
    
    if (video_is_mda())
        return 0x30;
    else if (video_is_cga())
        return 0x20;	/* 0x10 would be 40x25 */
    else
        return 0x00;    

}

static void
kbd_poll(void *priv)
{
    xtkbd_t *kbd = (xtkbd_t *)priv;

    timer_advance_u64(&kbd->send_delay_timer, 1000 * TIMER_USEC);

    if (!(kbd->pb & 0x40) && (kbd->type != 5))
	return;

    if (kbd->want_irq) {
	kbd->want_irq = 0;
	kbd->pa = kbd->key_waiting;
	kbd->blocked = 1;
	picint(2);
#ifdef ENABLE_KEYBOARD_XT_LOG
	kbd_log("kbd_poll(): keyboard_xt : take IRQ\n");
#endif
    }

    if ((key_queue_start != key_queue_end) && !kbd->blocked) {
	kbd->key_waiting = key_queue[key_queue_start];
	kbd_log("XTkbd: reading %02X from the key queue at %i\n",
		kbd->key_waiting, key_queue_start);
	key_queue_start = (key_queue_start + 1) & 0x0f;
	kbd->want_irq = 1;
    }
}


static void
kbd_adddata(uint16_t val)
{
    /* Test for T1000 'Fn' key (Right Alt / Right Ctrl) */
    if (is_t1x00) {
 	if (keyboard_recv(0xb8) || keyboard_recv(0x9d)) {	/* 'Fn' pressed */
		t1000_syskey(0x00, 0x04, 0x00);	/* Set 'Fn' indicator */
		switch (val) {
			case 0x45: /* Num Lock => toggle numpad */
				t1000_syskey(0x00, 0x00, 0x10); break;
			case 0x47: /* Home => internal display */
				t1000_syskey(0x40, 0x00, 0x00); break;
			case 0x49: /* PgDn => turbo on */
				t1000_syskey(0x80, 0x00, 0x00); break;
			case 0x4D: /* Right => toggle LCD font */
				t1000_syskey(0x00, 0x00, 0x20); break;
			case 0x4F: /* End => external display */
				t1000_syskey(0x00, 0x40, 0x00); break;
			case 0x51: /* PgDn => turbo off */
				t1000_syskey(0x00, 0x80, 0x00); break;
			case 0x54: /* SysRQ => toggle window */
				t1000_syskey(0x00, 0x00, 0x08); break;
		}
	} else
		t1000_syskey(0x04, 0x00, 0x00);	/* Reset 'Fn' indicator */
    }

    key_queue[key_queue_end] = val;
    kbd_log("XTkbd: %02X added to key queue at %i\n",
	    val, key_queue_end);
    key_queue_end = (key_queue_end + 1) & 0x0f;
}


void
kbd_adddata_process(uint16_t val, void (*adddata)(uint16_t val))
{
    uint8_t num_lock = 0, shift_states = 0;

    if (!adddata)
	return;

    keyboard_get_states(NULL, &num_lock, NULL);
    shift_states = keyboard_get_shift() & STATE_LSHIFT;

    if (is_amstrad)
	num_lock = !num_lock;

    /* If NumLock is on, invert the left shift state so we can always check for
       the the same way flag being set (and with NumLock on that then means it
       is actually *NOT* set). */
    if (num_lock)
	shift_states ^= STATE_LSHIFT;

    switch(val) {
	case FAKE_LSHIFT_ON:
		/* If NumLock is on, fake shifts are sent when shift is *NOT* presed,
		   if NumLock is off, fake shifts are sent when shift is pressed. */
		if (shift_states) {
			/* Send fake shift. */
			adddata(num_lock ? 0x2a : 0xaa);
		}
		break;
	case FAKE_LSHIFT_OFF:
		if (shift_states) {
			/* Send fake shift. */
			adddata(num_lock ? 0xaa : 0x2a);
		}
		break;
	default:
		adddata(val);
		break;
    }
}


static void
kbd_adddata_ex(uint16_t val)
{
    kbd_adddata_process(val, kbd_adddata);
}


static void
kbd_write(uint16_t port, uint8_t val, void *priv)
{
    xtkbd_t *kbd = (xtkbd_t *)priv;

    switch (port) {
	case 0x61:
		if (!(kbd->pb & 0x40) && (val & 0x40)) {
			key_queue_start = key_queue_end = 0;
			kbd->want_irq = 0;
			kbd->blocked = 0;
			kbd_adddata(0xaa);
		}
		kbd->pb = val;
		ppi.pb = val;

                timer_process();

		if ((kbd->type <= 1) && (cassette != NULL))
			pc_cas_set_motor(cassette, (kbd->pb & 0x08) == 0);

		speaker_update();

		speaker_gated = val & 1;
		speaker_enable = val & 2;

		if (speaker_enable) 
			was_speaker_enable = 1;
		pit_ctr_set_gate(&pit->counters[2], val & 1);

		if (val & 0x80) {
			kbd->pa = 0;
			kbd->blocked = 0;
			picintc(2);
		}

#ifdef ENABLE_KEYBOARD_XT_LOG
		if (kbd->type <= 1)
			kbd_log("Cassette motor is %s\n", !(val & 0x08) ? "ON" : "OFF");
#endif
		break;
#ifdef ENABLE_KEYBOARD_XT_LOG
	case 0x62:
		if (kbd->type <= 1)
			kbd_log("Cassette IN is %i\n", !!(val & 0x10));
		break;
#endif
    }
}


static uint8_t
kbd_read(uint16_t port, void *priv)
{
    xtkbd_t *kbd = (xtkbd_t *)priv;
    uint8_t ret = 0xff;
    

    switch (port) {
	case 0x60:
		if ((kbd->pb & 0x80) && ((kbd->type <= 3) || (kbd->type == 9))) {
			if (kbd->type <= 1)
				ret = (kbd->pd & ~0x02) | (hasfpu ? 0x02 : 0x00);
			else if ((kbd->type == 2) || (kbd->type == 3))
				ret = 0xff;	/* According to Ruud on the PCem forum, this is supposed to return 0xFF on the XT. */
			else if (kbd->type == 9) {
				/* Zenith Data Systems Z-151
				 * SW1 switch settings:
				 * bits 6-7: floppy drive number
				 * bits 4-5: video mode
				 * bit 2-3: base memory size
				 * bit 1: fpu enable
				 * bit 0: fdc enable
				 */
				ret = get_fdd_switch_settings();

				ret |= get_videomode_switch_settings();

				/* Base memory size should always be 64k */
				ret |= 0x0c;

				if (hasfpu)
					ret |= 0x02;
			}
		} else
			ret = kbd->pa;
		break;

	case 0x61:
		ret = kbd->pb;
		break;

	case 0x62:
		if (kbd->type == 0)
			ret = 0x00;
        else if (kbd->type == 1) {
			if (kbd->pb & 0x04)
				ret = ((mem_size-64) / 32) & 0x0f;
			else
				ret = ((mem_size-64) / 32) >> 4;
		} 
        else if (kbd->type == 8 || kbd->type == 9) {
            /* Olivetti M19 or Zenith Data Systems Z-151 */
            if (kbd->pb & 0x04)
				ret = kbd->pd & 0xbf;
            else 
                ret = kbd->pd >> 4;
        } 
        else {
			if (kbd->pb & 0x08)
				ret = kbd->pd >> 4;
			else {
				/* LaserXT = Always 512k RAM;
				   LaserXT/3 = Bit 0: set = 512k, clear = 256k. */
#if defined(DEV_BRANCH) && defined(USE_LASERXT)
				if (kbd->type == 6)
					ret = ((mem_size == 512) ? 0x0d : 0x0c) | (hasfpu ? 0x02 : 0x00);
				else
#endif
					ret = (kbd->pd & 0x0d) | (hasfpu ? 0x02 : 0x00);
			}
		}
		ret |= (ppispeakon ? 0x20 : 0);

		/* This is needed to avoid error 131 (cassette error).
		   This is serial read: bit 5 = clock, bit 4 = data, cassette header is 256 x 0xff. */
		if (kbd->type <= 1) {
			if (cassette == NULL)
				ret |= (ppispeakon ? 0x10 : 0);
			else
				ret |= (pc_cas_get_inp(cassette) ? 0x10 : 0);
		}

		if (kbd->type == 5)
			ret |= (tandy1k_eeprom_read() ? 0x10 : 0);
		break;

	case 0x63:
		if ((kbd->type == 2) || (kbd->type == 3) || (kbd->type == 4) || (kbd->type == 6))
			ret = kbd->pd;
        
		break;
    }

    return(ret);
}


static void
kbd_reset(void *priv)
{
    xtkbd_t *kbd = (xtkbd_t *)priv;

    kbd->want_irq = 0;
    kbd->blocked = 0;
    kbd->pa = 0x00;
    kbd->pb = 0x00;

    keyboard_scan = 1;

    key_queue_start = 0,
    key_queue_end = 0;
}


void
keyboard_set_is_amstrad(int ams)
{
    is_amstrad = ams;
}


static void *
kbd_init(const device_t *info)
{
    xtkbd_t *kbd;

    kbd = (xtkbd_t *)malloc(sizeof(xtkbd_t));
    memset(kbd, 0x00, sizeof(xtkbd_t));

    io_sethandler(0x0060, 4,
		  kbd_read, NULL, NULL, kbd_write, NULL, NULL, kbd);
    keyboard_send = kbd_adddata_ex;
    kbd_reset(kbd);
    kbd->type = info->local;

    key_queue_start = key_queue_end = 0;

    video_reset(gfxcard);

    if (kbd->type <= 3 || kbd-> type == 8) {
        
        /* DIP switch readout: bit set = OFF, clear = ON. */
        if (kbd->type != 8)
            /* Switches 7, 8 - floppy drives. */
            kbd->pd = get_fdd_switch_settings();
        else 
            /* Olivetti M19
             * Jumpers J1, J2 - monitor type. 
             * 01 - mono (high-res)
             * 10 - color (low-res, disables 640x400x2 mode)
             * 00 - autoswitching
             */
            kbd->pd |= 0x00;
        
        kbd->pd |= get_videomode_switch_settings();
        
        /* Switches 3, 4 - memory size. */
        // Note to Compaq/Toshiba keyboard maintainers: type 4 and 6 will never be activated in this block
        // Should the top if be closed right after setting floppy drive count?
        if ((kbd->type == 3) || (kbd->type == 4) || (kbd->type == 6)) {
            switch (mem_size) {
                case 256:
                    kbd->pd |= 0x00;
                    break;
                case 512:
                    kbd->pd |= 0x04;
                    break;
                case 576:
                    kbd->pd |= 0x08;
                    break;
                case 640:
                default:
                    kbd->pd |= 0x0c;
                    break;
            }
        } else if (kbd->type >= 1) {
            switch (mem_size) {
                case 64:
                    kbd->pd |= 0x00;
                    break;
                case 128:
                    kbd->pd |= 0x04;
                    break;
                case 192:
                    kbd->pd |= 0x08;
                    break;
                case 256:
                default:
                    kbd->pd |= 0x0c;
                    break;
            }
        } else {
            switch (mem_size) {
                case 16:
                    kbd->pd |= 0x00;
                    break;
                case 32:
                    kbd->pd |= 0x04;
                    break;
                case 48:
                    kbd->pd |= 0x08;
                    break;
                case 64:
                default:
                    kbd->pd |= 0x0c;
                    break;
            }
        }

        /* Switch 2 - 8087 FPU. */
        if (hasfpu)
            kbd->pd |= 0x02;

        /* Switch 1 - always off. */
        kbd->pd |= 0x01;
    } else if (kbd-> type == 9) {
        /* Zenith Data Systems Z-151
         * SW2 switch settings:
         * bit 7: monitor frequency
         * bits 5-6: autoboot (00-11 resident monitor, 10 hdd, 01 fdd)
         * bits 0-4: installed memory
         */
        kbd->pd = 0x20;
        switch (mem_size) {
            case 128:
                kbd->pd |= 0x02;
                break;
            case 192:
                kbd->pd |= 0x04;
                break;
            case 256:
                kbd->pd |= 0x02|0x04;
                break;
            case 320:
                kbd->pd |= 0x08;
                break;
            case 384:
                kbd->pd |= 0x02|0x08;
                break;
            case 448:
                kbd->pd |= 0x04|0x08;
                break;
            case 512:
                kbd->pd |= 0x02|0x04|0x08;
                break;
            case 576:
                kbd->pd |= 0x10;
                break;
            case 640:
            default:
                kbd->pd |= 0x02|0x10;
                break;
        }
    }

    timer_add(&kbd->send_delay_timer, kbd_poll, kbd, 1);

    keyboard_set_table(scancode_xt);

    is_tandy = (kbd->type == 5);
    is_t1x00 = (kbd->type == 6);

    is_amstrad = 0;

    return(kbd);
}


static void
kbd_close(void *priv)
{
    xtkbd_t *kbd = (xtkbd_t *)priv;

    /* Stop the timer. */
    timer_disable(&kbd->send_delay_timer);

    /* Disable scanning. */
    keyboard_scan = 0;

    keyboard_send = NULL;

    io_removehandler(0x0060, 4,
		     kbd_read, NULL, NULL, kbd_write, NULL, NULL, kbd);

    free(kbd);
}


const device_t keyboard_pc_device = {
    "IBM PC Keyboard (1981)",
    0,
    0,
    kbd_init,
    kbd_close,
    kbd_reset,
    { NULL }, NULL, NULL
};

const device_t keyboard_pc82_device = {
    "IBM PC Keyboard (1982)",
    0,
    1,
    kbd_init,
    kbd_close,
    kbd_reset,
    { NULL }, NULL, NULL
};

const device_t keyboard_xt_device = {
    "XT (1982) Keyboard",
    0,
    2,
    kbd_init,
    kbd_close,
    kbd_reset,
    { NULL }, NULL, NULL
};

const device_t keyboard_xt86_device = {
    "XT (1986) Keyboard",
    0,
    3,
    kbd_init,
    kbd_close,
    kbd_reset,
    { NULL }, NULL, NULL
};

const device_t keyboard_xt_compaq_device = {
    "Compaq Portable Keyboard",
    0,
    4,
    kbd_init,
    kbd_close,
    kbd_reset,
    { NULL }, NULL, NULL
};

const device_t keyboard_tandy_device = {
    "Tandy 1000 Keyboard",
    0,
    5,
    kbd_init,
    kbd_close,
    kbd_reset,
    { NULL }, NULL, NULL
};

const device_t keyboard_xt_t1x00_device = {
    "Toshiba T1x00 Keyboard",
    0,
    6,
    kbd_init,
    kbd_close,
    kbd_reset,
    { NULL }, NULL, NULL
};

#if defined(DEV_BRANCH) && defined(USE_LASERXT)
const device_t keyboard_xt_lxt3_device = {
    "VTech Laser XT3 Keyboard",
    0,
    7,
    kbd_init,
    kbd_close,
    kbd_reset,
    { NULL }, NULL, NULL
};
#endif

const device_t keyboard_xt_olivetti_device = {
    "Olivetti XT Keyboard",
    0,
    8,
    kbd_init,
    kbd_close,
    kbd_reset,
    { NULL }, NULL, NULL
};

const device_t keyboard_xt_zenith_device = {
    "Zenith XT Keyboard",
    0,
    9,
    kbd_init,
    kbd_close,
    kbd_reset,
    { NULL }, NULL, NULL
};
