#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#include <86box/86box.h>
#include <86box/mem.h>
#include "cpu.h"
#include "x86.h"
#include "x86_ops.h"
#include "x86_flags.h"
#include "x87.h"
#include "386_common.h"
#include "cpu.h"
#include "codegen.h"
#include "codegen_ops.h"

#if defined __amd64__ || defined _M_X64
#    include "codegen_ops_x86-64.h"
#elif defined i386 || defined __i386 || defined __i386__ || defined _X86_ || defined _M_IX86
#    include "codegen_ops_x86.h"
#endif

#include "codegen_ops_arith.h"
#include "codegen_ops_fpu.h"
#include "codegen_ops_jump.h"
#include "codegen_ops_logic.h"
#include "codegen_ops_misc.h"
#include "codegen_ops_mmx.h"
#include "codegen_ops_mov.h"
#include "codegen_ops_shift.h"
#include "codegen_ops_stack.h"
#include "codegen_ops_xchg.h"

RecompOpFn recomp_opcodes[512] = {
    // clang-format off
        /*16-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropADD_b_rmw,   ropADD_w_rmw,   ropADD_b_rm,    ropADD_w_rm,    ropADD_AL_imm,  ropADD_AX_imm,  ropPUSH_ES_16,  ropPOP_ES_16,   ropOR_b_rmw,    ropOR_w_rmw,    ropOR_b_rm,     ropOR_w_rm,     ropOR_AL_imm,   ropOR_AX_imm,   ropPUSH_CS_16,  NULL,
/*10*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropPUSH_SS_16,  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropPUSH_DS_16,  ropPOP_DS_16,
/*20*/  ropAND_b_rmw,   ropAND_w_rmw,   ropAND_b_rm,    ropAND_w_rm,    ropAND_AL_imm,  ropAND_AX_imm,  NULL,           NULL,           ropSUB_b_rmw,   ropSUB_w_rmw,   ropSUB_b_rm,    ropSUB_w_rm,    ropSUB_AL_imm,  ropSUB_AX_imm,  NULL,           NULL,
/*30*/  ropXOR_b_rmw,   ropXOR_w_rmw,   ropXOR_b_rm,    ropXOR_w_rm,    ropXOR_AL_imm,  ropXOR_AX_imm,  NULL,           NULL,           ropCMP_b_rmw,   ropCMP_w_rmw,   ropCMP_b_rm,    ropCMP_w_rm,    ropCMP_AL_imm,  ropCMP_AX_imm,  NULL,           NULL,

/*40*/  ropINC_rw,      ropINC_rw,      ropINC_rw,      ropINC_rw,      ropINC_rw,      ropINC_rw,      ropINC_rw,      ropINC_rw,      ropDEC_rw,      ropDEC_rw,      ropDEC_rw,      ropDEC_rw,      ropDEC_rw,      ropDEC_rw,      ropDEC_rw,      ropDEC_rw,
/*50*/  ropPUSH_16,     ropPUSH_16,     ropPUSH_16,     ropPUSH_16,     ropPUSH_16,     ropPUSH_16,     ropPUSH_16,     ropPUSH_16,     ropPOP_16,      ropPOP_16,      ropPOP_16,      ropPOP_16,      ropPOP_16,      ropPOP_16,      ropPOP_16,      ropPOP_16,
/*60*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropPUSH_imm_16, NULL,           ropPUSH_imm_b16,NULL,           NULL,           NULL,           NULL,           NULL,
/*70*/  ropJO,          ropJNO,         ropJB,          ropJNB,         ropJE,          ropJNE,         ropJBE,         ropJNBE,        ropJS,          ropJNS,         ropJP,          ropJNP,         ropJL,          ropJNL,         ropJLE,         ropJNLE,

/*80*/  rop80,          rop81_w,        rop80,          rop83_w,        ropTEST_b_rm,   ropTEST_w_rm,   ropXCHG_b,      ropXCHG_w,      ropMOV_b_r,     ropMOV_w_r,     ropMOV_r_b,     ropMOV_r_w,     ropMOV_w_seg,   ropLEA_w,       ropMOV_seg_w,   NULL,
/*90*/  ropNOP,         ropXCHG_AX_CX,  ropXCHG_AX_DX,  ropXCHG_AX_BX,  ropXCHG_AX_SP,  ropXCHG_AX_BP,  ropXCHG_AX_SI,  ropXCHG_AX_DI,  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*a0*/  ropMOV_AL_a,    ropMOV_AX_a,    ropMOV_a_AL,    ropMOV_a_AX,    NULL,           NULL,           NULL,           NULL,           ropTEST_AL_imm, ropTEST_AX_imm, NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*b0*/  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rw_imm,  ropMOV_rw_imm,  ropMOV_rw_imm,  ropMOV_rw_imm,  ropMOV_rw_imm,  ropMOV_rw_imm,  ropMOV_rw_imm,  ropMOV_rw_imm,

/*c0*/  ropC0,          ropC1_w,        ropRET_imm_16,  ropRET_16,      ropLES,         ropLDS,         ropMOV_b_imm,   ropMOV_w_imm,   NULL,           ropLEAVE_16,    NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*d0*/  ropD0,          ropD1_w,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  NULL,           NULL,           ropLOOP,        ropJCXZ,        NULL,           NULL,           NULL,           NULL,           ropCALL_r16,    ropJMP_r16,     NULL,           ropJMP_r8,      NULL,           NULL,           NULL,           NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropF6,          ropF7_w,        NULL,           NULL,           ropCLI,         ropSTI,         ropCLD,         ropSTD,         ropFE,          ropFF_16,

        /*32-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropADD_b_rmw,   ropADD_l_rmw,   ropADD_b_rm,    ropADD_l_rm,    ropADD_AL_imm,  ropADD_EAX_imm, ropPUSH_ES_32,  ropPOP_ES_32,   ropOR_b_rmw,    ropOR_l_rmw,    ropOR_b_rm,     ropOR_l_rm,     ropOR_AL_imm,   ropOR_EAX_imm,  ropPUSH_CS_32,  NULL,
/*10*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropPUSH_SS_32,  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropPUSH_DS_32,  ropPOP_DS_32,
/*20*/  ropAND_b_rmw,   ropAND_l_rmw,   ropAND_b_rm,    ropAND_l_rm,    ropAND_AL_imm,  ropAND_EAX_imm, NULL,           NULL,           ropSUB_b_rmw,   ropSUB_l_rmw,   ropSUB_b_rm,    ropSUB_l_rm,    ropSUB_AL_imm,  ropSUB_EAX_imm, NULL,           NULL,
/*30*/  ropXOR_b_rmw,   ropXOR_l_rmw,   ropXOR_b_rm,    ropXOR_l_rm,    ropXOR_AL_imm,  ropXOR_EAX_imm, NULL,           NULL,           ropCMP_b_rmw,   ropCMP_l_rmw,   ropCMP_b_rm,    ropCMP_l_rm,    ropCMP_AL_imm,  ropCMP_EAX_imm, NULL,           NULL,

/*40*/  ropINC_rl,      ropINC_rl,      ropINC_rl,      ropINC_rl,      ropINC_rl,      ropINC_rl,      ropINC_rl,      ropINC_rl,      ropDEC_rl,      ropDEC_rl,      ropDEC_rl,      ropDEC_rl,      ropDEC_rl,      ropDEC_rl,      ropDEC_rl,      ropDEC_rl,
/*50*/  ropPUSH_32,     ropPUSH_32,     ropPUSH_32,     ropPUSH_32,     ropPUSH_32,     ropPUSH_32,     ropPUSH_32,     ropPUSH_32,     ropPOP_32,      ropPOP_32,      ropPOP_32,      ropPOP_32,      ropPOP_32,      ropPOP_32,      ropPOP_32,      ropPOP_32,
/*60*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropPUSH_imm_32, NULL,           ropPUSH_imm_b32,NULL,           NULL,           NULL,           NULL,           NULL,
/*70*/  ropJO,          ropJNO,         ropJB,          ropJNB,         ropJE,          ropJNE,         ropJBE,         ropJNBE,        ropJS,          ropJNS,         ropJP,          ropJNP,         ropJL,          ropJNL,         ropJLE,         ropJNLE,

/*80*/  rop80,          rop81_l,        rop80,          rop83_l,        ropTEST_b_rm,   ropTEST_l_rm,   ropXCHG_b,      ropXCHG_l,      ropMOV_b_r,     ropMOV_l_r,     ropMOV_r_b,     ropMOV_r_l,     ropMOV_w_seg,   ropLEA_l,       ropMOV_seg_w,   NULL,
/*90*/  ropNOP,         ropXCHG_EAX_ECX,ropXCHG_EAX_EDX,ropXCHG_EAX_EBX,ropXCHG_EAX_ESP,ropXCHG_EAX_EBP,ropXCHG_EAX_ESI,ropXCHG_EAX_EDI,NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*a0*/  ropMOV_AL_a,    ropMOV_EAX_a,   ropMOV_a_AL,    ropMOV_a_EAX,   NULL,           NULL,           NULL,           NULL,           ropTEST_AL_imm, ropTEST_EAX_imm,NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*b0*/  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rl_imm,  ropMOV_rl_imm,  ropMOV_rl_imm,  ropMOV_rl_imm,  ropMOV_rl_imm,  ropMOV_rl_imm,  ropMOV_rl_imm,  ropMOV_rl_imm,

/*c0*/  ropC0,          ropC1_l,        ropRET_imm_32,  ropRET_32,      ropLES,         ropLDS,         ropMOV_b_imm,   ropMOV_l_imm,   NULL,           ropLEAVE_32,    NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*d0*/  ropD0,          ropD1_l,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  NULL,           NULL,           ropLOOP,        ropJCXZ,        NULL,           NULL,           NULL,           NULL,           ropCALL_r32,    ropJMP_r32,     NULL,           ropJMP_r8,      NULL,           NULL,           NULL,           NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropF6,          ropF7_l,        NULL,           NULL,           ropCLI,         ropSTI,         ropCLD,         ropSTD,         ropFE,          ropFF_32
    // clang-format on
};

