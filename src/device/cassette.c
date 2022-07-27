/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/arch/ibmpc/cassette.c                                    *
 * Created:     2008-11-25 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2008-2019 Hampa Hug <hampa@hampa.ch>                     *
 *****************************************************************************/

/*****************************************************************************
 * This program is free software. You can redistribute it and / or modify it *
 * under the terms of the GNU General Public License version 2 as  published *
 * by the Free Software Foundation.                                          *
 *                                                                           *
 * This program is distributed in the hope  that  it  will  be  useful,  but *
 * WITHOUT  ANY   WARRANTY,   without   even   the   implied   warranty   of *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU  General *
 * Public License for more details.                                          *
 *****************************************************************************/


#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <wchar.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/cassette.h>

// #include <lib/console.h>


#define CAS_CLK 1193182


pc_cassette_t *	cassette;

char		cassette_fname[512];
char		cassette_mode[512];
unsigned long	cassette_pos, cassette_srate;
int		cassette_enable;
int		cassette_append, cassette_pcm;
int		cassette_ui_writeprot;


static int	cassette_cycles = -1;


static void pc_cas_reset (pc_cassette_t *cas);


#ifdef ENABLE_CASSETTE_LOG
int cassette_do_log = ENABLE_CASSETTE_LOG;


static void
cassette_log(const char *fmt, ...)
{
   va_list ap;

   if (cassette_do_log)
   {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
   }
}
#else
#define cassette_log(fmt, ...)
#endif


void pc_cas_init (pc_cassette_t *cas)
{
	cas->save = 0;
	cas->pcm = 0;

	cas->motor = 0;
	ui_sb_update_icon(SB_CASSETTE, 0);

	cas->position = 0;

	cas->position_save = 0;
	cas->position_load = 0;

	cas->data_out = 0;
	cas->data_inp = 0;

	cas->pcm_out_vol = 64;
	cas->pcm_out_val = 0;

	cas->cas_out_cnt = 0;
	cas->cas_out_buf = 0;

	cas->cas_inp_cnt = 0;
	cas->cas_inp_buf = 0;
	cas->cas_inp_bit = 0;

	cas->clk = 0;

	cas->clk_pcm = 0;

	cas->clk_out = 0;
	cas->clk_inp = 0;

	cas->srate = 44100;

	cas->close = 0;
	cas->fname = NULL;
	cas->fp = NULL;

	pc_cas_reset (cas);
}

void pc_cas_free (pc_cassette_t *cas)
{
	free (cas->fname);

	if (cas->close) {
		fclose (cas->fp);
	}
}

pc_cassette_t *pc_cas_new (void)
{
	pc_cassette_t *cas;

	cas = malloc (sizeof (pc_cassette_t));

	if (cas == NULL) {
		return (NULL);
	}

	pc_cas_init (cas);

	return (cas);
}

void pc_cas_del (pc_cassette_t *cas)
{
	if (cas != NULL) {
		pc_cas_free (cas);
		free (cas);
	}
}

int pc_cas_set_fname (pc_cassette_t *cas, const char *fname)
{
	unsigned	n;
	const char *	ext;

	if (cas->close)
		fclose (cas->fp);

	cas->close = 0;
	cas->fp = NULL;

	free (cas->fname);
	cas->fname = NULL;

	cas->position = 0;

	cas->position_save = 0;
	cas->position_load = 0;

	if (fname == NULL) {
		ui_sb_update_icon_state(SB_CASSETTE, 1);
		return (0);
	}

	cas->fp = plat_fopen (fname, "r+b");

	if (cas->fp == NULL)
		cas->fp = plat_fopen (fname, "w+b");

	if (cas->fp == NULL) {
		ui_sb_update_icon_state(SB_CASSETTE, 1);
		return (1);
	}

	cas->close = 1;

	pc_cas_append (cas);

	cas->position_save = cas->position;

	if (cas->save == 0)
		pc_cas_set_position (cas, 0);

	n = strlen (fname);

	cas->fname = malloc ((n + 1) * sizeof(char));

	if (cas->fname != NULL)
		memcpy (cas->fname, fname, (n + 1) * sizeof(char));

	if (n > 4) {
		ext = fname + (n - 4);

		/* Has to be 44.1 kHz, mono, 8-bit. */
		if (stricmp (ext, ".pcm") == 0)
			pc_cas_set_pcm (cas, 1);
		else if (stricmp (ext, ".raw") == 0)
			pc_cas_set_pcm (cas, 1);
		else if (stricmp (ext, ".wav") == 0)
			pc_cas_set_pcm (cas, 1);
		else if (stricmp (ext, ".cas") == 0)
			pc_cas_set_pcm (cas, 0);
	}

	return (0);
}

