#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>

#include "codegen.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_3dnow.h"
#include "codegen_ops_arith.h"
#include "codegen_ops_branch.h"
#include "codegen_ops_fpu_arith.h"
#include "codegen_ops_fpu_constant.h"
#include "codegen_ops_fpu_loadstore.h"
#include "codegen_ops_fpu_misc.h"
#include "codegen_ops_jump.h"
#include "codegen_ops_logic.h"
#include "codegen_ops_misc.h"
#include "codegen_ops_mmx_arith.h"
#include "codegen_ops_mmx_cmp.h"
#include "codegen_ops_mmx_loadstore.h"
#include "codegen_ops_mmx_logic.h"
#include "codegen_ops_mmx_pack.h"
#include "codegen_ops_mmx_shift.h"
#include "codegen_ops_mov.h"
#include "codegen_ops_shift.h"
#include "codegen_ops_stack.h"

RecompOpFn recomp_opcodes[512] = {
    // clang-format off
        /*16-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropADD_b_rmw,   ropADD_w_rmw,   ropADD_b_rm,    ropADD_w_rm,    ropADD_AL_imm,  ropADD_AX_imm,  ropPUSH_ES_16,  ropPOP_ES_16,   ropOR_b_rmw,    ropOR_w_rmw,    ropOR_b_rm,     ropOR_w_rm,     ropOR_AL_imm,   ropOR_AX_imm,   ropPUSH_CS_16,  NULL,
/*10*/  ropADC_b_rmw,   ropADC_w_rmw,   ropADC_b_rm,    ropADC_w_rm,    ropADC_AL_imm,  ropADC_AX_imm,  ropPUSH_SS_16,  NULL,           ropSBB_b_rmw,   ropSBB_w_rmw,   ropSBB_b_rm,    ropSBB_w_rm,    ropSBB_AL_imm,  ropSBB_AX_imm,  ropPUSH_DS_16,  ropPOP_DS_16,
/*20*/  ropAND_b_rmw,   ropAND_w_rmw,   ropAND_b_rm,    ropAND_w_rm,    ropAND_AL_imm,  ropAND_AX_imm,  NULL,           NULL,           ropSUB_b_rmw,   ropSUB_w_rmw,   ropSUB_b_rm,    ropSUB_w_rm,    ropSUB_AL_imm,  ropSUB_AX_imm,  NULL,           NULL,
/*30*/  ropXOR_b_rmw,   ropXOR_w_rmw,   ropXOR_b_rm,    ropXOR_w_rm,    ropXOR_AL_imm,  ropXOR_AX_imm,  NULL,           NULL,           ropCMP_b_rmw,   ropCMP_w_rmw,   ropCMP_b_rm,    ropCMP_w_rm,    ropCMP_AL_imm,  ropCMP_AX_imm,  NULL,           NULL,

/*40*/  ropINC_r16,     ropINC_r16,     ropINC_r16,     ropINC_r16,     ropINC_r16,     ropINC_r16,     ropINC_r16,     ropINC_r16,     ropDEC_r16,     ropDEC_r16,     ropDEC_r16,     ropDEC_r16,     ropDEC_r16,     ropDEC_r16,     ropDEC_r16,     ropDEC_r16,
/*50*/  ropPUSH_r16,    ropPUSH_r16,    ropPUSH_r16,    ropPUSH_r16,    ropPUSH_r16,    ropPUSH_r16,    ropPUSH_r16,    ropPUSH_r16,    ropPOP_r16,     ropPOP_r16,     ropPOP_r16,     ropPOP_r16,     ropPOP_r16,     ropPOP_r16,     ropPOP_r16,     ropPOP_r16,
/*60*/  ropPUSHA_16,    ropPOPA_16,     NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropPUSH_imm_16, NULL,           ropPUSH_imm_16_8,NULL,           NULL,           NULL,           NULL,           NULL,
/*70*/  ropJO_8,        ropJNO_8,       ropJB_8,        ropJNB_8,       ropJE_8,        ropJNE_8,       ropJBE_8,       ropJNBE_8,      ropJS_8,        ropJNS_8,       ropJP_8,        ropJNP_8,       ropJL_8,        ropJNL_8,       ropJLE_8,       ropJNLE_8,

/*80*/  rop80,          rop81_w,        rop80,          rop83_w,        ropTEST_b_rm,   ropTEST_w_rm,   ropXCHG_8,      ropXCHG_16,     ropMOV_b_r,     ropMOV_w_r,     ropMOV_r_b,     ropMOV_r_w,     ropMOV_w_seg,   ropLEA_16,      ropMOV_seg_w,   ropPOP_W,
/*90*/  ropNOP,         ropXCHG_AX,     ropXCHG_AX,     ropXCHG_AX,     ropXCHG_AX,     ropXCHG_AX,     ropXCHG_AX,     ropXCHG_AX,     ropCBW,         ropCWD,         NULL,           NULL,           ropPUSHF,       NULL,           NULL,           NULL,
/*a0*/  ropMOV_AL_abs,  ropMOV_AX_abs,  ropMOV_abs_AL,  ropMOV_abs_AX,  NULL,           NULL,           NULL,           NULL,           ropTEST_AL_imm, ropTEST_AX_imm, NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*b0*/  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rw_imm,  ropMOV_rw_imm,  ropMOV_rw_imm,  ropMOV_rw_imm,  ropMOV_rw_imm,  ropMOV_rw_imm,  ropMOV_rw_imm,  ropMOV_rw_imm,

/*c0*/  ropC0,          ropC1_w,        ropRET_imm_16,  ropRET_16,      ropLES_16,      ropLDS_16,      ropMOV_b_imm,   ropMOV_w_imm,   NULL,           ropLEAVE_16,    ropRETF_imm_16, ropRETF_16,     NULL,           NULL,           NULL,           NULL,
/*d0*/  ropD0,          ropD1_w,        ropD2,          ropD3_w,        NULL,           NULL,           NULL,           ropXLAT,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  ropLOOPNE,      ropLOOPE,       ropLOOP,        ropJCXZ,        NULL,           NULL,           NULL,           NULL,           ropCALL_r16,    ropJMP_r16,     ropJMP_far_16,  ropJMP_r8,      NULL,           NULL,           NULL,           NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           ropCMC,         ropF6,          ropF7_16,       ropCLC,         ropSTC,         ropCLI,         ropSTI,         ropCLD,         ropSTD,         ropINCDEC,      ropFF_16,

        /*32-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropADD_b_rmw,   ropADD_l_rmw,   ropADD_b_rm,    ropADD_l_rm,    ropADD_AL_imm,  ropADD_EAX_imm, ropPUSH_ES_32,  ropPOP_ES_32,   ropOR_b_rmw,    ropOR_l_rmw,    ropOR_b_rm,     ropOR_l_rm,     ropOR_AL_imm,   ropOR_EAX_imm,  ropPUSH_CS_32,  NULL,
/*10*/  ropADC_b_rmw,   ropADC_l_rmw,   ropADC_b_rm,    ropADC_l_rm,    ropADC_AL_imm,  ropADC_EAX_imm, ropPUSH_SS_32,  NULL,           ropSBB_b_rmw,   ropSBB_l_rmw,   ropSBB_b_rm,    ropSBB_l_rm,    ropSBB_AL_imm,  ropSBB_EAX_imm, ropPUSH_DS_32,  ropPOP_DS_32,
/*20*/  ropAND_b_rmw,   ropAND_l_rmw,   ropAND_b_rm,    ropAND_l_rm,    ropAND_AL_imm,  ropAND_EAX_imm, NULL,           NULL,           ropSUB_b_rmw,   ropSUB_l_rmw,   ropSUB_b_rm,    ropSUB_l_rm,    ropSUB_AL_imm,  ropSUB_EAX_imm, NULL,           NULL,
/*30*/  ropXOR_b_rmw,   ropXOR_l_rmw,   ropXOR_b_rm,    ropXOR_l_rm,    ropXOR_AL_imm,  ropXOR_EAX_imm, NULL,           NULL,           ropCMP_b_rmw,   ropCMP_l_rmw,   ropCMP_b_rm,    ropCMP_l_rm,    ropCMP_AL_imm,  ropCMP_EAX_imm, NULL,           NULL,

/*40*/  ropINC_r32,     ropINC_r32,     ropINC_r32,     ropINC_r32,     ropINC_r32,     ropINC_r32,     ropINC_r32,     ropINC_r32,     ropDEC_r32,     ropDEC_r32,     ropDEC_r32,     ropDEC_r32,     ropDEC_r32,     ropDEC_r32,     ropDEC_r32,     ropDEC_r32,
/*50*/  ropPUSH_r32,    ropPUSH_r32,    ropPUSH_r32,    ropPUSH_r32,    ropPUSH_r32,    ropPUSH_r32,    ropPUSH_r32,    ropPUSH_r32,    ropPOP_r32,     ropPOP_r32,     ropPOP_r32,     ropPOP_r32,     ropPOP_r32,     ropPOP_r32,     ropPOP_r32,     ropPOP_r32,
/*60*/  ropPUSHA_32,    ropPOPA_32,     NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropPUSH_imm_32, NULL,           ropPUSH_imm_32_8,NULL,           NULL,           NULL,           NULL,           NULL,
/*70*/  ropJO_8,        ropJNO_8,       ropJB_8,        ropJNB_8,       ropJE_8,        ropJNE_8,       ropJBE_8,       ropJNBE_8,      ropJS_8,        ropJNS_8,       ropJP_8,        ropJNP_8,       ropJL_8,        ropJNL_8,       ropJLE_8,       ropJNLE_8,

/*80*/  rop80,          rop81_l,        rop80,          rop83_l,        ropTEST_b_rm,   ropTEST_l_rm,   ropXCHG_8,      ropXCHG_32,     ropMOV_b_r,     ropMOV_l_r,     ropMOV_r_b,     ropMOV_r_l,     ropMOV_l_seg,   ropLEA_32,      ropMOV_seg_w,   ropPOP_L,
/*90*/  ropNOP,         ropXCHG_EAX,    ropXCHG_EAX,    ropXCHG_EAX,    ropXCHG_EAX,    ropXCHG_EAX,    ropXCHG_EAX,    ropXCHG_EAX,    ropCWDE,        ropCDQ,         NULL,           NULL,           ropPUSHFD,      NULL,           NULL,           NULL,
/*a0*/  ropMOV_AL_abs,  ropMOV_EAX_abs, ropMOV_abs_AL,  ropMOV_abs_EAX, NULL,           NULL,           NULL,           NULL,           ropTEST_AL_imm, ropTEST_EAX_imm,NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*b0*/  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rb_imm,  ropMOV_rl_imm,  ropMOV_rl_imm,  ropMOV_rl_imm,  ropMOV_rl_imm,  ropMOV_rl_imm,  ropMOV_rl_imm,  ropMOV_rl_imm,  ropMOV_rl_imm,

/*c0*/  ropC0,          ropC1_l,        ropRET_imm_32,  ropRET_32,      ropLES_32,      ropLDS_32,      ropMOV_b_imm,   ropMOV_l_imm,   NULL,           ropLEAVE_32,    ropRETF_imm_32, ropRETF_32,     NULL,           NULL,           NULL,           NULL,
/*d0*/  ropD0,          ropD1_l,        ropD2,          ropD3_l,        NULL,           NULL,           NULL,           ropXLAT,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  ropLOOPNE,      ropLOOPE,       ropLOOP,        ropJCXZ,        NULL,           NULL,           NULL,           NULL,           ropCALL_r32,    ropJMP_r32,     ropJMP_far_32,  ropJMP_r8,      NULL,           NULL,           NULL,           NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           ropCMC,         ropF6,          ropF7_32,       ropCLC,         ropSTC,         ropCLI,         ropSTI,         ropCLD,         ropSTD,         ropINCDEC,      ropFF_32
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
#if defined __ARM_EABI__ || defined _ARM_ || defined _M_ARM || defined __aarch64__ || defined _M_ARM64
/*60*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*70*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
#else
/*60*/  ropPUNPCKLBW,   ropPUNPCKLWD,   ropPUNPCKLDQ,   ropPACKSSWB,    ropPCMPGTB,     ropPCMPGTW,     ropPCMPGTD,     ropPACKUSWB,    ropPUNPCKHBW,   ropPUNPCKHWD,   ropPUNPCKHDQ,   ropPACKSSDW,    NULL,           NULL,           ropMOVD_r_d,    ropMOVQ_r_q,
/*70*/  NULL,           ropPSxxW_imm,   ropPSxxD_imm,   ropPSxxQ_imm,   ropPCMPEQB,     ropPCMPEQW,     ropPCMPEQD,     NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropMOVD_d_r,    ropMOVQ_q_r,
#endif

/*80*/  ropJO_16,       ropJNO_16,      ropJB_16,       ropJNB_16,      ropJE_16,       ropJNE_16,      ropJBE_16,      ropJNBE_16,     ropJS_16,       ropJNS_16,      ropJP_16,       ropJNP_16,      ropJL_16,       ropJNL_16,      ropJLE_16,      ropJNLE_16,
/*90*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*a0*/  ropPUSH_FS_16,  ropPOP_FS_16,   NULL,           NULL,           ropSHLD_16_imm, NULL,           NULL,           NULL,           ropPUSH_GS_16,  ropPOP_GS_16,   NULL,           NULL,           ropSHRD_16_imm, NULL,           NULL,           NULL,
/*b0*/  NULL,           NULL,           ropLSS_16,      NULL,           ropLFS_16,      ropLGS_16,      ropMOVZX_16_8,  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropMOVSX_16_8,  NULL,

/*c0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
#if defined __ARM_EABI__ || defined _ARM_ || defined _M_ARM || defined __aarch64__ || defined _M_ARM64
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
#else
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           ropPMULLW,      NULL,           NULL,           ropPSUBUSB,     ropPSUBUSW,     NULL,           ropPAND,        ropPADDUSB,     ropPADDUSW,     NULL,           ropPANDN,
/*e0*/  NULL,           NULL,           NULL,           NULL,           NULL,           ropPMULHW,      NULL,           NULL,           ropPSUBSB,      ropPSUBSW,      NULL,           ropPOR,         ropPADDSB,      ropPADDSW,      NULL,           ropPXOR,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           ropPMADDWD,     NULL,           NULL,           ropPSUBB,       ropPSUBW,       ropPSUBD,       NULL,           ropPADDB,       ropPADDW,       ropPADDD,       NULL,
#endif

        /*32-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*10*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*20*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*30*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*40*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*50*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
#if defined __ARM_EABI__ || defined _ARM_ || defined _M_ARM || defined __aarch64__ || defined _M_ARM64
/*60*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*70*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
#else
/*60*/  ropPUNPCKLBW,   ropPUNPCKLWD,   ropPUNPCKLDQ,   ropPACKSSWB,    ropPCMPGTB,     ropPCMPGTW,     ropPCMPGTD,     ropPACKUSWB,    ropPUNPCKHBW,   ropPUNPCKHWD,   ropPUNPCKHDQ,   ropPACKSSDW,    NULL,           NULL,           ropMOVD_r_d,    ropMOVQ_r_q,
/*70*/  NULL,           ropPSxxW_imm,   ropPSxxD_imm,   ropPSxxQ_imm,   ropPCMPEQB,     ropPCMPEQW,     ropPCMPEQD,     NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropMOVD_d_r,    ropMOVQ_q_r,
#endif

/*80*/  ropJO_32,       ropJNO_32,      ropJB_32,       ropJNB_32,      ropJE_32,       ropJNE_32,      ropJBE_32,      ropJNBE_32,     ropJS_32,       ropJNS_32,      ropJP_32,       ropJNP_32,      ropJL_32,       ropJNL_32,      ropJLE_32,      ropJNLE_32,
/*90*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*a0*/  ropPUSH_FS_32,  ropPOP_FS_32,   NULL,           NULL,           ropSHLD_32_imm, NULL,           NULL,           NULL,           ropPUSH_GS_32,  ropPOP_GS_32,   NULL,           NULL,           ropSHRD_32_imm, NULL,           NULL,           NULL,
/*b0*/  NULL,           NULL,           ropLSS_32,      NULL,           ropLFS_32,      ropLGS_32,      ropMOVZX_32_8,  ropMOVZX_32_16, NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropMOVSX_32_8,  ropMOVSX_32_16,

/*c0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
#if defined __ARM_EABI__ || defined _ARM_ || defined _M_ARM || defined __aarch64__ || defined _M_ARM64
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
#else
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           ropPMULLW,      NULL,           NULL,           ropPSUBUSB,     ropPSUBUSW,     NULL,           ropPAND,        ropPADDUSB,     ropPADDUSW,     NULL,           ropPANDN,
/*e0*/  NULL,           NULL,           NULL,           NULL,           NULL,           ropPMULHW,      NULL,           NULL,           ropPSUBSB,      ropPSUBSW,      NULL,           ropPOR,         ropPADDSB,      ropPADDSW,      NULL,           ropPXOR,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           ropPMADDWD,     NULL,           NULL,           ropPSUBB,       ropPSUBW,       ropPSUBD,       NULL,           ropPADDB,       ropPADDW,       ropPADDD,       NULL,
#endif
    // clang-format on
};