RecompOpFn recomp_opcodes_0f[512] = {
    // clang-format off
        /*16-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*10*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*20*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*30*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*40*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*50*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*60*/  ropPUNPCKLBW,   ropPUNPCKLWD,   ropPUNPCKLDQ,   ropPACKSSWB,    ropPCMPGTB,     ropPCMPGTW,     ropPCMPGTD,     ropPACKUSWB,    ropPUNPCKHBW,   ropPUNPCKHWD,   ropPUNPCKHDQ,   ropPACKSSDW,    NULL,           NULL,           ropMOVD_mm_l,   ropMOVQ_mm_q,
/*70*/  NULL,           ropPSxxW_imm,   ropPSxxD_imm,   ropPSxxQ_imm,   ropPCMPEQB,     ropPCMPEQW,     ropPCMPEQD,     ropEMMS,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropMOVD_l_mm,   ropMOVQ_q_mm,

/*80*/  ropJO_w,        ropJNO_w,       ropJB_w,        ropJNB_w,       ropJE_w,        ropJNE_w,       ropJBE_w,       ropJNBE_w,      ropJS_w,        ropJNS_w,       ropJP_w,        ropJNP_w,       ropJL_w,        ropJNL_w,       ropJLE_w,       ropJNLE_w,
/*90*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*a0*/  ropPUSH_FS_16,  ropPOP_FS_16,   NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropPUSH_GS_16,  ropPOP_GS_16,   NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*b0*/  NULL,           NULL,           ropLSS,         NULL,           ropLFS,         ropLGS,         ropMOVZX_w_b,   NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropMOVSX_w_b,   NULL,

/*c0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*d0*/  NULL,           ropPSRLW,       ropPSRLD,       ropPSRLQ,       NULL,           ropPMULLW,      NULL,           NULL,           ropPSUBUSB,     ropPSUBUSW,     NULL,           ropPAND,        ropPADDUSB,     ropPADDUSW,     NULL,           ropPANDN,
/*e0*/  NULL,           ropPSRAW,       ropPSRAD,       NULL,           NULL,           ropPMULHW,      NULL,           NULL,           ropPSUBSB,      ropPSUBSW,      NULL,           ropPOR,         ropPADDSB,      ropPADDSW,      NULL,           ropPXOR,
/*f0*/  NULL,           ropPSLLW,       ropPSLLD,       ropPSLLQ,       NULL,           ropPMADDWD,     NULL,           NULL,           ropPSUBB,       ropPSUBW,       ropPSUBD,       NULL,           ropPADDB,       ropPADDW,       ropPADDD,       NULL,

        /*32-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*10*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*20*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*30*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*40*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*50*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*60*/  ropPUNPCKLBW,   ropPUNPCKLWD,   ropPUNPCKLDQ,   ropPACKSSWB,    ropPCMPGTB,     ropPCMPGTW,     ropPCMPGTD,     ropPACKUSWB,    ropPUNPCKHBW,   ropPUNPCKHWD,   ropPUNPCKHDQ,   ropPACKSSDW,    NULL,           NULL,           ropMOVD_mm_l,   ropMOVQ_mm_q,
/*70*/  NULL,           ropPSxxW_imm,   ropPSxxD_imm,   ropPSxxQ_imm,   ropPCMPEQB,     ropPCMPEQW,     ropPCMPEQD,     ropEMMS,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropMOVD_l_mm,   ropMOVQ_q_mm,

/*80*/  ropJO_l,        ropJNO_l,       ropJB_l,        ropJNB_l,       ropJE_l,        ropJNE_l,       ropJBE_l,       ropJNBE_l,      ropJS_l,        ropJNS_l,       ropJP_l,        ropJNP_l,       ropJL_l,        ropJNL_l,       ropJLE_l,       ropJNLE_l,
/*90*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*a0*/  ropPUSH_FS_32,  ropPOP_FS_32,   NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropPUSH_GS_32,  ropPOP_GS_32,   NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*b0*/  NULL,           NULL,           ropLSS,         NULL,           ropLFS,         ropLGS,         ropMOVZX_l_b,   ropMOVZX_l_w,   NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropMOVSX_l_b,   ropMOVSX_l_w,

/*c0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*d0*/  NULL,           ropPSRLW,       ropPSRLD,       ropPSRLQ,       NULL,           ropPMULLW,      NULL,           NULL,           ropPSUBUSB,     ropPSUBUSW,     NULL,           ropPAND,        ropPADDUSB,     ropPADDUSW,     NULL,           ropPANDN,
/*e0*/  NULL,           ropPSRAW,       ropPSRAD,       NULL,           NULL,           ropPMULHW,      NULL,           NULL,           ropPSUBSB,      ropPSUBSW,      NULL,           ropPOR,         ropPADDSB,      ropPADDSW,      NULL,           ropPXOR,
/*f0*/  NULL,           ropPSLLW,       ropPSLLD,       ropPSLLQ,       NULL,           ropPMADDWD,     NULL,           NULL,           ropPSUBB,       ropPSUBW,       ropPSUBD,       NULL,           ropPADDB,       ropPADDW,       ropPADDD,       NULL,
    // clang-format on
};