static
void pc_cas_reset (pc_cassette_t *cas)
{
	unsigned i;

	cas->clk_pcm = 0;

	cas->clk_out = cas->clk;
	cas->clk_inp = 0;

	cas->pcm_out_val = 0;

	cas->cas_out_cnt = 0;
	cas->cas_out_buf = 0;

	cas->cas_inp_cnt = 0;
	cas->cas_inp_buf = 0;
	cas->cas_inp_bit = 0;

	for (i = 0; i < 3; i++) {
		cas->pcm_inp_fir[i] = 0;
	}
}

int pc_cas_get_mode (const pc_cassette_t *cas)
{
	return (cas->save);
}

void pc_cas_set_mode (pc_cassette_t *cas, int save)
{
	save = (save != 0);

	if (cas->save == save) {
		return;
	}

	if (cas->save) {
		cas->position_save = cas->position;
		cas->position = cas->position_load;
	}
	else {
		cas->position_load = cas->position;
		cas->position = cas->position_save;
	}

	cas->save = save;

	memset(cassette_mode, 0x00, sizeof(cassette_mode));
	if (save)
		memcpy(cassette_mode, "save", strlen("save") + 1);
	else
		memcpy(cassette_mode, "load", strlen("load") + 1);

	if (cas->fp != NULL) {
		fflush (cas->fp);

		pc_cas_set_position (cas, cas->position);
	}

	pc_cas_reset (cas);
}

int pc_cas_get_pcm (const pc_cassette_t *cas)
{
	return (cas->pcm);
}

void pc_cas_set_pcm (pc_cassette_t *cas, int pcm)
{
	cas->pcm = (pcm != 0);

	cassette_pcm = (pcm != 0);

	pc_cas_reset (cas);
}

unsigned long pc_cas_get_srate (const pc_cassette_t *cas)
{
	return (cas->srate);
}

void pc_cas_set_srate (pc_cassette_t *cas, unsigned long srate)
{
	cas->srate = srate;

	pc_cas_reset (cas);
}

void pc_cas_rewind (pc_cassette_t *cas)
{
	if (cas->fp != NULL) {
		rewind (cas->fp);
		cas->position = 0;
	}

	pc_cas_reset (cas);
}

void pc_cas_append (pc_cassette_t *cas)
{
	if (cas->fp != NULL) {
		fseek (cas->fp, 0, SEEK_END);
		cas->position = ftell (cas->fp);
	}

	pc_cas_reset (cas);
}

unsigned long pc_cas_get_position (const pc_cassette_t *cas)
{
	return (cas->position);
}

int pc_cas_set_position (pc_cassette_t *cas, unsigned long pos)
{
	if (cas->fp == NULL) {
		return (1);
	}

	if (fseek (cas->fp, pos, SEEK_SET) != 0) {
		return (1);
	}

	cas->position = pos;

	pc_cas_reset (cas);

	return (0);
}

static
void pc_cas_read_bit (pc_cassette_t *cas)
{
	int val;

	if (cas->cas_inp_cnt == 0) {
		if (cas->fp == NULL) {
			return;
		}

		if (feof (cas->fp)) {
			return;
		}

		val = fgetc (cas->fp);

		if (val == EOF) {
			cassette_log ("cassette EOF at %lu\n", cas->position);
			return;
		}

		cas->position += 1;

		cas->cas_inp_cnt = 8;
		cas->cas_inp_buf = val;
	}

	cas->cas_inp_bit = ((cas->cas_inp_buf & 0x80) != 0);

	cas->cas_inp_buf = (cas->cas_inp_buf << 1) & 0xff;
	cas->cas_inp_cnt -= 1;
}

