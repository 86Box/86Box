/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Miscellaneous x86 CPU Instructions.
 *
 *
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2018 Fred N. van Kempen.
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2018 Miran Grca.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
#ifndef _X86_OPS_H
#define _X86_OPS_H

#define UN_USED(x) (void) (x)

typedef int (*OpFn)(uint32_t fetchdat);

#ifdef USE_DYNAREC
void x86_setopcodes(const OpFn *opcodes, const OpFn *opcodes_0f,
                    const OpFn *dynarec_opcodes,
                    const OpFn *dynarec_opcodes_0f);

extern const OpFn *x86_dynarec_opcodes;
extern const OpFn *x86_dynarec_opcodes_0f;
extern const OpFn *x86_dynarec_opcodes_d8_a16;
extern const OpFn *x86_dynarec_opcodes_d8_a32;
extern const OpFn *x86_dynarec_opcodes_d9_a16;
extern const OpFn *x86_dynarec_opcodes_d9_a32;
extern const OpFn *x86_dynarec_opcodes_da_a16;
extern const OpFn *x86_dynarec_opcodes_da_a32;
extern const OpFn *x86_dynarec_opcodes_db_a16;
extern const OpFn *x86_dynarec_opcodes_db_a32;
extern const OpFn *x86_dynarec_opcodes_dc_a16;
extern const OpFn *x86_dynarec_opcodes_dc_a32;
extern const OpFn *x86_dynarec_opcodes_dd_a16;
extern const OpFn *x86_dynarec_opcodes_dd_a32;
extern const OpFn *x86_dynarec_opcodes_de_a16;
extern const OpFn *x86_dynarec_opcodes_de_a32;
extern const OpFn *x86_dynarec_opcodes_df_a16;
extern const OpFn *x86_dynarec_opcodes_df_a32;
extern const OpFn *x86_dynarec_opcodes_REPE;
extern const OpFn *x86_dynarec_opcodes_REPNE;
extern const OpFn *x86_dynarec_opcodes_3DNOW;

extern const OpFn dynarec_ops_186[1024];
extern const OpFn dynarec_ops_186_0f[1024];

extern const OpFn dynarec_ops_286[1024];
extern const OpFn dynarec_ops_286_0f[1024];

extern const OpFn dynarec_ops_386[1024];
extern const OpFn dynarec_ops_386_0f[1024];

extern const OpFn dynarec_ops_486_0f[1024];
extern const OpFn dynarec_ops_c486_0f[1024];
extern const OpFn dynarec_ops_stpc_0f[1024];
extern const OpFn dynarec_ops_ibm486_0f[1024];

extern const OpFn dynarec_ops_winchip_0f[1024];
extern const OpFn dynarec_ops_winchip2_0f[1024];

extern const OpFn dynarec_ops_pentium_0f[1024];
extern const OpFn dynarec_ops_pentiummmx_0f[1024];

#    if defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)
extern const OpFn dynarec_ops_c6x86_0f[1024];
extern const OpFn dynarec_ops_c6x86mx_0f[1024];
#    endif

extern const OpFn dynarec_ops_k6_0f[1024];
extern const OpFn dynarec_ops_k62_0f[1024];

extern const OpFn dynarec_ops_pentiumpro_0f[1024];
extern const OpFn dynarec_ops_pentium2_0f[1024];
extern const OpFn dynarec_ops_pentium2d_0f[1024];

extern const OpFn dynarec_ops_sf_fpu_287_d9_a16[256];
extern const OpFn dynarec_ops_sf_fpu_287_d9_a32[256];
extern const OpFn dynarec_ops_sf_fpu_287_da_a16[256];
extern const OpFn dynarec_ops_sf_fpu_287_da_a32[256];
extern const OpFn dynarec_ops_sf_fpu_287_db_a16[256];
extern const OpFn dynarec_ops_sf_fpu_287_db_a32[256];
extern const OpFn dynarec_ops_sf_fpu_287_dc_a16[32];
extern const OpFn dynarec_ops_sf_fpu_287_dc_a32[32];
extern const OpFn dynarec_ops_sf_fpu_287_dd_a16[256];
extern const OpFn dynarec_ops_sf_fpu_287_dd_a32[256];
extern const OpFn dynarec_ops_sf_fpu_287_de_a16[256];
extern const OpFn dynarec_ops_sf_fpu_287_de_a32[256];
extern const OpFn dynarec_ops_sf_fpu_287_df_a16[256];
extern const OpFn dynarec_ops_sf_fpu_287_df_a32[256];

