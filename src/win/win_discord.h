/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the Discord integration module.
 *
 * Version:	@(#)win_discord.h	1.0.0	2019/12/05
 *
 * Authors:	David Hrdlička, <hrdlickadavid@outlook.com>
 *
 *		Copyright 2019 David Hrdlička.
 */
#ifndef WIN_DISCORD_H
# define WIN_DISCORD_H

extern int	discord_loaded;

extern int	discord_load();
extern void	discord_init();
extern void	discord_close();
extern void	discord_update_activity(int paused);
extern void	discord_run_callbacks();

#endif