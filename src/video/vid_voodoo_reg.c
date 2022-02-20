/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		3DFX Voodoo emulation.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *
 *		Copyright 2008-2020 Sarah Walker.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <wchar.h>
#include <math.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_voodoo_common.h>
#include <86box/vid_voodoo_banshee.h>
#include <86box/vid_voodoo_blitter.h>
#include <86box/vid_voodoo_dither.h>
#include <86box/vid_voodoo_fifo.h>
#include <86box/vid_voodoo_regs.h>
#include <86box/vid_voodoo_render.h>
#include <86box/vid_voodoo_setup.h>
#include <86box/vid_voodoo_texture.h>


enum
{
        CHIP_FBI = 0x1,
        CHIP_TREX0 = 0x2,
        CHIP_TREX1 = 0x4,
        CHIP_TREX2 = 0x8
};


#ifdef ENABLE_VOODOO_REG_LOG
int voodoo_reg_do_log = ENABLE_VOODOO_REG_LOG;

static void
voodoo_reg_log(const char *fmt, ...)
{
    va_list ap;

    if (voodoo_reg_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define voodoo_reg_log(fmt, ...)
#endif


void voodoo_reg_writel(uint32_t addr, uint32_t val, void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;
        union
        {
                uint32_t i;
                float f;
        } tempif;
        int ad21 = addr & (1 << 21);
        int chip = (addr >> 10) & 0xf;
        if (!chip)
                chip = 0xf;

        tempif.i = val;
//voodoo_reg_log("voodoo_reg_write_l: addr=%08x val=%08x(%f) chip=%x\n", addr, val, tempif.f, chip);
        addr &= 0x3fc;

        if ((voodoo->fbiInit3 & FBIINIT3_REMAP) && addr < 0x100 && ad21)
                addr |= 0x400;
        switch (addr)
        {
                case SST_swapbufferCMD:
                if (voodoo->type >= VOODOO_BANSHEE)
                {
//                        voodoo_reg_log("swapbufferCMD %08x %08x\n", val, voodoo->leftOverlayBuf);

                        voodoo_wait_for_render_thread_idle(voodoo);
                        if (!(val & 1))
                        {
                                banshee_set_overlay_addr(voodoo->p, voodoo->leftOverlayBuf);
                                thread_wait_mutex(voodoo->swap_mutex);
                                if (voodoo->swap_count > 0)
                                        voodoo->swap_count--;
                                thread_release_mutex(voodoo->swap_mutex);
                                voodoo->frame_count++;
                        }
                        else if (TRIPLE_BUFFER)
                        {
                                if (voodoo->swap_pending)
                                        voodoo_wait_for_swap_complete(voodoo);
                                voodoo->swap_interval = (val >> 1) & 0xff;
                                voodoo->swap_offset = voodoo->leftOverlayBuf;
                                voodoo->swap_pending = 1;
                        }
                        else
                        {
                                voodoo->swap_interval = (val >> 1) & 0xff;
                                voodoo->swap_offset = voodoo->leftOverlayBuf;
                                voodoo->swap_pending = 1;

                                voodoo_wait_for_swap_complete(voodoo);
                        }

                        voodoo->cmd_read++;
                        break;
                }

                if (TRIPLE_BUFFER)
                {
                        voodoo->disp_buffer = (voodoo->disp_buffer + 1) % 3;
                        voodoo->draw_buffer = (voodoo->draw_buffer + 1) % 3;
                }
                else
                {
                        voodoo->disp_buffer = !voodoo->disp_buffer;
                        voodoo->draw_buffer = !voodoo->draw_buffer;
                }
                voodoo_recalc(voodoo);

                voodoo->params.swapbufferCMD = val;

//                voodoo_reg_log("Swap buffer %08x %d %p %i\n", val, voodoo->swap_count, &voodoo->swap_count, (voodoo == voodoo->set->voodoos[1]) ? 1 : 0);
//                voodoo->front_offset = params->front_offset;
                voodoo_wait_for_render_thread_idle(voodoo);
                if (!(val & 1))
                {
                        memset(voodoo->dirty_line, 1, sizeof(voodoo->dirty_line));
                        voodoo->front_offset = voodoo->params.front_offset;
                        thread_wait_mutex(voodoo->swap_mutex);
                        if (voodoo->swap_count > 0)
                                voodoo->swap_count--;
                        thread_release_mutex(voodoo->swap_mutex);
                }
                else if (TRIPLE_BUFFER)
                {
                        if (voodoo->swap_pending)
                                voodoo_wait_for_swap_complete(voodoo);

                        voodoo->swap_interval = (val >> 1) & 0xff;
                        voodoo->swap_offset = voodoo->params.front_offset;
                        voodoo->swap_pending = 1;
                }
                else
                {
                        voodoo->swap_interval = (val >> 1) & 0xff;
                        voodoo->swap_offset = voodoo->params.front_offset;
                        voodoo->swap_pending = 1;

                        voodoo_wait_for_swap_complete(voodoo);
                }
                voodoo->cmd_read++;
                break;

                case SST_vertexAx: case SST_remap_vertexAx:
                voodoo->params.vertexAx = val & 0xffff;
                break;
                case SST_vertexAy: case SST_remap_vertexAy:
                voodoo->params.vertexAy = val & 0xffff;
                break;
                case SST_vertexBx: case SST_remap_vertexBx:
                voodoo->params.vertexBx = val & 0xffff;
                break;
                case SST_vertexBy: case SST_remap_vertexBy:
                voodoo->params.vertexBy = val & 0xffff;
                break;
                case SST_vertexCx: case SST_remap_vertexCx:
                voodoo->params.vertexCx = val & 0xffff;
                break;
                case SST_vertexCy: case SST_remap_vertexCy:
                voodoo->params.vertexCy = val & 0xffff;
                break;

                case SST_startR: case SST_remap_startR:
                voodoo->params.startR = val & 0xffffff;
                break;
                case SST_startG: case SST_remap_startG:
                voodoo->params.startG = val & 0xffffff;
                break;
                case SST_startB: case SST_remap_startB:
                voodoo->params.startB = val & 0xffffff;
                break;
                case SST_startZ: case SST_remap_startZ:
                voodoo->params.startZ = val;
                break;
                case SST_startA: case SST_remap_startA:
                voodoo->params.startA = val & 0xffffff;
                break;
                case SST_startS: case SST_remap_startS:
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].startS = ((int64_t)(int32_t)val) << 14;
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].startS = ((int64_t)(int32_t)val) << 14;
                break;
                case SST_startT: case SST_remap_startT:
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].startT = ((int64_t)(int32_t)val) << 14;
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].startT = ((int64_t)(int32_t)val) << 14;
                break;
                case SST_startW: case SST_remap_startW:
                if (chip & CHIP_FBI)
                        voodoo->params.startW = (int64_t)(int32_t)val << 2;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].startW = (int64_t)(int32_t)val << 2;
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].startW = (int64_t)(int32_t)val << 2;
                break;

                case SST_dRdX: case SST_remap_dRdX:
                voodoo->params.dRdX = (val & 0xffffff) | ((val & 0x800000) ? 0xff000000 : 0);
                break;
                case SST_dGdX: case SST_remap_dGdX:
                voodoo->params.dGdX = (val & 0xffffff) | ((val & 0x800000) ? 0xff000000 : 0);
                break;
                case SST_dBdX: case SST_remap_dBdX:
                voodoo->params.dBdX = (val & 0xffffff) | ((val & 0x800000) ? 0xff000000 : 0);
                break;
                case SST_dZdX: case SST_remap_dZdX:
                voodoo->params.dZdX = val;
                break;
                case SST_dAdX: case SST_remap_dAdX:
                voodoo->params.dAdX = (val & 0xffffff) | ((val & 0x800000) ? 0xff000000 : 0);
                break;
                case SST_dSdX: case SST_remap_dSdX:
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dSdX = ((int64_t)(int32_t)val) << 14;
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].dSdX = ((int64_t)(int32_t)val) << 14;
                break;
                case SST_dTdX: case SST_remap_dTdX:
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dTdX = ((int64_t)(int32_t)val) << 14;
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].dTdX = ((int64_t)(int32_t)val) << 14;
                break;
                case SST_dWdX: case SST_remap_dWdX:
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dWdX = (int64_t)(int32_t)val << 2;
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].dWdX = (int64_t)(int32_t)val << 2;
                if (chip & CHIP_FBI)
                        voodoo->params.dWdX = (int64_t)(int32_t)val << 2;
                break;

                case SST_dRdY: case SST_remap_dRdY:
                voodoo->params.dRdY = (val & 0xffffff) | ((val & 0x800000) ? 0xff000000 : 0);
                break;
                case SST_dGdY: case SST_remap_dGdY:
                voodoo->params.dGdY = (val & 0xffffff) | ((val & 0x800000) ? 0xff000000 : 0);
                break;
                case SST_dBdY: case SST_remap_dBdY:
                voodoo->params.dBdY = (val & 0xffffff) | ((val & 0x800000) ? 0xff000000 : 0);
                break;
                case SST_dZdY: case SST_remap_dZdY:
                voodoo->params.dZdY = val;
                break;
                case SST_dAdY: case SST_remap_dAdY:
                voodoo->params.dAdY = (val & 0xffffff) | ((val & 0x800000) ? 0xff000000 : 0);
                break;
                case SST_dSdY: case SST_remap_dSdY:
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dSdY = ((int64_t)(int32_t)val) << 14;
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].dSdY = ((int64_t)(int32_t)val) << 14;
                break;
                case SST_dTdY: case SST_remap_dTdY:
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dTdY = ((int64_t)(int32_t)val) << 14;
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].dTdY = ((int64_t)(int32_t)val) << 14;
                break;
                case SST_dWdY: case SST_remap_dWdY:
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dWdY = (int64_t)(int32_t)val << 2;
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].dWdY = (int64_t)(int32_t)val << 2;
                if (chip & CHIP_FBI)
                        voodoo->params.dWdY = (int64_t)(int32_t)val << 2;
                break;

                case SST_triangleCMD: case SST_remap_triangleCMD:
                voodoo->params.sign = val & (1 << 31);

                if (voodoo->ncc_dirty[0])
                        voodoo_update_ncc(voodoo, 0);
                if (voodoo->ncc_dirty[1])
                        voodoo_update_ncc(voodoo, 1);
                voodoo->ncc_dirty[0] = voodoo->ncc_dirty[1] = 0;

                voodoo_queue_triangle(voodoo, &voodoo->params);

                voodoo->cmd_read++;
                break;

                case SST_fvertexAx: case SST_remap_fvertexAx:
                voodoo->fvertexAx.i = val;
                voodoo->params.vertexAx = (int32_t)(int16_t)(int32_t)(voodoo->fvertexAx.f * 16.0f) & 0xffff;
                break;
                case SST_fvertexAy: case SST_remap_fvertexAy:
                voodoo->fvertexAy.i = val;
                voodoo->params.vertexAy = (int32_t)(int16_t)(int32_t)(voodoo->fvertexAy.f * 16.0f) & 0xffff;
                break;
                case SST_fvertexBx: case SST_remap_fvertexBx:
                voodoo->fvertexBx.i = val;
                voodoo->params.vertexBx = (int32_t)(int16_t)(int32_t)(voodoo->fvertexBx.f * 16.0f) & 0xffff;
                break;
                case SST_fvertexBy: case SST_remap_fvertexBy:
                voodoo->fvertexBy.i = val;
                voodoo->params.vertexBy = (int32_t)(int16_t)(int32_t)(voodoo->fvertexBy.f * 16.0f) & 0xffff;
                break;
                case SST_fvertexCx: case SST_remap_fvertexCx:
                voodoo->fvertexCx.i = val;
                voodoo->params.vertexCx = (int32_t)(int16_t)(int32_t)(voodoo->fvertexCx.f * 16.0f) & 0xffff;
                break;
                case SST_fvertexCy: case SST_remap_fvertexCy:
                voodoo->fvertexCy.i = val;
                voodoo->params.vertexCy = (int32_t)(int16_t)(int32_t)(voodoo->fvertexCy.f * 16.0f) & 0xffff;
                break;

                case SST_fstartR: case SST_remap_fstartR:
                tempif.i = val;
                voodoo->params.startR = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fstartG: case SST_remap_fstartG:
                tempif.i = val;
                voodoo->params.startG = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fstartB: case SST_remap_fstartB:
                tempif.i = val;
                voodoo->params.startB = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fstartZ: case SST_remap_fstartZ:
                tempif.i = val;
                voodoo->params.startZ = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fstartA: case SST_remap_fstartA:
                tempif.i = val;
                voodoo->params.startA = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fstartS: case SST_remap_fstartS:
                tempif.i = val;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].startS = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].startS = (int64_t)(tempif.f * 4294967296.0f);
                break;
                case SST_fstartT: case SST_remap_fstartT:
                tempif.i = val;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].startT = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].startT = (int64_t)(tempif.f * 4294967296.0f);
                break;
                case SST_fstartW: case SST_remap_fstartW:
                tempif.i = val;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].startW = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].startW = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_FBI)
                        voodoo->params.startW = (int64_t)(tempif.f * 4294967296.0f);
                break;

                case SST_fdRdX: case SST_remap_fdRdX:
                tempif.i = val;
                voodoo->params.dRdX = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fdGdX: case SST_remap_fdGdX:
                tempif.i = val;
                voodoo->params.dGdX = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fdBdX: case SST_remap_fdBdX:
                tempif.i = val;
                voodoo->params.dBdX = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fdZdX: case SST_remap_fdZdX:
                tempif.i = val;
                voodoo->params.dZdX = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fdAdX: case SST_remap_fdAdX:
                tempif.i = val;
                voodoo->params.dAdX = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fdSdX: case SST_remap_fdSdX:
                tempif.i = val;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dSdX = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].dSdX = (int64_t)(tempif.f * 4294967296.0f);
                break;
                case SST_fdTdX: case SST_remap_fdTdX:
                tempif.i = val;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dTdX = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].dTdX = (int64_t)(tempif.f * 4294967296.0f);
                break;
                case SST_fdWdX: case SST_remap_fdWdX:
                tempif.i = val;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dWdX = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].dWdX = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_FBI)
                        voodoo->params.dWdX = (int64_t)(tempif.f * 4294967296.0f);
                break;

                case SST_fdRdY: case SST_remap_fdRdY:
                tempif.i = val;
                voodoo->params.dRdY = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fdGdY: case SST_remap_fdGdY:
                tempif.i = val;
                voodoo->params.dGdY = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fdBdY: case SST_remap_fdBdY:
                tempif.i = val;
                voodoo->params.dBdY = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fdZdY: case SST_remap_fdZdY:
                tempif.i = val;
                voodoo->params.dZdY = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fdAdY: case SST_remap_fdAdY:
                tempif.i = val;
                voodoo->params.dAdY = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fdSdY: case SST_remap_fdSdY:
                tempif.i = val;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dSdY = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].dSdY = (int64_t)(tempif.f * 4294967296.0f);
                break;
                case SST_fdTdY: case SST_remap_fdTdY:
                tempif.i = val;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dTdY = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].dTdY = (int64_t)(tempif.f * 4294967296.0f);
                break;
                case SST_fdWdY: case SST_remap_fdWdY:
                tempif.i = val;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dWdY = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].dWdY = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_FBI)
                        voodoo->params.dWdY = (int64_t)(tempif.f * 4294967296.0f);
                break;

                case SST_ftriangleCMD:
                voodoo->params.sign = val & (1 << 31);

                if (voodoo->ncc_dirty[0])
                        voodoo_update_ncc(voodoo, 0);
                if (voodoo->ncc_dirty[1])
                        voodoo_update_ncc(voodoo, 1);
                voodoo->ncc_dirty[0] = voodoo->ncc_dirty[1] = 0;

                voodoo_queue_triangle(voodoo, &voodoo->params);

                voodoo->cmd_read++;
                break;

                case SST_fbzColorPath:
                voodoo->params.fbzColorPath = val;
                voodoo->rgb_sel = val & 3;
                break;

                case SST_fogMode:
                voodoo->params.fogMode = val;
                break;
                case SST_alphaMode:
                voodoo->params.alphaMode = val;
                break;
                case SST_fbzMode:
                voodoo->params.fbzMode = val;
                voodoo_recalc(voodoo);
                break;
                case SST_lfbMode:
                voodoo->lfbMode = val;
                voodoo_recalc(voodoo);
                break;

                case SST_clipLeftRight:
                if (voodoo->type >= VOODOO_2)
                {
                        voodoo->params.clipRight = val & 0xfff;
                        voodoo->params.clipLeft = (val >> 16) & 0xfff;
                }
                else
                {
                        voodoo->params.clipRight = val & 0x3ff;
                        voodoo->params.clipLeft = (val >> 16) & 0x3ff;
                }
                break;
                case SST_clipLowYHighY:
                if (voodoo->type >= VOODOO_2)
                {
                        voodoo->params.clipHighY = val & 0xfff;
                        voodoo->params.clipLowY = (val >> 16) & 0xfff;
                }
                else
                {
                        voodoo->params.clipHighY = val & 0x3ff;
                        voodoo->params.clipLowY = (val >> 16) & 0x3ff;
                }
                break;

                case SST_nopCMD:
                voodoo->cmd_read++;
                voodoo->fbiPixelsIn = 0;
                voodoo->fbiChromaFail = 0;
                voodoo->fbiZFuncFail = 0;
                voodoo->fbiAFuncFail = 0;
                voodoo->fbiPixelsOut = 0;
                break;
                case SST_fastfillCMD:
                voodoo_wait_for_render_thread_idle(voodoo);
                voodoo_fastfill(voodoo, &voodoo->params);
                voodoo->cmd_read++;
                break;

                case SST_fogColor:
                voodoo->params.fogColor.r = (val >> 16) & 0xff;
                voodoo->params.fogColor.g = (val >> 8) & 0xff;
                voodoo->params.fogColor.b = val & 0xff;
                break;

                case SST_zaColor:
                voodoo->params.zaColor = val;
                break;
                case SST_chromaKey:
                voodoo->params.chromaKey_r = (val >> 16) & 0xff;
                voodoo->params.chromaKey_g = (val >> 8) & 0xff;
                voodoo->params.chromaKey_b = val & 0xff;
                voodoo->params.chromaKey = val & 0xffffff;
                break;
                case SST_stipple:
                voodoo->params.stipple = val;
                break;
                case SST_color0:
                voodoo->params.color0 = val;
                break;
                case SST_color1:
                voodoo->params.color1 = val;
                break;

                case SST_fogTable00: case SST_fogTable01: case SST_fogTable02: case SST_fogTable03:
                case SST_fogTable04: case SST_fogTable05: case SST_fogTable06: case SST_fogTable07:
                case SST_fogTable08: case SST_fogTable09: case SST_fogTable0a: case SST_fogTable0b:
                case SST_fogTable0c: case SST_fogTable0d: case SST_fogTable0e: case SST_fogTable0f:
                case SST_fogTable10: case SST_fogTable11: case SST_fogTable12: case SST_fogTable13:
                case SST_fogTable14: case SST_fogTable15: case SST_fogTable16: case SST_fogTable17:
                case SST_fogTable18: case SST_fogTable19: case SST_fogTable1a: case SST_fogTable1b:
                case SST_fogTable1c: case SST_fogTable1d: case SST_fogTable1e: case SST_fogTable1f:
                addr = (addr - SST_fogTable00) >> 1;
                voodoo->params.fogTable[addr].dfog   = val & 0xff;
                voodoo->params.fogTable[addr].fog    = (val >> 8) & 0xff;
                voodoo->params.fogTable[addr+1].dfog = (val >> 16) & 0xff;
                voodoo->params.fogTable[addr+1].fog  = (val >> 24) & 0xff;
                break;

                case SST_clipLeftRight1:
                if (voodoo->type >= VOODOO_BANSHEE)
                {
                        voodoo->params.clipRight1 = val & 0xfff;
                        voodoo->params.clipLeft1 = (val >> 16) & 0xfff;
                }
                break;
                case SST_clipTopBottom1:
                if (voodoo->type >= VOODOO_BANSHEE)
                {
                        voodoo->params.clipHighY1 = val & 0xfff;
                        voodoo->params.clipLowY1 = (val >> 16) & 0xfff;
                }
                break;

                case SST_colBufferAddr:
                if (voodoo->type >= VOODOO_BANSHEE)
                {
                        voodoo->params.draw_offset = val & 0xfffff0;
                        voodoo->fb_write_offset = voodoo->params.draw_offset;
//                        voodoo_reg_log("colorBufferAddr=%06x\n", voodoo->params.draw_offset);
                }
                break;
                case SST_colBufferStride:
                if (voodoo->type >= VOODOO_BANSHEE)
                {
                        voodoo->col_tiled = val & (1 << 15);
			voodoo->params.col_tiled = voodoo->col_tiled;
                        if (voodoo->col_tiled)
                        {
                                voodoo->row_width = (val & 0x7f) * 128*32;
//                                voodoo_reg_log("colBufferStride tiled = %i bytes, tiled  %08x\n", voodoo->row_width, val);
                        }
                        else
                        {
                                voodoo->row_width = val & 0x3fff;
//                                voodoo_reg_log("colBufferStride linear = %i bytes, linear\n", voodoo->row_width);
                        }
			voodoo->params.row_width = voodoo->row_width;
                }
                break;
                case SST_auxBufferAddr:
                if (voodoo->type >= VOODOO_BANSHEE)
                {
                        voodoo->params.aux_offset = val & 0xfffff0;
//                        pclog("auxBufferAddr=%06x\n", voodoo->params.aux_offset);
                }
                break;
                case SST_auxBufferStride:
                if (voodoo->type >= VOODOO_BANSHEE)
                {
                        voodoo->aux_tiled = val & (1 << 15);
			voodoo->params.aux_tiled = voodoo->aux_tiled;
                        if (voodoo->aux_tiled)
                        {
                                voodoo->aux_row_width = (val & 0x7f) * 128*32;
//                                voodoo_reg_log("auxBufferStride tiled = %i bytes, tiled\n", voodoo->aux_row_width);
                        }
                        else
                        {
                                voodoo->aux_row_width = val & 0x3fff;
//                                voodoo_reg_log("auxBufferStride linear = %i bytes, linear\n", voodoo->aux_row_width);
                        }
			voodoo->params.aux_row_width = voodoo->aux_row_width;
                }
                break;

                case SST_clutData:
                voodoo->clutData[(val >> 24) & 0x3f].b = val & 0xff;
                voodoo->clutData[(val >> 24) & 0x3f].g = (val >> 8) & 0xff;
                voodoo->clutData[(val >> 24) & 0x3f].r = (val >> 16) & 0xff;
                if (val & 0x20000000)
                {
                        voodoo->clutData[(val >> 24) & 0x3f].b = 255;
                        voodoo->clutData[(val >> 24) & 0x3f].g = 255;
                        voodoo->clutData[(val >> 24) & 0x3f].r = 255;
                }
                voodoo->clutData_dirty = 1;
                break;

                case SST_sSetupMode:
                voodoo->sSetupMode = val;
                break;
                case SST_sVx:
                tempif.i = val;
                voodoo->verts[3].sVx = tempif.f;
