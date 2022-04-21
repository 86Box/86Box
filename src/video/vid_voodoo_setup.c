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
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_voodoo_common.h>
#include <86box/vid_voodoo_regs.h>
#include <86box/vid_voodoo_render.h>
#include <86box/vid_voodoo_setup.h>

#ifdef ENABLE_VOODOO_SETUP_LOG
int voodoo_setup_do_log = ENABLE_VOODOO_SETUP_LOG;

static void
voodoo_setup_log(const char *fmt, ...)
{
    va_list ap;

    if (voodoo_setup_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define voodoo_setup_log(fmt, ...)
#endif


void voodoo_triangle_setup(voodoo_t *voodoo)
{
        float dxAB, dxBC, dyAB, dyBC;
        float area;
        int va = 0, vb = 1, vc = 2;
        vert_t verts[3];

        verts[0] = voodoo->verts[0];
        verts[1] = voodoo->verts[1];
        verts[2] = voodoo->verts[2];

        if (verts[0].sVy < verts[1].sVy)
        {
                if (verts[1].sVy < verts[2].sVy)
                {
                        /* V1>V0, V2>V1, V2>V1>V0*/
                        va = 0; /*OK*/
                        vb = 1;
                        vc = 2;
                }
                else
                {
                        /* V1>V0, V1>V2*/
                        if (verts[0].sVy < verts[2].sVy)
                        {
                                /* V1>V0, V1>V2, V2>V0, V1>V2>V0*/
                                va = 0;
                                vb = 2;
                                vc = 1;
                        }
                        else
                        {
                                /* V1>V0, V1>V2, V0>V2, V1>V0>V2*/
                                va = 2;
                                vb = 0;
                                vc = 1;
                        }
                }
        }
        else
        {
                if (verts[1].sVy < verts[2].sVy)
                {
                        /* V0>V1, V2>V1*/
                        if (verts[0].sVy < verts[2].sVy)
                        {
                                /* V0>V1, V2>V1, V2>V0, V2>V0>V1*/
                                va = 1;
                                vb = 0;
                                vc = 2;
                        }
                        else
                        {
                                /* V0>V1, V2>V1, V0>V2, V0>V2>V1*/
                                va = 1;
                                vb = 2;
                                vc = 0;
                        }
                }
                else
                {
                        /*V0>V1>V2*/
                        va = 2;
                        vb = 1;
                        vc = 0;
                }
        }

        dxAB = verts[0].sVx - verts[1].sVx;
        dxBC = verts[1].sVx - verts[2].sVx;
        dyAB = verts[0].sVy - verts[1].sVy;
        dyBC = verts[1].sVy - verts[2].sVy;

        area = dxAB * dyBC - dxBC * dyAB;

        if (area == 0.0)
                return;

        if (voodoo->sSetupMode & SETUPMODE_CULLING_ENABLE)
        {
                int cull_sign = voodoo->sSetupMode & SETUPMODE_CULLING_SIGN;
                int sign = (area < 0.0);

                if ((voodoo->sSetupMode & (SETUPMODE_CULLING_ENABLE | SETUPMODE_DISABLE_PINGPONG))
                                == SETUPMODE_CULLING_ENABLE && voodoo->cull_pingpong)
                        cull_sign = !cull_sign;

                if (cull_sign && sign)
                        return;
                if (!cull_sign && !sign)
                        return;
        }


        dxAB = verts[va].sVx - verts[vb].sVx;
        dxBC = verts[vb].sVx - verts[vc].sVx;
        dyAB = verts[va].sVy - verts[vb].sVy;
        dyBC = verts[vb].sVy - verts[vc].sVy;

        area = dxAB * dyBC - dxBC * dyAB;

        dxAB /= area;
        dxBC /= area;
        dyAB /= area;
        dyBC /= area;



        voodoo->params.vertexAx = (int32_t)(int16_t)((int32_t)(verts[va].sVx * 16.0f) & 0xffff);
        voodoo->params.vertexAy = (int32_t)(int16_t)((int32_t)(verts[va].sVy * 16.0f) & 0xffff);
        voodoo->params.vertexBx = (int32_t)(int16_t)((int32_t)(verts[vb].sVx * 16.0f) & 0xffff);
        voodoo->params.vertexBy = (int32_t)(int16_t)((int32_t)(verts[vb].sVy * 16.0f) & 0xffff);
        voodoo->params.vertexCx = (int32_t)(int16_t)((int32_t)(verts[vc].sVx * 16.0f) & 0xffff);
        voodoo->params.vertexCy = (int32_t)(int16_t)((int32_t)(verts[vc].sVy * 16.0f) & 0xffff);

        if (voodoo->params.vertexAy > voodoo->params.vertexBy || voodoo->params.vertexBy > voodoo->params.vertexCy) {
                voodoo_setup_log("triangle_setup wrong order %d %d %d\n", voodoo->params.vertexAy, voodoo->params.vertexBy, voodoo->params.vertexCy);
		return;
	}

        if (voodoo->sSetupMode & SETUPMODE_RGB)
        {
                voodoo->params.startR = (int32_t)(verts[va].sRed * 4096.0f);
                voodoo->params.dRdX = (int32_t)(((verts[va].sRed - verts[vb].sRed) * dyBC - (verts[vb].sRed - verts[vc].sRed) * dyAB) * 4096.0f);
                voodoo->params.dRdY = (int32_t)(((verts[vb].sRed - verts[vc].sRed) * dxAB - (verts[va].sRed - verts[vb].sRed) * dxBC) * 4096.0f);
                voodoo->params.startG = (int32_t)(verts[va].sGreen * 4096.0f);
                voodoo->params.dGdX = (int32_t)(((verts[va].sGreen - verts[vb].sGreen) * dyBC - (verts[vb].sGreen - verts[vc].sGreen) * dyAB) * 4096.0f);
                voodoo->params.dGdY = (int32_t)(((verts[vb].sGreen - verts[vc].sGreen) * dxAB - (verts[va].sGreen - verts[vb].sGreen) * dxBC) * 4096.0f);
                voodoo->params.startB = (int32_t)(verts[va].sBlue * 4096.0f);
                voodoo->params.dBdX = (int32_t)(((verts[va].sBlue - verts[vb].sBlue) * dyBC - (verts[vb].sBlue - verts[vc].sBlue) * dyAB) * 4096.0f);
                voodoo->params.dBdY = (int32_t)(((verts[vb].sBlue - verts[vc].sBlue) * dxAB - (verts[va].sBlue - verts[vb].sBlue) * dxBC) * 4096.0f);
        }
        if (voodoo->sSetupMode & SETUPMODE_ALPHA)
        {
                voodoo->params.startA = (int32_t)(verts[va].sAlpha * 4096.0f);
                voodoo->params.dAdX = (int32_t)(((verts[va].sAlpha - verts[vb].sAlpha) * dyBC - (verts[vb].sAlpha - verts[vc].sAlpha) * dyAB) * 4096.0f);
                voodoo->params.dAdY = (int32_t)(((verts[vb].sAlpha - verts[vc].sAlpha) * dxAB - (verts[va].sAlpha - verts[vb].sAlpha) * dxBC) * 4096.0f);
        }
        if (voodoo->sSetupMode & SETUPMODE_Z)
        {
                voodoo->params.startZ = (int32_t)(verts[va].sVz * 4096.0f);
                voodoo->params.dZdX = (int32_t)(((verts[va].sVz - verts[vb].sVz) * dyBC - (verts[vb].sVz - verts[vc].sVz) * dyAB) * 4096.0f);
                voodoo->params.dZdY = (int32_t)(((verts[vb].sVz - verts[vc].sVz) * dxAB - (verts[va].sVz - verts[vb].sVz) * dxBC) * 4096.0f);
        }
        if (voodoo->sSetupMode & SETUPMODE_Wb)
        {
                voodoo->params.startW = (int64_t)(verts[va].sWb * 4294967296.0f);
                voodoo->params.dWdX = (int64_t)(((verts[va].sWb - verts[vb].sWb) * dyBC - (verts[vb].sWb - verts[vc].sWb) * dyAB) * 4294967296.0f);
                voodoo->params.dWdY = (int64_t)(((verts[vb].sWb - verts[vc].sWb) * dxAB - (verts[va].sWb - verts[vb].sWb) * dxBC) * 4294967296.0f);
                voodoo->params.tmu[0].startW = voodoo->params.tmu[1].startW = voodoo->params.startW;
                voodoo->params.tmu[0].dWdX = voodoo->params.tmu[1].dWdX = voodoo->params.dWdX;
                voodoo->params.tmu[0].dWdY = voodoo->params.tmu[1].dWdY = voodoo->params.dWdY;
        }
        if (voodoo->sSetupMode & SETUPMODE_W0)
        {
                voodoo->params.tmu[0].startW = (int64_t)(verts[va].sW0 * 4294967296.0f);
                voodoo->params.tmu[0].dWdX = (int64_t)(((verts[va].sW0 - verts[vb].sW0) * dyBC - (verts[vb].sW0 - verts[vc].sW0) * dyAB) * 4294967296.0f);
                voodoo->params.tmu[0].dWdY = (int64_t)(((verts[vb].sW0 - verts[vc].sW0) * dxAB - (verts[va].sW0 - verts[vb].sW0) * dxBC) * 4294967296.0f);
                voodoo->params.tmu[1].startW = voodoo->params.tmu[0].startW;
                voodoo->params.tmu[1].dWdX = voodoo->params.tmu[0].dWdX;
                voodoo->params.tmu[1].dWdY = voodoo->params.tmu[0].dWdY;
        }
        if (voodoo->sSetupMode & SETUPMODE_S0_T0)
        {
                voodoo->params.tmu[0].startS = (int64_t)(verts[va].sS0 * 4294967296.0f);
                voodoo->params.tmu[0].dSdX = (int64_t)(((verts[va].sS0 - verts[vb].sS0) * dyBC - (verts[vb].sS0 - verts[vc].sS0) * dyAB) * 4294967296.0f);
                voodoo->params.tmu[0].dSdY = (int64_t)(((verts[vb].sS0 - verts[vc].sS0) * dxAB - (verts[va].sS0 - verts[vb].sS0) * dxBC) * 4294967296.0f);
                voodoo->params.tmu[0].startT = (int64_t)(verts[va].sT0 * 4294967296.0f);
                voodoo->params.tmu[0].dTdX = (int64_t)(((verts[va].sT0 - verts[vb].sT0) * dyBC - (verts[vb].sT0 - verts[vc].sT0) * dyAB) * 4294967296.0f);
                voodoo->params.tmu[0].dTdY = (int64_t)(((verts[vb].sT0 - verts[vc].sT0) * dxAB - (verts[va].sT0 - verts[vb].sT0) * dxBC) * 4294967296.0f);
                voodoo->params.tmu[1].startS = voodoo->params.tmu[0].startS;
                voodoo->params.tmu[1].dSdX = voodoo->params.tmu[0].dSdX;
                voodoo->params.tmu[1].dSdY = voodoo->params.tmu[0].dSdY;
                voodoo->params.tmu[1].startT = voodoo->params.tmu[0].startT;
                voodoo->params.tmu[1].dTdX = voodoo->params.tmu[0].dTdX;
                voodoo->params.tmu[1].dTdY = voodoo->params.tmu[0].dTdY;
        }
        if (voodoo->sSetupMode & SETUPMODE_W1)
        {
                voodoo->params.tmu[1].startW = (int64_t)(verts[va].sW1 * 4294967296.0f);
                voodoo->params.tmu[1].dWdX = (int64_t)(((verts[va].sW1 - verts[vb].sW1) * dyBC - (verts[vb].sW1 - verts[vc].sW1) * dyAB) * 4294967296.0f);
                voodoo->params.tmu[1].dWdY = (int64_t)(((verts[vb].sW1 - verts[vc].sW1) * dxAB - (verts[va].sW1 - verts[vb].sW1) * dxBC) * 4294967296.0f);
        }
        if (voodoo->sSetupMode & SETUPMODE_S1_T1)
        {
                voodoo->params.tmu[1].startS = (int64_t)(verts[va].sS1 * 4294967296.0f);
                voodoo->params.tmu[1].dSdX = (int64_t)(((verts[va].sS1 - verts[vb].sS1) * dyBC - (verts[vb].sS1 - verts[vc].sS1) * dyAB) * 4294967296.0f);
                voodoo->params.tmu[1].dSdY = (int64_t)(((verts[vb].sS1 - verts[vc].sS1) * dxAB - (verts[va].sS1 - verts[vb].sS1) * dxBC) * 4294967296.0f);
                voodoo->params.tmu[1].startT = (int64_t)(verts[va].sT1 * 4294967296.0f);
                voodoo->params.tmu[1].dTdX = (int64_t)(((verts[va].sT1 - verts[vb].sT1) * dyBC - (verts[vb].sT1 - verts[vc].sT1) * dyAB) * 4294967296.0f);
                voodoo->params.tmu[1].dTdY = (int64_t)(((verts[vb].sT1 - verts[vc].sT1) * dxAB - (verts[va].sT1 - verts[vb].sT1) * dxBC) * 4294967296.0f);
        }

        voodoo->params.sign = (area < 0.0);

        if (voodoo->ncc_dirty[0])
                voodoo_update_ncc(voodoo, 0);
        if (voodoo->ncc_dirty[1])
                voodoo_update_ncc(voodoo, 1);
        voodoo->ncc_dirty[0] = voodoo->ncc_dirty[1] = 0;

        voodoo_queue_triangle(voodoo, &voodoo->params);
}