extern const OpFn dynarec_ops_sf_fpu_d8_a16[32];
extern const OpFn dynarec_ops_sf_fpu_d8_a32[32];
extern const OpFn dynarec_ops_sf_fpu_d9_a16[256];
extern const OpFn dynarec_ops_sf_fpu_d9_a32[256];
extern const OpFn dynarec_ops_sf_fpu_da_a16[256];
extern const OpFn dynarec_ops_sf_fpu_da_a32[256];
extern const OpFn dynarec_ops_sf_fpu_db_a16[256];
extern const OpFn dynarec_ops_sf_fpu_db_a32[256];
extern const OpFn dynarec_ops_sf_fpu_dc_a16[32];
extern const OpFn dynarec_ops_sf_fpu_dc_a32[32];
extern const OpFn dynarec_ops_sf_fpu_dd_a16[256];
extern const OpFn dynarec_ops_sf_fpu_dd_a32[256];
extern const OpFn dynarec_ops_sf_fpu_de_a16[256];
extern const OpFn dynarec_ops_sf_fpu_de_a32[256];
extern const OpFn dynarec_ops_sf_fpu_df_a16[256];
extern const OpFn dynarec_ops_sf_fpu_df_a32[256];

extern const OpFn dynarec_ops_fpu_287_d9_a16[256];
extern const OpFn dynarec_ops_fpu_287_d9_a32[256];
extern const OpFn dynarec_ops_fpu_287_da_a16[256];
extern const OpFn dynarec_ops_fpu_287_da_a32[256];
extern const OpFn dynarec_ops_fpu_287_db_a16[256];
extern const OpFn dynarec_ops_fpu_287_db_a32[256];
extern const OpFn dynarec_ops_fpu_287_dc_a16[32];
extern const OpFn dynarec_ops_fpu_287_dc_a32[32];
extern const OpFn dynarec_ops_fpu_287_dd_a16[256];
extern const OpFn dynarec_ops_fpu_287_dd_a32[256];
extern const OpFn dynarec_ops_fpu_287_de_a16[256];
extern const OpFn dynarec_ops_fpu_287_de_a32[256];
extern const OpFn dynarec_ops_fpu_287_df_a16[256];
extern const OpFn dynarec_ops_fpu_287_df_a32[256];

extern const OpFn dynarec_ops_fpu_d8_a16[32];
extern const OpFn dynarec_ops_fpu_d8_a32[32];
extern const OpFn dynarec_ops_fpu_d9_a16[256];
extern const OpFn dynarec_ops_fpu_d9_a32[256];
extern const OpFn dynarec_ops_fpu_da_a16[256];
extern const OpFn dynarec_ops_fpu_da_a32[256];
extern const OpFn dynarec_ops_fpu_db_a16[256];
extern const OpFn dynarec_ops_fpu_db_a32[256];
extern const OpFn dynarec_ops_fpu_dc_a16[32];
extern const OpFn dynarec_ops_fpu_dc_a32[32];
extern const OpFn dynarec_ops_fpu_dd_a16[256];
extern const OpFn dynarec_ops_fpu_dd_a32[256];
extern const OpFn dynarec_ops_fpu_de_a16[256];
extern const OpFn dynarec_ops_fpu_de_a32[256];
extern const OpFn dynarec_ops_fpu_df_a16[256];
extern const OpFn dynarec_ops_fpu_df_a32[256];
extern const OpFn dynarec_ops_nofpu_a16[256];
extern const OpFn dynarec_ops_nofpu_a32[256];

extern const OpFn dynarec_ops_sf_fpu_686_da_a16[256];
extern const OpFn dynarec_ops_sf_fpu_686_da_a32[256];
extern const OpFn dynarec_ops_sf_fpu_686_db_a16[256];
extern const OpFn dynarec_ops_sf_fpu_686_db_a32[256];
extern const OpFn dynarec_ops_sf_fpu_686_df_a16[256];
extern const OpFn dynarec_ops_sf_fpu_686_df_a32[256];