RecompOpFn recomp_opcodes_3DNOW[256] = {
// clang-format off
#if defined __ARM_EABI__ || defined _ARM_ || defined _M_ARM || defined __aarch64__ || defined _M_ARM64
0
#else
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropPI2FD,       NULL,           NULL,
/*10*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropPF2ID,       NULL,           NULL,
/*20*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*30*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*40*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*50*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*60*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*70*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*80*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*90*/  ropPFCMPGE,     NULL,           NULL,           NULL,           ropPFMIN,       NULL,           ropPFRCP,       ropPFRSQRT,     NULL,           NULL,           ropPFSUB,       NULL,           NULL,           NULL,           ropPFADD,       NULL,
/*a0*/  ropPFCMPGT,     NULL,           NULL,           NULL,           ropPFMAX,       NULL,           ropPFRCPIT,     ropPFRSQIT1,    NULL,           NULL,           ropPFSUBR,      NULL,           NULL,           NULL,           NULL,           NULL,
/*b0*/  ropPFCMPEQ,     NULL,           NULL,           NULL,           ropPFMUL,       NULL,           ropPFRCPIT,     NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*c0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
#endif
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
/*20*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*30*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,

/*40*/  ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*50*/  ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,
/*60*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*70*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,

/*80*/  ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*90*/  ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,
/*a0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*b0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,

/*c0*/  ropFLD,         ropFLD,         ropFLD,         ropFLD,         ropFLD,         ropFLD,         ropFLD,         ropFLD,         ropFXCH,        ropFXCH,        ropFXCH,        ropFXCH,        ropFXCH,        ropFXCH,        ropFXCH,        ropFXCH,
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,
/*e0*/  ropFCHS,        ropFABS,        NULL,           NULL,           ropFTST,        NULL,           NULL,           NULL,           ropFLD1,        NULL,           NULL,           NULL,           NULL,           NULL,           ropFLDZ,        NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFSQRT,       NULL,           NULL,           NULL,           NULL,           NULL,

        /*32-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*10*/  ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,
/*20*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*30*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,

/*40*/  ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*50*/  ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,
/*60*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*70*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,

/*80*/  ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        ropFLDs,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*90*/  ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTs,        ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,       ropFSTPs,
/*a0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*b0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,       ropFSTCW,

/*c0*/  ropFLD,         ropFLD,         ropFLD,         ropFLD,         ropFLD,         ropFLD,         ropFLD,         ropFLD,         ropFXCH,        ropFXCH,        ropFXCH,        ropFXCH,        ropFXCH,        ropFXCH,        ropFXCH,        ropFXCH,
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,
/*e0*/  ropFCHS,        ropFABS,        NULL,           NULL,           ropFTST,        NULL,           NULL,           NULL,           ropFLD1,        NULL,           NULL,           NULL,           NULL,           NULL,           ropFLDZ,        NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFSQRT,       NULL,           NULL,           NULL,           NULL,           NULL,
    // clang-format on
};