RecompOpFn recomp_opcodes_d8[512] = {
    // clang-format off
        /*16-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,
/*10*/  ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,
/*20*/  ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,
/*30*/  ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,

/*40*/  ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,
/*50*/  ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,
/*60*/  ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,
/*70*/  ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,

/*80*/  ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,
/*90*/  ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,
/*a0*/  ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,
/*b0*/  ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,

/*c0*/  ropFADD,        ropFADD,        ropFADD,        ropFADD,        ropFADD,        ropFADD,        ropFADD,        ropFADD,        ropFMUL,        ropFMUL,        ropFMUL,        ropFMUL,        ropFMUL,        ropFMUL,        ropFMUL,        ropFMUL,
/*d0*/  ropFCOM,        ropFCOM,        ropFCOM,        ropFCOM,        ropFCOM,        ropFCOM,        ropFCOM,        ropFCOM,        ropFCOMP,       ropFCOMP,       ropFCOMP,       ropFCOMP,       ropFCOMP,       ropFCOMP,       ropFCOMP,       ropFCOMP,
/*e0*/  ropFSUB,        ropFSUB,        ropFSUB,        ropFSUB,        ropFSUB,        ropFSUB,        ropFSUB,        ropFSUB,        ropFSUBR,       ropFSUBR,       ropFSUBR,       ropFSUBR,       ropFSUBR,       ropFSUBR,       ropFSUBR,       ropFSUBR,
/*f0*/  ropFDIV,        ropFDIV,        ropFDIV,        ropFDIV,        ropFDIV,        ropFDIV,        ropFDIV,        ropFDIV,        ropFDIVR,       ropFDIVR,       ropFDIVR,       ropFDIVR,       ropFDIVR,       ropFDIVR,       ropFDIVR,       ropFDIVR,

        /*32-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,
/*10*/  ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,
/*20*/  ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,
/*30*/  ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,

/*40*/  ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,
/*50*/  ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,
/*60*/  ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,
/*70*/  ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,

/*80*/  ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFADDs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,       ropFMULs,
/*90*/  ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMs,       ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,      ropFCOMPs,
/*a0*/  ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBs,       ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,      ropFSUBRs,
/*b0*/  ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVs,       ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,      ropFDIVRs,

/*c0*/  ropFADD,        ropFADD,        ropFADD,        ropFADD,        ropFADD,        ropFADD,        ropFADD,        ropFADD,        ropFMUL,        ropFMUL,        ropFMUL,        ropFMUL,        ropFMUL,        ropFMUL,        ropFMUL,        ropFMUL,
/*d0*/  ropFCOM,        ropFCOM,        ropFCOM,        ropFCOM,        ropFCOM,        ropFCOM,        ropFCOM,        ropFCOM,        ropFCOMP,       ropFCOMP,       ropFCOMP,       ropFCOMP,       ropFCOMP,       ropFCOMP,       ropFCOMP,       ropFCOMP,
/*e0*/  ropFSUB,        ropFSUB,        ropFSUB,        ropFSUB,        ropFSUB,        ropFSUB,        ropFSUB,        ropFSUB,        ropFSUBR,       ropFSUBR,       ropFSUBR,       ropFSUBR,       ropFSUBR,       ropFSUBR,       ropFSUBR,       ropFSUBR,
/*f0*/  ropFDIV,        ropFDIV,        ropFDIV,        ropFDIV,        ropFDIV,        ropFDIV,        ropFDIV,        ropFDIV,        ropFDIVR,       ropFDIVR,       ropFDIVR,       ropFDIVR,       ropFDIVR,       ropFDIVR,       ropFDIVR,       ropFDIVR,
    // clang-format on
};

