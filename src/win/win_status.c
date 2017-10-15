/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#undef BITMAP
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../ibm.h"
#include "../mem.h"
#include "../cpu/x86_ops.h"
#ifdef USE_DYNAREC
#include "../cpu/codegen.h"
#endif
#include "../device.h"
#include "win.h"



HWND	hwndStatus = NULL;


extern int sreadlnum, swritelnum, segareads, segawrites, scycles_lost;
extern uint64_t main_time;
static uint64_t status_time;


static BOOL CALLBACK
StatusWindowProcedure(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    char temp[4096];
    uint64_t new_time;
    uint64_t status_diff;

    switch (message) {
	case WM_INITDIALOG:
		hwndStatus = hdlg;
		/*FALLTHROUGH*/

	case WM_USER:
		new_time = timer_read();
		status_diff = new_time - status_time;
		status_time = new_time;
		sprintf(temp,
			"CPU speed : %f MIPS\n"
			"FPU speed : %f MFLOPS\n\n"

			"Video throughput (read) : %i bytes/sec\n"
			"Video throughput (write) : %i bytes/sec\n\n"
			"Effective clockspeed : %iHz\n\n"
			"Timer 0 frequency : %fHz\n\n"
			"CPU time : %f%% (%f%%)\n"

#ifdef USE_DYNAREC
			"New blocks : %i\nOld blocks : %i\nRecompiled speed : %f MIPS\nAverage size : %f\n"
			"Flushes : %i\nEvicted : %i\nReused : %i\nRemoved : %i"
#endif
			,mips,
			flops,
			segareads,
			segawrites,
			clockrate - scycles_lost,
			pit_timer0_freq(),
			((double)main_time * 100.0) / status_diff,
                        ((double)main_time * 100.0) / timer_freq

#ifdef USE_DYNAREC
			, cpu_new_blocks_latched, cpu_recomp_blocks_latched, (double)cpu_recomp_ins_latched / 1000000.0, (double)cpu_recomp_ins_latched/cpu_recomp_blocks_latched,
			cpu_recomp_flushes_latched, cpu_recomp_evicted_latched,
			cpu_recomp_reuse_latched, cpu_recomp_removed_latched
#endif
		);
		main_time = 0;
		SendDlgItemMessage(hdlg, IDT_SDEVICE, WM_SETTEXT,
				   (WPARAM)NULL, (LPARAM)temp);

		temp[0] = 0;
		device_add_status_info(temp, 4096);
		SendDlgItemMessage(hdlg, IDT_STEXT, WM_SETTEXT,
				   (WPARAM)NULL, (LPARAM)temp);
		return(TRUE);
		
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
			case IDOK:
			case IDCANCEL:
				hwndStatus = NULL;
				EndDialog(hdlg, 0);
				return(TRUE);
		}
		break;
    }

    return(FALSE);
}


void
StatusWindowCreate(HWND hwndParent)
{
    HWND hwnd;

    hwnd = CreateDialog(hinstance, (LPCSTR)DLG_STATUS,
			hwndParent, StatusWindowProcedure);
    ShowWindow(hwnd, SW_SHOW);
}
