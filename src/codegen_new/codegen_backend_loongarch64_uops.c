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
 *          uop handlers.
 *
 *          Skeleton (milestone M0): the handler table is empty and every
 *          dispatch fatal()s through codegen_ir_compile()'s NULL check
 *          (non-release builds); the 172 handlers land in M1-M4.
 */

#    include <stdint.h>
#    include <86box/86box.h>
#    include "cpu.h"
#    include <86box/mem.h>
#    include <86box/plat_unused.h>

#    include "codegen.h"
#    include "codegen_backend.h"
#    include "codegen_backend_loongarch64_defs.h"
#    include "codegen_ir_defs.h"

const uOpFn uop_handlers[UOP_MAX] = { 0 };

#endif