RecompOpFn recomp_opcodes_d9[512] = {
    // clang-format off
        /*16-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*10*/  ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,
/*20*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,
/*30*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,

/*40*/  ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*50*/  ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,
/*60*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,
/*70*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,

/*80*/  ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*90*/  ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,
/*a0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,
/*b0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,

/*c0*/  ropFLD,         ropFLD,         ropFLD,         ropFLD,         ropFLD,         ropFLD,         ropFLD,         ropFLD,         ropFXCH,        ropFXCH,        ropFXCH,        ropFXCH,        ropFXCH,        ropFXCH,        ropFXCH,        ropFXCH,
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  ropFCHS,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFLD1,        ropFLDL2T,      ropFLDL2E,      ropFLDPI,       ropFLDEG2,      ropFLDLN2,      ropFLDZ,        NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

        /*32-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*10*/  ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,
/*20*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,
/*30*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,

/*40*/  ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*50*/  ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,
/*60*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,
/*70*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,

/*80*/  ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*90*/  ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,
/*a0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,       ropFLDCW,
/*b0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,

/*c0*/  ropFLD,         ropFLD,         ropFLD,         ropFLD,         ropFLD,         ropFLD,         ropFLD,         ropFLD,         ropFXCH,        ropFXCH,        ropFXCH,        ropFXCH,        ropFXCH,        ropFXCH,        ropFXCH,        ropFXCH,
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  ropFCHS,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFLD1,        ropFLDL2T,      ropFLDL2E,      ropFLDPI,       ropFLDEG2,      ropFLDLN2,      ropFLDZ,        NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
    // clang-format on
};

