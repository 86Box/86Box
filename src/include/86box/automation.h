/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the automation control socket.
 *
 *          The automation socket lets an external program (test harness,
 *          scripting bridge, AI agent) hold the emulated machine, advance
 *          it by an exact amount of emulated time, inject input, and read
 *          back the screen and memory. See doc/specifications/automation.md.
 *
 * Authors: Justin Akers, <5621217+dafugg@users.noreply.github.com>
 *
 *          Copyright 2026 Justin Akers.
 */
#ifndef EMU_AUTOMATION_H
#define EMU_AUTOMATION_H

/* Values returned by automation_run_state(). */
#define AUTOMATION_FREE 0 /* no opinion: run with normal real-time pacing */
#define AUTOMATION_HOLD 1 /* do not run the CPU */
#define AUTOMATION_RUN  2 /* run the CPU now, without real-time pacing */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef USE_AUTOMATION
extern char automation_path[1024];       /* unix:<path>, tcp:[host:]port, or bare (see doc) */
extern char automation_token_file[1024]; /* TCP: where the per-run token is written */
extern int  automation_start_held;
extern int  automation_allow_remote; /* TCP: permit a non-loopback bind address */

extern void automation_init(void);
extern void automation_close(void);
/* Called by the main loop on the CPU thread, once per scheduler pass. */
extern void automation_poll(void);
extern int  automation_run_state(void);
/* Called by the main loop on the CPU thread after each pc_run(). */
extern void automation_quantum(void);
/* Called by the video core whenever a frame is handed to the renderer. */
extern void automation_frame(int x, int y, int w, int h, int monitor_index);
/* Called by the CPU core on every reset (hard = 1 for power-on/hard reset). */
extern void automation_reset(int hard);
#else
#    define automation_init()
#    define automation_close()
#    define automation_poll()
#    define automation_run_state() AUTOMATION_FREE
#    define automation_quantum()
#    define automation_frame(x, y, w, h, monitor_index)
#    define automation_reset(hard)
#endif

#ifdef __cplusplus
}
#endif

#endif /*EMU_AUTOMATION_H*/
