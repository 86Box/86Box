/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#undef BITMAP

#include "ibm.h"
#include "device.h"
#include "video.h"
#include "resource.h"
#include "win.h"
#include "x86_ops.h"
#include "mem.h"
#include "codegen.h"

HWND status_hwnd;
int status_is_open = 0;

extern int sreadlnum, swritelnum, segareads, segawrites, scycles_lost;

extern uint64_t main_time;
static uint64_t status_time;

static BOOL CALLBACK status_dlgproc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
        char device_s[4096];
        switch (message)
        {
                case WM_INITDIALOG:
                status_is_open = 1;
                case WM_USER:
                {
                uint64_t new_time = timer_read();
                uint64_t status_diff = new_time - status_time;
                status_time = new_time;
                sprintf(device_s,
                        "CPU speed : %f MIPS\n"
                        "FPU speed : %f MFLOPS\n\n"

                        "Video throughput (read) : %i bytes/sec\n"
                        "Video throughput (write) : %i bytes/sec\n\n"
                        "Effective clockspeed : %iHz\n\n"
                        "Timer 0 frequency : %fHz\n\n"
                        "CPU time : %f%% (%f%%)\n"

                        "New blocks : %i\nOld blocks : %i\nRecompiled speed : %f MIPS\nAverage size : %f\n"
                        "Flushes : %i\nEvicted : %i\nReused : %i\nRemoved : %i\nReal speed : %f MIPS"
                        ,mips,
                        flops,
                        segareads,
                        segawrites,
                        clockrate - scycles_lost,
                        pit_timer0_freq(),
                        ((double)main_time * 100.0) / status_diff,
                        ((double)main_time * 100.0) / timer_freq

                        , cpu_new_blocks_latched, cpu_recomp_blocks_latched, (double)cpu_recomp_ins_latched / 1000000.0, (double)cpu_recomp_ins_latched/cpu_recomp_blocks_latched,
                        cpu_recomp_flushes_latched, cpu_recomp_evicted_latched,
                        cpu_recomp_reuse_latched, cpu_recomp_removed_latched,
                        
                        ((double)cpu_recomp_ins_latched / 1000000.0) / ((double)main_time / timer_freq)
                );
                main_time = 0;
                SendDlgItemMessage(hdlg, IDC_STEXT_DEVICE, WM_SETTEXT, (WPARAM)NULL, (LPARAM)device_s);

                device_s[0] = 0;
                device_add_status_info(device_s, 4096);
                SendDlgItemMessage(hdlg, IDC_STEXT1, WM_SETTEXT, (WPARAM)NULL, (LPARAM)device_s);
                }
                return TRUE;
                
                case WM_COMMAND:
                switch (LOWORD(wParam))
                {
                        case IDOK:
                        case IDCANCEL:
                        status_is_open = 0;
                        EndDialog(hdlg, 0);
                        return TRUE;
                }
                break;
        }

        return FALSE;
}

void status_open(HWND hwnd)
{
        status_hwnd = CreateDialog(hinstance, TEXT("StatusDlg"), hwnd, status_dlgproc);
        ShowWindow(status_hwnd, SW_SHOW);
}


#if 0
void
set_bugui(char *str)
{
    if (str == NULL)
	str = "L:R GGGGGGGG-RRRRRRRR";
    SendMessage(status_hwnd, SB_SETTEXT, 2, (WPARAM)str);
}
#endif