RecompOpFn recomp_opcodes_da[512] = {
    // clang-format off
        /*16-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,
/*10*/  ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,
/*20*/  ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,
/*30*/  ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,

/*40*/  ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,
/*50*/  ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,
/*60*/  ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,
/*70*/  ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,

/*80*/  ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,
/*90*/  ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,
/*a0*/  ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,
/*b0*/  ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,

/*c0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

        /*32-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,
/*10*/  ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,
/*20*/  ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,
/*30*/  ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,

/*40*/  ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,
/*50*/  ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,
/*60*/  ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,
/*70*/  ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,

/*80*/  ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFADDil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,      ropFMULil,
/*90*/  ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMil,      ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,     ropFCOMPil,
/*a0*/  ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBil,      ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,     ropFSUBRil,
/*b0*/  ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVil,      ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,     ropFDIVRil,

/*c0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
    // clang-format on
};

RecompOpFn recomp_opcodes_db[512] = {
    // clang-format off
        /*16-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*10*/  ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,
/*20*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*30*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*40*/  ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*50*/  ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,
/*60*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*70*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*80*/  ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*90*/  ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,
/*a0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*b0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*c0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

        /*32-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*80*/  ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*10*/  ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,
/*20*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*30*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*40*/  ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*50*/  ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,
/*60*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*70*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*80*/  ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       ropFILDl,       NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*90*/  ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTl,       ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,      ropFISTPl,
/*a0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*b0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*c0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
    // clang-format on
};

RecompOpFn recomp_opcodes_dc[512] = {
    // clang-format off
        /*16-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,
/*10*/  ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,
/*20*/  ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,
/*30*/  ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,

/*40*/  ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,
/*50*/  ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,
/*60*/  ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,
/*70*/  ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,

/*80*/  ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,
/*90*/  ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,
/*a0*/  ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,
/*b0*/  ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,

/*c0*/  ropFADDr,       ropFADDr,       ropFADDr,       ropFADDr,       ropFADDr,       ropFADDr,       ropFADDr,       ropFADDr,       ropFMULr,       ropFMULr,       ropFMULr,       ropFMULr,       ropFMULr,       ropFMULr,       ropFMULr,       ropFMULr,
/*d0*/  ropFCOM,        ropFCOM,        ropFCOM,        ropFCOM,        ropFCOM,        ropFCOM,        ropFCOM,        ropFCOM,        ropFCOMP,       ropFCOMP,       ropFCOMP,       ropFCOMP,       ropFCOMP,       ropFCOMP,       ropFCOMP,       ropFCOMP,
/*e0*/  ropFSUBRr,      ropFSUBRr,      ropFSUBRr,      ropFSUBRr,      ropFSUBRr,      ropFSUBRr,      ropFSUBRr,      ropFSUBRr,      ropFSUBr,       ropFSUBr,       ropFSUBr,       ropFSUBr,       ropFSUBr,       ropFSUBr,       ropFSUBr,       ropFSUBr,
/*f0*/  ropFDIVRr,      ropFDIVRr,      ropFDIVRr,      ropFDIVRr,      ropFDIVRr,      ropFDIVRr,      ropFDIVRr,      ropFDIVRr,      ropFDIVr,       ropFDIVr,       ropFDIVr,       ropFDIVr,       ropFDIVr,       ropFDIVr,       ropFDIVr,       ropFDIVr,

        /*32-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,
/*10*/  ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,
/*20*/  ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,
/*30*/  ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,

/*40*/  ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,
/*50*/  ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,
/*60*/  ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,
/*70*/  ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,

/*80*/  ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFADDd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,       ropFMULd,
/*90*/  ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMd,       ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,      ropFCOMPd,
/*a0*/  ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBd,       ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,      ropFSUBRd,
/*b0*/  ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVd,       ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,      ropFDIVRd,

/*c0*/  ropFADDr,       ropFADDr,       ropFADDr,       ropFADDr,       ropFADDr,       ropFADDr,       ropFADDr,       ropFADDr,       ropFMULr,       ropFMULr,       ropFMULr,       ropFMULr,       ropFMULr,       ropFMULr,       ropFMULr,       ropFMULr,
/*d0*/  ropFCOM,        ropFCOM,        ropFCOM,        ropFCOM,        ropFCOM,        ropFCOM,        ropFCOM,        ropFCOM,        ropFCOMP,       ropFCOMP,       ropFCOMP,       ropFCOMP,       ropFCOMP,       ropFCOMP,       ropFCOMP,       ropFCOMP,
/*e0*/  ropFSUBRr,      ropFSUBRr,      ropFSUBRr,      ropFSUBRr,      ropFSUBRr,      ropFSUBRr,      ropFSUBRr,      ropFSUBRr,      ropFSUBr,       ropFSUBr,       ropFSUBr,       ropFSUBr,       ropFSUBr,       ropFSUBr,       ropFSUBr,       ropFSUBr,
/*f0*/  ropFDIVRr,      ropFDIVRr,      ropFDIVRr,      ropFDIVRr,      ropFDIVRr,      ropFDIVRr,      ropFDIVRr,      ropFDIVRr,      ropFDIVr,       ropFDIVr,       ropFDIVr,       ropFDIVr,       ropFDIVr,       ropFDIVr,       ropFDIVr,       ropFDIVr,
    // clang-format on
};