static
int pc_cas_read_smp (pc_cassette_t *cas)
{
	int smp, *fir;

	if (feof (cas->fp)) {
		return (0);
	}

	smp = fgetc (cas->fp);

	if (smp == EOF) {
		cassette_log ("cassette EOF at %lu\n", cas->position);
		return (0);
	}

	cas->position += 1;

	fir = cas->pcm_inp_fir;

	fir[0] = fir[1];
	fir[1] = fir[2];
	fir[2] = (smp & 0x80) ? (smp - 256) : smp;

	smp = (fir[0] + 2 * fir[1] + fir[2]) / 4;

	return (smp);
}

static
void pc_cas_write_bit (pc_cassette_t *cas, unsigned char val)
{
	if (val && !cassette_ui_writeprot) {
		cas->cas_out_buf |= (0x80 >> cas->cas_out_cnt);
	}

	cas->cas_out_cnt += 1;

	if (cas->cas_out_cnt >= 8) {
		if (cas->fp != NULL) {
			if (!cassette_ui_writeprot)
				fputc (cas->cas_out_buf, cas->fp);
			cas->position += 1;
		}

		cas->cas_out_buf = 0;
		cas->cas_out_cnt = 0;
	}
}

static
void pc_cas_write_smp (pc_cassette_t *cas, int val)
{
	unsigned char smp;

	if (val < 0) {
		smp = (val < -127) ? 0x80 : (val + 256);
	}
	else {
		smp = (val > 127) ? 0x7f : val;
	}

	if (!cassette_ui_writeprot)
		fputc (smp, cas->fp);

	cas->position += 1;
}

void pc_cas_set_motor (pc_cassette_t *cas, unsigned char val)
{
	unsigned i;

	val = (val != 0);

	if (val == cas->motor) {
		return;
	}

	if ((val == 0) && cas->save && cas->pcm) {
		for (i = 0; i < (cas->srate / 16); i++) {
			pc_cas_write_smp (cas, 0);
		}
	}

	cassette_log ("cassette %S at %lu motor %s\n", (cas->fname != NULL) ? cas->fname : "<none>", cas->position, val ? "on" : "off");

	cas->motor = val;

	if (cas->fp != NULL) {
		fflush (cas->fp);

		pc_cas_set_position (cas, cas->position);
	}

	pc_cas_reset (cas);

	if (cas->motor)
		timer_set_delay_u64(&cas->timer, 8ULL * PITCONST);
	else
		timer_disable(&cas->timer);

	ui_sb_update_icon(SB_CASSETTE, !!val);
}

unsigned char pc_cas_get_inp (const pc_cassette_t *cas)
{
	return (cas->data_inp);
}

void pc_cas_set_out (pc_cassette_t *cas, unsigned char val)
{
	unsigned long clk;

	val = (val != 0);

	if (cas->motor == 0) {
		cas->data_inp = val;
		return;
	}

	if (cas->data_out == val) {
		return;
	}

	cas->data_out = val;

	if (cas->pcm) {
		cas->pcm_out_val = val ? -cas->pcm_out_vol : cas->pcm_out_vol;
		return;
	}

	if (cas->save == 0) {
		return;
	}

	if (val == 0) {
		return;
	}

	clk = cas->clk - cas->clk_out;
	cas->clk_out = cas->clk;

	if (clk < (CAS_CLK / 4000)) {
		;
	}
	else if (clk < ((3 * CAS_CLK) / 4000)) {
		pc_cas_write_bit (cas, 0);
	}
	else if (clk < ((5 * CAS_CLK) / 4000)) {
		pc_cas_write_bit (cas, 1);
	}
}

void pc_cas_print_state (const pc_cassette_t *cas)
{
	cassette_log ("%s %s %lu %s %lu\n", (cas->fname != NULL) ? cas->fname : "<none>", cas->pcm ? "pcm" : "cas", cas->srate, cas->save ? "save" : "load", cas->position);
}