//                voodoo_reg_log("sVx[%i]=%f\n", voodoo->vertex_num, tempif.f);
                break;
                case SST_sVy:
                tempif.i = val;
                voodoo->verts[3].sVy = tempif.f;
//                voodoo_reg_log("sVy[%i]=%f\n", voodoo->vertex_num, tempif.f);
                break;
                case SST_sARGB:
                voodoo->verts[3].sBlue  = (float)(val & 0xff);
                voodoo->verts[3].sGreen = (float)((val >> 8) & 0xff);
                voodoo->verts[3].sRed   = (float)((val >> 16) & 0xff);
                voodoo->verts[3].sAlpha = (float)((val >> 24) & 0xff);
                break;
                case SST_sRed:
                tempif.i = val;
                voodoo->verts[3].sRed = tempif.f;
                break;
                case SST_sGreen:
                tempif.i = val;
                voodoo->verts[3].sGreen = tempif.f;
                break;
                case SST_sBlue:
                tempif.i = val;
                voodoo->verts[3].sBlue = tempif.f;
                break;
                case SST_sAlpha:
                tempif.i = val;
                voodoo->verts[3].sAlpha = tempif.f;
                break;
                case SST_sVz:
                tempif.i = val;
                voodoo->verts[3].sVz = tempif.f;
                break;
                case SST_sWb:
                tempif.i = val;
                voodoo->verts[3].sWb = tempif.f;
                break;
                case SST_sW0:
                tempif.i = val;
                voodoo->verts[3].sW0 = tempif.f;
                break;
                case SST_sS0:
                tempif.i = val;
                voodoo->verts[3].sS0 = tempif.f;
                break;
                case SST_sT0:
                tempif.i = val;
                voodoo->verts[3].sT0 = tempif.f;
                break;
                case SST_sW1:
                tempif.i = val;
                voodoo->verts[3].sW1 = tempif.f;
                break;
                case SST_sS1:
                tempif.i = val;
                voodoo->verts[3].sS1 = tempif.f;
                break;
                case SST_sT1:
                tempif.i = val;
                voodoo->verts[3].sT1 = tempif.f;
                break;

                case SST_sBeginTriCMD:
//                voodoo_reg_log("sBeginTriCMD %i %f\n", voodoo->vertex_num, voodoo->verts[4].sVx);
                voodoo->verts[0] = voodoo->verts[3];
                voodoo->verts[1] = voodoo->verts[3];
                voodoo->verts[2] = voodoo->verts[3];
                voodoo->vertex_next_age = 0;
                voodoo->vertex_ages[0] = voodoo->vertex_next_age++;

                voodoo->num_verticies = 1;
                voodoo->cull_pingpong = 0;
                break;
                case SST_sDrawTriCMD:
//                voodoo_reg_log("sDrawTriCMD %i %i\n", voodoo->num_verticies, voodoo->sSetupMode & SETUPMODE_STRIP_MODE);
                /*I'm not sure this is the vertex selection algorithm actually used in the 3dfx
                  chips, but this works with a number of games that switch between strip and fan
                  mode in the middle of a run (eg Black & White, Viper Racing)*/
                if (voodoo->vertex_next_age < 3)
                {
                        /*Fewer than three vertices already written, store in next slot*/
                        int vertex_nr = voodoo->vertex_next_age;

                        voodoo->verts[vertex_nr] = voodoo->verts[3];
                        voodoo->vertex_ages[vertex_nr] = voodoo->vertex_next_age++;
                }
                else
                {
                        int vertex_nr = 0;

                        if (!(voodoo->sSetupMode & SETUPMODE_STRIP_MODE))
                        {
                                /*Strip - find oldest vertex*/
                                if ((voodoo->vertex_ages[0] < voodoo->vertex_ages[1]) &&
                                    (voodoo->vertex_ages[0] < voodoo->vertex_ages[2]))
                                        vertex_nr = 0;
                                else if ((voodoo->vertex_ages[1] < voodoo->vertex_ages[0]) &&
                                    (voodoo->vertex_ages[1] < voodoo->vertex_ages[2]))
                                        vertex_nr = 1;
                                else
                                        vertex_nr = 2;
                        }
                        else
                        {
                                /*Fan - find second oldest vertex (ie pivot around oldest)*/
                                if ((voodoo->vertex_ages[1] < voodoo->vertex_ages[0]) &&
                                    (voodoo->vertex_ages[0] < voodoo->vertex_ages[2]))
                                        vertex_nr = 0;
                                else if ((voodoo->vertex_ages[2] < voodoo->vertex_ages[0]) &&
                                    (voodoo->vertex_ages[0] < voodoo->vertex_ages[1]))
                                        vertex_nr = 0;
                                else if ((voodoo->vertex_ages[0] < voodoo->vertex_ages[1]) &&
                                    (voodoo->vertex_ages[1] < voodoo->vertex_ages[2]))
                                        vertex_nr = 1;
                                else if ((voodoo->vertex_ages[2] < voodoo->vertex_ages[1]) &&
                                    (voodoo->vertex_ages[1] < voodoo->vertex_ages[0]))
                                        vertex_nr = 1;
                                else
                                        vertex_nr = 2;
                        }
                        voodoo->verts[vertex_nr] = voodoo->verts[3];
                        voodoo->vertex_ages[vertex_nr] = voodoo->vertex_next_age++;
                }

                voodoo->num_verticies++;
                if (voodoo->num_verticies == 3)
                {
//                        voodoo_reg_log("triangle_setup\n");
                        voodoo_triangle_setup(voodoo);
                        voodoo->cull_pingpong = !voodoo->cull_pingpong;

                        voodoo->num_verticies = 2;
                }
                break;

                case SST_bltSrcBaseAddr:
                voodoo->bltSrcBaseAddr = val & 0x3fffff;
                break;
                case SST_bltDstBaseAddr:
//                voodoo_reg_log("Write bltDstBaseAddr %08x\n", val);
                voodoo->bltDstBaseAddr = val & 0x3fffff;
                break;
                case SST_bltXYStrides:
                voodoo->bltSrcXYStride = val & 0xfff;
                voodoo->bltDstXYStride = (val >> 16) & 0xfff;
//                voodoo_reg_log("Write bltXYStrides %08x\n", val);
                break;
                case SST_bltSrcChromaRange:
                voodoo->bltSrcChromaRange = val;
                voodoo->bltSrcChromaMinB = val & 0x1f;
                voodoo->bltSrcChromaMinG = (val >> 5) & 0x3f;
                voodoo->bltSrcChromaMinR = (val >> 11) & 0x1f;
                voodoo->bltSrcChromaMaxB = (val >> 16) & 0x1f;
                voodoo->bltSrcChromaMaxG = (val >> 21) & 0x3f;
                voodoo->bltSrcChromaMaxR = (val >> 27) & 0x1f;
                break;
                case SST_bltDstChromaRange:
                voodoo->bltDstChromaRange = val;
                voodoo->bltDstChromaMinB = val & 0x1f;
                voodoo->bltDstChromaMinG = (val >> 5) & 0x3f;
                voodoo->bltDstChromaMinR = (val >> 11) & 0x1f;
                voodoo->bltDstChromaMaxB = (val >> 16) & 0x1f;
                voodoo->bltDstChromaMaxG = (val >> 21) & 0x3f;
                voodoo->bltDstChromaMaxR = (val >> 27) & 0x1f;
                break;
                case SST_bltClipX:
                voodoo->bltClipRight = val & 0xfff;
                voodoo->bltClipLeft = (val >> 16) & 0xfff;
                break;
                case SST_bltClipY:
                voodoo->bltClipHighY = val & 0xfff;
                voodoo->bltClipLowY = (val >> 16) & 0xfff;
                break;

                case SST_bltSrcXY:
                voodoo->bltSrcX = val & 0x7ff;
                voodoo->bltSrcY = (val >> 16) & 0x7ff;
                break;
                case SST_bltDstXY:
//                voodoo_reg_log("Write bltDstXY %08x\n", val);
                voodoo->bltDstX = val & 0x7ff;
                voodoo->bltDstY = (val >> 16) & 0x7ff;
                if (val & (1 << 31))
                        voodoo_v2_blit_start(voodoo);
                break;
                case SST_bltSize:
//                voodoo_reg_log("Write bltSize %08x\n", val);
                voodoo->bltSizeX = val & 0xfff;
                if (voodoo->bltSizeX & 0x800)
                        voodoo->bltSizeX |= 0xfffff000;
                voodoo->bltSizeY = (val >> 16) & 0xfff;
                if (voodoo->bltSizeY & 0x800)
                        voodoo->bltSizeY |= 0xfffff000;
                if (val & (1 << 31))
                        voodoo_v2_blit_start(voodoo);
                break;
                case SST_bltRop:
                voodoo->bltRop[0] = val & 0xf;
                voodoo->bltRop[1] = (val >> 4) & 0xf;
                voodoo->bltRop[2] = (val >> 8) & 0xf;
                voodoo->bltRop[3] = (val >> 12) & 0xf;
                break;
                case SST_bltColor:
//                voodoo_reg_log("Write bltColor %08x\n", val);
                voodoo->bltColorFg = val & 0xffff;
                voodoo->bltColorBg = (val >> 16) & 0xffff;
                break;

                case SST_bltCommand:
                voodoo->bltCommand = val;
