/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Define the various UI functions.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2017-2019 Fred N. van Kempen.
 */
#ifndef EMU_UI_H
# define EMU_UI_H

#ifdef __cplusplus
extern "C" {
#endif


#ifdef USE_WX
# define RENDER_FPS	30			/* default render speed */
#endif

/* Message Box functions. */
#define MBX_INFO	1
#define MBX_ERROR	2
#define MBX_QUESTION	3
#define MBX_QUESTION_YN	4
#define MBX_QUESTION_OK	8
#define MBX_QMARK	0x10
#define MBX_WARNING	0x20
#define MBX_FATAL	0x40
#define MBX_ANSI	0x80
#define MBX_LINKS	0x100
#define MBX_DONTASK	0x200

extern int	ui_msgbox(int flags, void *message);
extern int	ui_msgbox_header(int flags, void *header, void *message);
extern int	ui_msgbox_ex(int flags, void *header, void *message, void *btn1, void *btn2, void *btn3);

extern void	ui_check_menu_item(int id, int checked);

/* Status Bar functions. */
#define SB_ICON_WIDTH	24
#define SB_CASSETTE	0x00
#define SB_CARTRIDGE    0x10
#define SB_FLOPPY       0x20
#define SB_CDROM        0x30
#define SB_ZIP          0x40
#define SB_MO           0x50
#define SB_HDD          0x60
#define SB_NETWORK      0x70
#define SB_SOUND        0x80
#define SB_TEXT         0x90

extern wchar_t  *ui_window_title(wchar_t *s);
extern void	ui_status_update(void);
extern int	ui_sb_find_part(int tag);
extern void	ui_sb_set_ready(int ready);
extern void	ui_sb_update_panes(void);
extern void	ui_sb_update_text(void);
extern void	ui_sb_update_tip(int meaning);
extern void	ui_sb_timer_callback(int pane);
extern void	ui_sb_update_icon(int tag, int val);
extern void	ui_sb_update_icon_state(int tag, int active);
extern void	ui_sb_set_text_w(wchar_t *wstr);
extern void	ui_sb_set_text(char *str);
extern void	ui_sb_bugui(char *str);
extern void	ui_sb_mt32lcd(char *str);

#ifdef __cplusplus
}
#endif

#endif	/*EMU_UI_H*/