static
void pc_cas_clock_pcm (pc_cassette_t *cas, unsigned long cnt)
{
	unsigned long i, n;
	int           v = 0;

	n = cas->srate * cnt + cas->clk_pcm;

	cas->clk_pcm = n % CAS_CLK;

	n = n / CAS_CLK;

	if (n == 0) {
		return;
	}

	if (cas->save) {
		for (i = 0; i < n; i++) {
			pc_cas_write_smp (cas, cas->pcm_out_val);
		}
	}
	else {
		for (i = 0; i < n; i++) {
			v = pc_cas_read_smp (cas);
		}

		cas->data_inp = (v < 0) ? 0 : 1;
	}
}

void pc_cas_clock (pc_cassette_t *cas, unsigned long cnt)
{
	cas->clk += cnt;

	if (cas->motor == 0) {
		return;
	}

	if (cas->pcm) {
		pc_cas_clock_pcm (cas, cnt);
		return;
	}

	if (cas->save) {
		return;
	}

	if (cas->clk_inp > cnt) {
		cas->clk_inp -= cnt;
		return;
	}

	cnt -= cas->clk_inp;

	cas->data_inp = !cas->data_inp;

	if (cas->data_inp) {
		pc_cas_read_bit (cas);
	}

	if (cas->cas_inp_bit) {
		cas->clk_inp = CAS_CLK / 2000;
	}
	else {
		cas->clk_inp = CAS_CLK / 4000;
	}

	if (cas->clk_inp > cnt) {
		cas->clk_inp -= cnt;
	}
}


void pc_cas_advance (pc_cassette_t *cas)
{
    int ticks;
    cpu_s = (CPU *) &cpu_f->cpus[cpu_effective];

    if (cas->motor == 0)
	return;

    if (cassette_cycles == -1)
	cassette_cycles = cycles;
    if (cycles <= cassette_cycles)
	ticks = (cassette_cycles - cycles);
    else
	ticks = (cassette_cycles + (cpu_s->rspeed / 100) - cycles);
    cassette_cycles = cycles;

    pc_cas_clock(cas, ticks);
}


static void
cassette_close(void *p)
{
    if (cassette != NULL) {
	free(cassette);
	cassette = NULL;
    }
}


static void
cassette_callback(void *p)
{
    pc_cassette_t *cas = (pc_cassette_t *) p;

    pc_cas_clock (cas, 8);

    if (cas->motor)
	ui_sb_update_icon(SB_CASSETTE, 1);

    timer_advance_u64(&cas->timer, 8ULL * PITCONST);
}


static void *
cassette_init(const device_t *info)
{
	cassette = NULL;

	if (cassette_pcm == 1)
		cassette_pcm = -1;

	cassette_log("CASSETTE: file=%s mode=%s pcm=%d srate=%lu pos=%lu append=%d\n",
		     (cassette_fname != NULL) ? cassette_fname : "<none>", cassette_mode, cassette_pcm, cassette_srate, cassette_pos, cassette_append);

	cassette = pc_cas_new();

	if (cassette == NULL) {
		cassette_log("ERROR: *** alloc failed\n");
		return NULL;
	}

	if (strlen(cassette_fname) == 0) {
		if (pc_cas_set_fname (cassette, NULL)) {
			cassette_log("ERROR: *** opening file failed (%s)\n", cassette_fname);
		}
	} else {
		if (pc_cas_set_fname (cassette, cassette_fname)) {
			cassette_log("ERROR: *** opening file failed (%s)\n", cassette_fname);
		}
	}

	if (strcmp (cassette_mode, "load") == 0)
		pc_cas_set_mode (cassette, 0);
	else if (strcmp (cassette_mode, "save") == 0)
		pc_cas_set_mode (cassette, 1);
	else {
		cassette_log ("ERROR: *** unknown cassette mode (%s)\n", cassette_mode);
	}

	if (cassette_append)
		pc_cas_append (cassette);
	else
		pc_cas_set_position (cassette, cassette_pos);

	if (cassette_pcm >= 0)
		pc_cas_set_pcm (cassette, cassette_pcm);

	pc_cas_set_srate (cassette, cassette_srate);

	timer_add(&cassette->timer, cassette_callback, cassette, 0);

	return cassette;
}


const device_t cassette_device = {
    .name = "IBM PC/PCjr Cassette Device",
    .internal_name = "cassette",
    .flags = 0,
    .local = 0,
    .init = cassette_init,
    .close = cassette_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