extern const OpFn dynarec_ops_fpu_686_da_a16[256];
extern const OpFn dynarec_ops_fpu_686_da_a32[256];
extern const OpFn dynarec_ops_fpu_686_db_a16[256];
extern const OpFn dynarec_ops_fpu_686_db_a32[256];
extern const OpFn dynarec_ops_fpu_686_df_a16[256];
extern const OpFn dynarec_ops_fpu_686_df_a32[256];

extern const OpFn dynarec_ops_REPE[1024];
extern const OpFn dynarec_ops_REPNE[1024];
extern const OpFn dynarec_ops_3DNOW[256];
extern const OpFn dynarec_ops_3DNOWE[256];
#else
void x86_setopcodes(const OpFn *opcodes, const OpFn *opcodes_0f);
#endif


extern const OpFn *x86_opcodes;
extern const OpFn *x86_opcodes_0f;
extern const OpFn *x86_opcodes_d8_a16;
extern const OpFn *x86_opcodes_d8_a32;
extern const OpFn *x86_opcodes_d9_a16;
extern const OpFn *x86_opcodes_d9_a32;
extern const OpFn *x86_opcodes_da_a16;
extern const OpFn *x86_opcodes_da_a32;
extern const OpFn *x86_opcodes_db_a16;
extern const OpFn *x86_opcodes_db_a32;
extern const OpFn *x86_opcodes_dc_a16;
extern const OpFn *x86_opcodes_dc_a32;
extern const OpFn *x86_opcodes_dd_a16;
extern const OpFn *x86_opcodes_dd_a32;
extern const OpFn *x86_opcodes_de_a16;
extern const OpFn *x86_opcodes_de_a32;
extern const OpFn *x86_opcodes_df_a16;
extern const OpFn *x86_opcodes_df_a32;
extern const OpFn *x86_opcodes_REPE;
extern const OpFn *x86_opcodes_REPNE;
extern const OpFn *x86_opcodes_3DNOW;

extern const OpFn ops_186[1024];
extern const OpFn ops_186_0f[1024];

extern const OpFn ops_286[1024];
extern const OpFn ops_286_0f[1024];

extern const OpFn ops_386[1024];
extern const OpFn ops_386_0f[1024];

extern const OpFn ops_486_0f[1024];
extern const OpFn ops_c486_0f[1024];
extern const OpFn ops_stpc_0f[1024];
extern const OpFn ops_ibm486_0f[1024];

extern const OpFn ops_winchip_0f[1024];
extern const OpFn ops_winchip2_0f[1024];

extern const OpFn ops_pentium_0f[1024];
extern const OpFn ops_pentiummmx_0f[1024];

#if defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)
extern const OpFn ops_c6x86_0f[1024];
extern const OpFn ops_c6x86mx_0f[1024];
#endif

extern const OpFn ops_k6_0f[1024];
extern const OpFn ops_k62_0f[1024];

extern const OpFn ops_pentiumpro_0f[1024];
extern const OpFn ops_pentium2_0f[1024];
extern const OpFn ops_pentium2d_0f[1024];

extern const OpFn ops_sf_fpu_287_d9_a16[256];
extern const OpFn ops_sf_fpu_287_d9_a32[256];
extern const OpFn ops_sf_fpu_287_da_a16[256];
extern const OpFn ops_sf_fpu_287_da_a32[256];
extern const OpFn ops_sf_fpu_287_db_a16[256];
extern const OpFn ops_sf_fpu_287_db_a32[256];
extern const OpFn ops_sf_fpu_287_dc_a16[32];
extern const OpFn ops_sf_fpu_287_dc_a32[32];
extern const OpFn ops_sf_fpu_287_dd_a16[256];
extern const OpFn ops_sf_fpu_287_dd_a32[256];
extern const OpFn ops_sf_fpu_287_de_a16[256];
extern const OpFn ops_sf_fpu_287_de_a32[256];
extern const OpFn ops_sf_fpu_287_df_a16[256];
extern const OpFn ops_sf_fpu_287_df_a32[256];

