#if defined __loongarch_lp64

/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          LoongArch64 backend for the "new" dynamic recompiler -
 *          immediate materialization.
 *
 *          Skeleton (milestone M0): real mov_imm (plan section 8.2) and
 *          find_imm land in M1/M5; these stubs only exist so the build
 *          links.
 */

#    include <stdint.h>
#    include <86box/86box.h>
#    include "cpu.h"
#    include <86box/mem.h>
#    include <86box/plat_unused.h>

#    include "codegen.h"
#    include "codegen_backend.h"
#    include "codegen_backend_loongarch64_defs.h"

void
host_loong64_mov_imm(UNUSED(codeblock_t *block), UNUSED(int reg), UNUSED(uint64_t imm_data))
{
    fatal("host_loong64_mov_imm: not implemented yet\n");
}

uint32_t
host_loong64_find_imm(UNUSED(uint32_t data))
{
    fatal("host_loong64_find_imm: not implemented yet\n");
    return 0;
}

#endif