RecompOpFn recomp_opcodes_da[512] = {
    // clang-format off
        /*16-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,
/*10*/  ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,
/*20*/  ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,
/*30*/  ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,

/*40*/  ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,
/*50*/  ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,
/*60*/  ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,
/*70*/  ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,

/*80*/  ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,
/*90*/  ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,
/*a0*/  ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,
/*b0*/  ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,

/*c0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFUCOMPP,     NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

        /*32-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,
/*10*/  ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,
/*20*/  ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,
/*30*/  ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,

/*40*/  ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,
/*50*/  ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,
/*60*/  ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,
/*70*/  ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,

/*80*/  ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIADDl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,      ropFIMULl,
/*90*/  ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMl,      ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,     ropFICOMPl,
/*a0*/  ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBl,      ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,     ropFISUBRl,
/*b0*/  ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVl,      ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,     ropFIDIVRl,

/*c0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFUCOMPP,     NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
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
/*30*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,

/*40*/  ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*50*/  ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,
/*60*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*70*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,

/*80*/  ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*90*/  ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,
/*a0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*b0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,

/*c0*/  ropFFREE,       ropFFREE,       ropFFREE,       ropFFREE,       ropFFREE,       ropFFREE,       ropFFREE,       ropFFREE,       NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*d0*/  ropFST,         ropFST,         ropFST,         ropFST,         ropFST,         ropFST,         ropFST,         ropFST,         ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,
/*e0*/  ropFUCOM,       ropFUCOM,       ropFUCOM,       ropFUCOM,       ropFUCOM,       ropFUCOM,       ropFUCOM,       ropFUCOM,       ropFUCOMP,      ropFUCOMP,      ropFUCOMP,      ropFUCOMP,      ropFUCOMP,      ropFUCOMP,      ropFUCOMP,      ropFUCOMP,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

        /*32-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*10*/  ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,
/*20*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*30*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,

/*40*/  ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*50*/  ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,
/*60*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*70*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,

/*80*/  ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        ropFLDd,        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*90*/  ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTd,        ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,       ropFSTPd,
/*a0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*b0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,       ropFSTSW,

/*c0*/  ropFFREE,       ropFFREE,       ropFFREE,       ropFFREE,       ropFFREE,       ropFFREE,       ropFFREE,       ropFFREE,       NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*d0*/  ropFST,         ropFST,         ropFST,         ropFST,         ropFST,         ropFST,         ropFST,         ropFST,         ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,        ropFSTP,
/*e0*/  ropFUCOM,       ropFUCOM,       ropFUCOM,       ropFUCOM,       ropFUCOM,       ropFUCOM,       ropFUCOM,       ropFUCOM,       ropFUCOMP,      ropFUCOMP,      ropFUCOMP,      ropFUCOMP,      ropFUCOMP,      ropFUCOMP,      ropFUCOMP,      ropFUCOMP,
/*f0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
    // clang-format on
};

RecompOpFn recomp_opcodes_de[512] = {
    // clang-format off
        /*16-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,
/*10*/  ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,
/*20*/  ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,
/*30*/  ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,

/*40*/  ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,
/*50*/  ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,
/*60*/  ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,
/*70*/  ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,

/*80*/  ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,
/*90*/  ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,
/*a0*/  ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,
/*b0*/  ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,

/*c0*/  ropFADDP,       ropFADDP,       ropFADDP,       ropFADDP,       ropFADDP,       ropFADDP,       ropFADDP,       ropFADDP,       ropFMULP,       ropFMULP,       ropFMULP,       ropFMULP,       ropFMULP,       ropFMULP,       ropFMULP,       ropFMULP,
/*d0*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           ropFCOMPP,      NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  ropFSUBRP,      ropFSUBRP,      ropFSUBRP,      ropFSUBRP,      ropFSUBRP,      ropFSUBRP,      ropFSUBRP,      ropFSUBRP,      ropFSUBP,       ropFSUBP,       ropFSUBP,       ropFSUBP,       ropFSUBP,       ropFSUBP,       ropFSUBP,       ropFSUBP,
/*f0*/  ropFDIVRP,      ropFDIVRP,      ropFDIVRP,      ropFDIVRP,      ropFDIVRP,      ropFDIVRP,      ropFDIVRP,      ropFDIVRP,      ropFDIVP,       ropFDIVP,       ropFDIVP,       ropFDIVP,       ropFDIVP,       ropFDIVP,       ropFDIVP,       ropFDIVP,

        /*32-bit data*/
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,
/*10*/  ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,
/*20*/  ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,
/*30*/  ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,

/*40*/  ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,
/*50*/  ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,
/*60*/  ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,
/*70*/  ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,

/*80*/  ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIADDw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,      ropFIMULw,
/*90*/  ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMw,      ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,     ropFICOMPw,
/*a0*/  ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBw,      ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,     ropFISUBRw,
/*b0*/  ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVw,      ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,     ropFIDIVRw,

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

