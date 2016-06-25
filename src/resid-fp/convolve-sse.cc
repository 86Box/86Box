//  ---------------------------------------------------------------------------
//  This file is part of reSID, a MOS6581 SID emulator engine.
//  Copyright (C) 2004  Dag Lem <resid@nimrod.no>
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//  ---------------------------------------------------------------------------
#include <stdint.h>
#include "sid.h"

#if (RESID_USE_SSE==1)

#include <xmmintrin.h>

float convolve_sse(const float *a, const float *b, int n)
{
    float out = 0.f;
    __m128 out4 = { 0, 0, 0, 0 };

    /* examine if we can use aligned loads on both pointers */
    int diff = (int) (a - b) & 0xf;
    /* long cast is no-op for x86-32, but x86-64 gcc needs 64 bit intermediate
     * to convince compiler we mean this. */
    unsigned int a_align = (unsigned int) (uintptr_t) a & 0xf;

    /* advance if necessary. We can't let n fall < 0, so no while (n --). */
    while (n > 0 && a_align != 0 && a_align != 16) {
        out += (*(a ++)) * (*(b ++));
        --n;
        a_align += 4;
    }

    int n4 = n / 4;
    if (diff == 0) {
        for (int i = 0; i < n4; i ++) {
            out4 = _mm_add_ps(out4, _mm_mul_ps(_mm_load_ps(a), _mm_load_ps(b)));
            a += 4;
            b += 4;
        }
    } else {
        /* XXX loadu is 4x slower than load, at least. We could at 4x memory
         * use prepare versions of b aligned for any a alignment. We could
         * also issue aligned loads and shuffle the halves at each iteration.
         * Initial results indicate only very small improvements. */
        for (int i = 0; i < n4; i ++) {
            out4 = _mm_add_ps(out4, _mm_mul_ps(_mm_load_ps(a), _mm_loadu_ps(b)));
            a += 4;
            b += 4;
        }
    }

    out4 = _mm_add_ps(_mm_movehl_ps(out4, out4), out4);
    out4 = _mm_add_ss(_mm_shuffle_ps(out4, out4, 1), out4);
    float out_tmp;
    _mm_store_ss(&out_tmp, out4);
    out += out_tmp;

    n &= 3;
    
    while (n --)
        out += (*(a ++)) * (*(b ++));

    return out;
}
#endif