RecompOpFn recomp_opcodes_dd[512] = {
    // clang-format off
        /*16-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*10*/  ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,
/*20*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*30*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*40*/  ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*50*/  ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,
/*60*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*70*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*80*/  ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*90*/  ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,
/*a0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*b0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*c0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*d0*/  ropFST,         ropFST,         ropFST,         ropFST,         ropFST,         ropFST,         ropFST,         ropFST,         ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,
/*e0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

        /*32-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*10*/  ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,
/*20*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*30*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*40*/  ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*50*/  ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,
/*60*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*70*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*80*/  ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*90*/  ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,
/*a0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*b0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*c0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*d0*/  ropFST,         ropFST,         ropFST,         ropFST,         ropFST,         ropFST,         ropFST,         ropFST,         ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,
/*e0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
    // clang-format on
};

RecompOpFn recomp_opcodes_de[512] = {
    // clang-format off
        /*16-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,
/*10*/  ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,
/*20*/  ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,
/*30*/  ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,

/*40*/  ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,
/*50*/  ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,
/*60*/  ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,
/*70*/  ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,

/*80*/  ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,
/*90*/  ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,
/*a0*/  ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,
/*b0*/  ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,

/*c0*/  ropFADDP,       ropFADDP,       ropFADDP,       ropFADDP,       ropFADDP,       ropFADDP,       ropFADDP,       ropFADDP,       ropFMULP,       ropFMULP,       ropFMULP,       ropFMULP,       ropFMULP,       ropFMULP,       ropFMULP,       ropFMULP,
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFCOMPP,      NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  ropFSUBRP,      ropFSUBRP,      ropFSUBRP,      ropFSUBRP,      ropFSUBRP,      ropFSUBRP,      ropFSUBRP,      ropFSUBRP,      ropFSUBP,       ropFSUBP,       ropFSUBP,       ropFSUBP,       ropFSUBP,       ropFSUBP,       ropFSUBP,       ropFSUBP,
/*f0*/  ropFDIVRP,      ropFDIVRP,      ropFDIVRP,      ropFDIVRP,      ropFDIVRP,      ropFDIVRP,      ropFDIVRP,      ropFDIVRP,      ropFDIVP,       ropFDIVP,       ropFDIVP,       ropFDIVP,       ropFDIVP,       ropFDIVP,       ropFDIVP,       ropFDIVP,

        /*32-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,
/*10*/  ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,
/*20*/  ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,
/*30*/  ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,

/*40*/  ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,
/*50*/  ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,
/*60*/  ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,
/*70*/  ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,

/*80*/  ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFADDiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,      ropFMULiw,
/*90*/  ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMiw,      ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,     ropFCOMPiw,
/*a0*/  ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBiw,      ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,     ropFSUBRiw,
/*b0*/  ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIViw,      ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,     ropFDIVRiw,

/*c0*/  ropFADDP,       ropFADDP,       ropFADDP,       ropFADDP,       ropFADDP,       ropFADDP,       ropFADDP,       ropFADDP,       ropFMULP,       ropFMULP,       ropFMULP,       ropFMULP,       ropFMULP,       ropFMULP,       ropFMULP,       ropFMULP,
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFCOMPP,      NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  ropFSUBRP,      ropFSUBRP,      ropFSUBRP,      ropFSUBRP,      ropFSUBRP,      ropFSUBRP,      ropFSUBRP,      ropFSUBRP,      ropFSUBP,       ropFSUBP,       ropFSUBP,       ropFSUBP,       ropFSUBP,       ropFSUBP,       ropFSUBP,       ropFSUBP,
/*f0*/  ropFDIVRP,      ropFDIVRP,      ropFDIVRP,      ropFDIVRP,      ropFDIVRP,      ropFDIVRP,      ropFDIVRP,      ropFDIVRP,      ropFDIVP,       ropFDIVP,       ropFDIVP,       ropFDIVP,       ropFDIVP,       ropFDIVP,       ropFDIVP,       ropFDIVP,
    // clang-format on
};