//                voodoo_reg_log("Write bltCommand %08x\n", val);
                if (val & (1 << 31))
                        voodoo_v2_blit_start(voodoo);
                break;
                case SST_bltData:
                voodoo_v2_blit_data(voodoo, val);
                break;

                case SST_textureMode:
                if (chip & CHIP_TREX0)
                {
                        voodoo->params.textureMode[0] = val;
                        voodoo->params.tformat[0] = (val >> 8) & 0xf;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->params.textureMode[1] = val;
                        voodoo->params.tformat[1] = (val >> 8) & 0xf;
                }
                break;
                case SST_tLOD:
                if (chip & CHIP_TREX0)
                {
                        voodoo->params.tLOD[0] = val;
                        voodoo_recalc_tex(voodoo, 0);
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->params.tLOD[1] = val;
                        voodoo_recalc_tex(voodoo, 1);
                }
                break;
                case SST_tDetail:
                if (chip & CHIP_TREX0)
                {
                        voodoo->params.detail_max[0] = val & 0xff;
                        voodoo->params.detail_bias[0] = (val >> 8) & 0x3f;
                        voodoo->params.detail_scale[0] = (val >> 14) & 7;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->params.detail_max[1] = val & 0xff;
                        voodoo->params.detail_bias[1] = (val >> 8) & 0x3f;
                        voodoo->params.detail_scale[1] = (val >> 14) & 7;
                }
                break;
                case SST_texBaseAddr:
                if (chip & CHIP_TREX0)
                {
                        if (voodoo->type >= VOODOO_BANSHEE)
                                voodoo->params.texBaseAddr[0] = val & 0xfffff0;
                        else
                                voodoo->params.texBaseAddr[0] = (val & 0x7ffff) << 3;
//                        voodoo_reg_log("texBaseAddr = %08x %08x\n", voodoo->params.texBaseAddr[0], val);
                        voodoo_recalc_tex(voodoo, 0);
                }
                if (chip & CHIP_TREX1)
                {
                        if (voodoo->type >= VOODOO_BANSHEE)
                                voodoo->params.texBaseAddr[1] = val & 0xfffff0;
                        else
                                voodoo->params.texBaseAddr[1] = (val & 0x7ffff) << 3;
                        voodoo_recalc_tex(voodoo, 1);
                }
                break;
                case SST_texBaseAddr1:
                if (chip & CHIP_TREX0)
                {
                        if (voodoo->type >= VOODOO_BANSHEE)
                                voodoo->params.texBaseAddr1[0] = val & 0xfffff0;
                        else
                                voodoo->params.texBaseAddr1[0] = (val & 0x7ffff) << 3;
                        voodoo_recalc_tex(voodoo, 0);
                }
                if (chip & CHIP_TREX1)
                {
                        if (voodoo->type >= VOODOO_BANSHEE)
                                voodoo->params.texBaseAddr1[1] = val & 0xfffff0;
                        else
                                voodoo->params.texBaseAddr1[1] = (val & 0x7ffff) << 3;
                        voodoo_recalc_tex(voodoo, 1);
                }
                break;
                case SST_texBaseAddr2:
                if (chip & CHIP_TREX0)
                {
                        if (voodoo->type >= VOODOO_BANSHEE)
                                voodoo->params.texBaseAddr2[0] = val & 0xfffff0;
                        else
                                voodoo->params.texBaseAddr2[0] = (val & 0x7ffff) << 3;
                        voodoo_recalc_tex(voodoo, 0);
                }
                if (chip & CHIP_TREX1)
                {
                        if (voodoo->type >= VOODOO_BANSHEE)
                                voodoo->params.texBaseAddr2[1] = val & 0xfffff0;
                        else
                                voodoo->params.texBaseAddr2[1] = (val & 0x7ffff) << 3;
                        voodoo_recalc_tex(voodoo, 1);
                }
                break;
                case SST_texBaseAddr38:
                if (chip & CHIP_TREX0)
                {
                        if (voodoo->type >= VOODOO_BANSHEE)
                                voodoo->params.texBaseAddr38[0] = val & 0xfffff0;
                        else
                                voodoo->params.texBaseAddr38[0] = (val & 0x7ffff) << 3;
                        voodoo_recalc_tex(voodoo, 0);
                }
                if (chip & CHIP_TREX1)
                {
                        if (voodoo->type >= VOODOO_BANSHEE)
                                voodoo->params.texBaseAddr38[1] = val & 0xfffff0;
                        else
                                voodoo->params.texBaseAddr38[1] = (val & 0x7ffff) << 3;
                        voodoo_recalc_tex(voodoo, 1);
                }
                break;

                case SST_trexInit1:
                if (chip & CHIP_TREX0)
                        voodoo->trexInit1[0] = val;
                if (chip & CHIP_TREX1)
                        voodoo->trexInit1[1] = val;
                break;

                case SST_nccTable0_Y0:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][0].y[0] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][0].y[0] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable0_Y1:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][0].y[1] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][0].y[1] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable0_Y2:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][0].y[2] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][0].y[2] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable0_Y3:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][0].y[3] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][0].y[3] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;

                case SST_nccTable0_I0:
                if (!(val & (1 << 31)))
                {
                        if (chip & CHIP_TREX0)
                        {
                                voodoo->nccTable[0][0].i[0] = val;
                                voodoo->ncc_dirty[0] = 1;
                        }
                        if (chip & CHIP_TREX1)
                        {
                                voodoo->nccTable[1][0].i[0] = val;
                                voodoo->ncc_dirty[1] = 1;
                        }
                        break;
                }
                case SST_nccTable0_I2:
                if (!(val & (1 << 31)))
                {
                        if (chip & CHIP_TREX0)
                        {
                                voodoo->nccTable[0][0].i[2] = val;
                                voodoo->ncc_dirty[0] = 1;
                        }
                        if (chip & CHIP_TREX1)
                        {
                                voodoo->nccTable[1][0].i[2] = val;
                                voodoo->ncc_dirty[1] = 1;
                        }
                        break;
                }
                case SST_nccTable0_Q0:
                if (!(val & (1 << 31)))
                {
                        if (chip & CHIP_TREX0)
                        {
                                voodoo->nccTable[0][0].q[0] = val;
                                voodoo->ncc_dirty[0] = 1;
                        }
                        if (chip & CHIP_TREX1)
                        {
                                voodoo->nccTable[1][0].q[0] = val;
                                voodoo->ncc_dirty[1] = 1;
                        }
                        break;
                }
                case SST_nccTable0_Q2:
                if (!(val & (1 << 31)))
                {
                        if (chip & CHIP_TREX0)
                        {
                                voodoo->nccTable[0][0].i[2] = val;
                                voodoo->ncc_dirty[0] = 1;
                        }
                        if (chip & CHIP_TREX1)
                        {
                                voodoo->nccTable[1][0].i[2] = val;
                                voodoo->ncc_dirty[1] = 1;
                        }
                        break;
                }
                if (val & (1 << 31))
                {
                        int p = (val >> 23) & 0xfe;
                        if (chip & CHIP_TREX0)
                        {
                                voodoo->palette[0][p].u = val | 0xff000000;
                                voodoo->palette_dirty[0] = 1;
                        }
                        if (chip & CHIP_TREX1)
                        {
                                voodoo->palette[1][p].u = val | 0xff000000;
                                voodoo->palette_dirty[1] = 1;
                        }
                }
                break;

                case SST_nccTable0_I1:
                if (!(val & (1 << 31)))
                {
                        if (chip & CHIP_TREX0)
                        {
                                voodoo->nccTable[0][0].i[1] = val;
                                voodoo->ncc_dirty[0] = 1;
                        }
                        if (chip & CHIP_TREX1)
                        {
                                voodoo->nccTable[1][0].i[1] = val;
                                voodoo->ncc_dirty[1] = 1;
                        }
                        break;
                }
                case SST_nccTable0_I3:
                if (!(val & (1 << 31)))
                {
                        if (chip & CHIP_TREX0)
                        {
                                voodoo->nccTable[0][0].i[3] = val;
                                voodoo->ncc_dirty[0] = 1;
                        }
                        if (chip & CHIP_TREX1)
                        {
                                voodoo->nccTable[1][0].i[3] = val;
                                voodoo->ncc_dirty[1] = 1;
                        }
                        break;
                }
                case SST_nccTable0_Q1:
                if (!(val & (1 << 31)))
                {
                        if (chip & CHIP_TREX0)
                        {
                                voodoo->nccTable[0][0].q[1] = val;
                                voodoo->ncc_dirty[0] = 1;
                        }
                        if (chip & CHIP_TREX1)
                        {
                                voodoo->nccTable[1][0].q[1] = val;
                                voodoo->ncc_dirty[1] = 1;
                        }
                        break;
                }
                case SST_nccTable0_Q3:
                if (!(val & (1 << 31)))
                {
                        if (chip & CHIP_TREX0)
                        {
                                voodoo->nccTable[0][0].q[3] = val;
                                voodoo->ncc_dirty[0] = 1;
                        }
                        if (chip & CHIP_TREX1)
                        {
                                voodoo->nccTable[1][0].q[3] = val;
                                voodoo->ncc_dirty[1] = 1;
                        }
                        break;
                }
                if (val & (1 << 31))
                {
                        int p = ((val >> 23) & 0xfe) | 0x01;
                        if (chip & CHIP_TREX0)
                        {
                                voodoo->palette[0][p].u = val | 0xff000000;
                                voodoo->palette_dirty[0] = 1;
                        }
                        if (chip & CHIP_TREX1)
                        {
                                voodoo->palette[1][p].u = val | 0xff000000;
                                voodoo->palette_dirty[1] = 1;
                        }
                }
                break;

                case SST_nccTable1_Y0:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][1].y[0] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][1].y[0] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable1_Y1:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][1].y[1] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][1].y[1] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable1_Y2:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][1].y[2] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][1].y[2] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable1_Y3:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][1].y[3] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][1].y[3] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable1_I0:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][1].i[0] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][1].i[0] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable1_I1:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][1].i[1] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][1].i[1] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable1_I2:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][1].i[2] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][1].i[2] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable1_I3:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][1].i[3] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][1].i[3] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable1_Q0:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][1].q[0] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][1].q[0] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable1_Q1:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][1].q[1] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][1].q[1] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable1_Q2:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][1].q[2] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][1].q[2] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable1_Q3:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][1].q[3] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][1].q[3] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;

                case SST_userIntrCMD:
                fatal("userIntrCMD write %08x from FIFO\n", val);
                break;


                case SST_leftOverlayBuf:
                voodoo->leftOverlayBuf = val;
                break;
        }
}