extern const OpFn ops_sf_fpu_d8_a16[32];
extern const OpFn ops_sf_fpu_d8_a32[32];
extern const OpFn ops_sf_fpu_d9_a16[256];
extern const OpFn ops_sf_fpu_d9_a32[256];
extern const OpFn ops_sf_fpu_da_a16[256];
extern const OpFn ops_sf_fpu_da_a32[256];
extern const OpFn ops_sf_fpu_db_a16[256];
extern const OpFn ops_sf_fpu_db_a32[256];
extern const OpFn ops_sf_fpu_dc_a16[32];
extern const OpFn ops_sf_fpu_dc_a32[32];
extern const OpFn ops_sf_fpu_dd_a16[256];
extern const OpFn ops_sf_fpu_dd_a32[256];
extern const OpFn ops_sf_fpu_de_a16[256];
extern const OpFn ops_sf_fpu_de_a32[256];
extern const OpFn ops_sf_fpu_df_a16[256];
extern const OpFn ops_sf_fpu_df_a32[256];

extern const OpFn ops_fpu_287_d9_a16[256];
extern const OpFn ops_fpu_287_d9_a32[256];
extern const OpFn ops_fpu_287_da_a16[256];
extern const OpFn ops_fpu_287_da_a32[256];
extern const OpFn ops_fpu_287_db_a16[256];
extern const OpFn ops_fpu_287_db_a32[256];
extern const OpFn ops_fpu_287_dc_a16[32];
extern const OpFn ops_fpu_287_dc_a32[32];
extern const OpFn ops_fpu_287_dd_a16[256];
extern const OpFn ops_fpu_287_dd_a32[256];
extern const OpFn ops_fpu_287_de_a16[256];
extern const OpFn ops_fpu_287_de_a32[256];
extern const OpFn ops_fpu_287_df_a16[256];
extern const OpFn ops_fpu_287_df_a32[256];

extern const OpFn ops_fpu_d8_a16[32];
extern const OpFn ops_fpu_d8_a32[32];
extern const OpFn ops_fpu_d9_a16[256];
extern const OpFn ops_fpu_d9_a32[256];
extern const OpFn ops_fpu_da_a16[256];
extern const OpFn ops_fpu_da_a32[256];
extern const OpFn ops_fpu_db_a16[256];
extern const OpFn ops_fpu_db_a32[256];
extern const OpFn ops_fpu_dc_a16[32];
extern const OpFn ops_fpu_dc_a32[32];
extern const OpFn ops_fpu_dd_a16[256];
extern const OpFn ops_fpu_dd_a32[256];
extern const OpFn ops_fpu_de_a16[256];
extern const OpFn ops_fpu_de_a32[256];
extern const OpFn ops_fpu_df_a16[256];
extern const OpFn ops_fpu_df_a32[256];
extern const OpFn ops_nofpu_a16[256];
extern const OpFn ops_nofpu_a32[256];

extern const OpFn ops_sf_fpu_686_da_a16[256];
extern const OpFn ops_sf_fpu_686_da_a32[256];
extern const OpFn ops_sf_fpu_686_db_a16[256];
extern const OpFn ops_sf_fpu_686_db_a32[256];
extern const OpFn ops_sf_fpu_686_df_a16[256];
extern const OpFn ops_sf_fpu_686_df_a32[256];

extern const OpFn ops_fpu_686_da_a16[256];
extern const OpFn ops_fpu_686_da_a32[256];
extern const OpFn ops_fpu_686_db_a16[256];
extern const OpFn ops_fpu_686_db_a32[256];
extern const OpFn ops_fpu_686_df_a16[256];
extern const OpFn ops_fpu_686_df_a32[256];

extern const OpFn ops_REPE[1024];
extern const OpFn ops_REPNE[1024];
extern const OpFn ops_3DNOW[256];
extern const OpFn ops_3DNOWE[256];

#define C0 (1 << 8)
#define C1 (1 << 9)
#define C2 (1 << 10)
#define C3 (1 << 14)

#endif /*_X86_OPS_H*/