RecompOpFn recomp_opcodes_df[512] = {
    // clang-format off
        /*16-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*10*/  ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,
/*20*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,
/*30*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,

/*40*/  ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*50*/  ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,
/*60*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,
/*70*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,

/*80*/  ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*90*/  ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,
/*a0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,
/*b0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,

/*c0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  ropFSTSW_AX,    NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

        /*32-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*10*/  ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,
/*20*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,
/*30*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,

/*40*/  ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*50*/  ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,
/*60*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,
/*70*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,

/*80*/  ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       ropFILDw,       NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*90*/  ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTw,       ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,      ropFISTPw,
/*a0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,       ropFILDq,
/*b0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,      ropFISTPq,

/*c0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  ropFSTSW_AX,    NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
    // clang-format on
};

RecompOpFn recomp_opcodes_REPE[512] = {
    // clang-format off
        /*16-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*10*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*20*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*30*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*40*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*50*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*60*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*70*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*80*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*90*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*a0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*b0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*c0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

        /*32-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*10*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*20*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*30*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*40*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*50*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*60*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*70*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*80*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*90*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*a0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*b0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*c0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
    // clang-format on
};

RecompOpFn recomp_opcodes_REPNE[512] = {
    // clang-format off
        /*16-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*10*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*20*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*30*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*40*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*50*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*60*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*70*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*80*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*90*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*a0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*b0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*c0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

        /*32-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*10*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*20*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*30*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*40*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*50*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*60*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*70*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*80*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*90*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*a0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*b0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*c0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
    // clang-format on
};

RecompOpFn recomp_opcodes_NULL[512] = {
    // clang-format off
        /*16-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*10*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*20*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*30*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*40*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*50*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*60*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*70*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*80*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*90*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*a0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*b0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*c0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

        /*32-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*10*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*20*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*30*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*40*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*50*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*60*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*70*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*80*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*90*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*a0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*b0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*c0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
    // clang-format on
};
