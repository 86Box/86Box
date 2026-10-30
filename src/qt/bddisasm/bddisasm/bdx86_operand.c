/*
 * Copyright (c) 2025 Bitdefender
 * SPDX-License-Identifier: Apache-2.0
 */
#include "include/bddisasm_crt.h"
#include "../inc/bddisasm.h"

// The table definitions.
#include "include/bdx86_tabledefs.h"


//
// NdGetSegOverride
//
static ND_UINT8
NdGetSegOverride(
    const INSTRUX_MINI *Instrux,
    ND_UINT8 DefaultSeg
)
{
    // Return default seg, if no override present.
    if (Instrux->Seg == 0)
    {
        return DefaultSeg;
    }

    // In 64 bit mode, the segment override is ignored, except for FS and GS.
    if ((Instrux->DefCode == ND_CODE_64) &&
        (Instrux->Seg != ND_PREFIX_G2_SEG_FS) &&
        (Instrux->Seg != ND_PREFIX_G2_SEG_GS))
    {
        return DefaultSeg;
    }

    switch (Instrux->Seg)
    {
    case ND_PREFIX_G2_SEG_CS:
        return NDR_CS;
    case ND_PREFIX_G2_SEG_DS:
        return NDR_DS;
    case ND_PREFIX_G2_SEG_ES:
        return NDR_ES;
    case ND_PREFIX_G2_SEG_SS:
        return NDR_SS;
    case ND_PREFIX_G2_SEG_FS:
        return NDR_FS;
    case ND_PREFIX_G2_SEG_GS:
        return NDR_GS;
    default:
        return DefaultSeg;
    }
}


//
// NdGetCompDispSize
//
static ND_UINT8
NdGetCompDispSize(
    const INSTRUX_MINI *Instrux,
    const ND_IDBE *Idbe,
    ND_UINT32 MemSize
)
{
    static const ND_UINT8 fvszLut[4] =  { 16, 32, 64, 1  };
    static const ND_UINT8 hvszLut[4] =  { 8,  16, 32, 1  };
    static const ND_UINT8 qvszLut[4] =  { 4,  8,  16, 1  };
    static const ND_UINT8 dupszLut[4] = { 8,  32, 64, 1  };
    static const ND_UINT8 fvmszLut[4] = { 16, 32, 64, 1  };
    static const ND_UINT8 hvmszLut[4] = { 8,  16, 32, 1  };
    static const ND_UINT8 qvmszLut[4] = { 4,  8,  16, 1  };
    static const ND_UINT8 ovmszLut[4] = { 2,  4,  8,  1  };

    if (Instrux->HasBroadcast)
    {
        // If the instruction uses broadcast, then compressed displacement will use the size of the element as scale:
        // - 2 when broadcasting 16 bit
        // - 4 when broadcasting 32 bit
        // - 8 when broadcasting 64 bit
        return (ND_UINT8)MemSize;
    }

    switch (Idbe->TupleType)
    {
    case ND_TUPLE_FV:
        return fvszLut[Instrux->Exs.l];
    case ND_TUPLE_HV:
        return hvszLut[Instrux->Exs.l];
    case ND_TUPLE_QV:
        return qvszLut[Instrux->Exs.l];
    case ND_TUPLE_DUP:
        return dupszLut[Instrux->Exs.l];
    case ND_TUPLE_FVM:
        return fvmszLut[Instrux->Exs.l];
    case ND_TUPLE_HVM:
        return hvmszLut[Instrux->Exs.l];
    case ND_TUPLE_QVM:
        return qvmszLut[Instrux->Exs.l];
    case ND_TUPLE_OVM:
        return ovmszLut[Instrux->Exs.l];
    case ND_TUPLE_M128:
        return 16;
    case ND_TUPLE_T1S8:
        return 1;
    case ND_TUPLE_T1S16:
        return 2;
    case ND_TUPLE_T1S:
        return !!(Idbe->Attributes & ND_FLAG_WIG) ? 4 : Instrux->Exs.w ? 8 : 4;
    case ND_TUPLE_T1F:
        return (ND_UINT8)MemSize;
    case ND_TUPLE_T2:
        return Instrux->Exs.w ? 16 : 8;
    case ND_TUPLE_T4:
        return Instrux->Exs.w ? 32 : 16;
    case ND_TUPLE_T8:
        return 32;
    case ND_TUPLE_T1_4X:
        return 16;
    default:
        // Default - we assume byte granularity for memory accesses, therefore, no scaling will be done.
        return 1;
    }
}


//
// NdParseMemoryOperand16
//
static NDSTATUS
NdParseMemoryOperand16(
    const INSTRUX_MINI *Instrux,
    ND_OPERAND *Operand
)
{
    switch (Instrux->ModRm.rm)
    {
    case 0:
        // [bx + si]
        Operand->Info.Memory.HasBase = ND_TRUE;
        Operand->Info.Memory.HasIndex = ND_TRUE;
        Operand->Info.Memory.Scale = 1;
        Operand->Info.Memory.Base = NDR_BX;
        Operand->Info.Memory.Index = NDR_SI;
        Operand->Info.Memory.BaseSize = ND_SIZE_16BIT;
        Operand->Info.Memory.IndexSize = ND_SIZE_16BIT;
        Operand->Info.Memory.Seg = NDR_DS;
        break;
    case 1:
        // [bx + di]
        Operand->Info.Memory.HasBase = ND_TRUE;
        Operand->Info.Memory.HasIndex = ND_TRUE;
        Operand->Info.Memory.Scale = 1;
        Operand->Info.Memory.Base = NDR_BX;
        Operand->Info.Memory.Index = NDR_DI;
        Operand->Info.Memory.BaseSize = ND_SIZE_16BIT;
        Operand->Info.Memory.IndexSize = ND_SIZE_16BIT;
        Operand->Info.Memory.Seg = NDR_DS;
        break;
    case 2:
        // [bp + si]
        Operand->Info.Memory.HasBase = ND_TRUE;
        Operand->Info.Memory.HasIndex = ND_TRUE;
        Operand->Info.Memory.Scale = 1;
        Operand->Info.Memory.Base = NDR_BP;
        Operand->Info.Memory.Index = NDR_SI;
        Operand->Info.Memory.BaseSize = ND_SIZE_16BIT;
        Operand->Info.Memory.IndexSize = ND_SIZE_16BIT;
        Operand->Info.Memory.Seg = NDR_SS;
        break;
    case 3:
        // [bp + di]
        Operand->Info.Memory.HasBase = ND_TRUE;
        Operand->Info.Memory.HasIndex = ND_TRUE;
        Operand->Info.Memory.Scale = 1;
        Operand->Info.Memory.Base = NDR_BP;
        Operand->Info.Memory.Index = NDR_DI;
        Operand->Info.Memory.BaseSize = ND_SIZE_16BIT;
        Operand->Info.Memory.IndexSize = ND_SIZE_16BIT;
        Operand->Info.Memory.Seg = NDR_SS;
        break;
    case 4:
        // [si]
        Operand->Info.Memory.HasBase = ND_TRUE;
        Operand->Info.Memory.Base = NDR_SI;
        Operand->Info.Memory.BaseSize = ND_SIZE_16BIT;
        Operand->Info.Memory.Seg = NDR_DS;
        break;
    case 5:
        // [di]
        Operand->Info.Memory.HasBase = ND_TRUE;
        Operand->Info.Memory.Base = NDR_DI;
        Operand->Info.Memory.BaseSize = ND_SIZE_16BIT;
        Operand->Info.Memory.Seg = NDR_DS;
        break;
    case 6:
        // [bp]
        if (Instrux->ModRm.mod != 0)
        {
            // If mod is not zero, than we have "[bp + displacement]".
            Operand->Info.Memory.HasBase = ND_TRUE;
            Operand->Info.Memory.Base = NDR_BP;
            Operand->Info.Memory.BaseSize = ND_SIZE_16BIT;
            Operand->Info.Memory.Seg = NDR_SS;
        }
        else
        {
            // If mod is zero, than we only have a displacement that is used to directly address mem.
            Operand->Info.Memory.Seg = NDR_DS;
        }
        break;
    case 7:
        // [bx]
        Operand->Info.Memory.HasBase = ND_TRUE;
        Operand->Info.Memory.Base = NDR_BX;
        Operand->Info.Memory.BaseSize = ND_SIZE_16BIT;
        Operand->Info.Memory.Seg = NDR_DS;
        break;
    }

    // Store the displacement.
    Operand->Info.Memory.HasDisp = !!Instrux->HasDisp;
    Operand->Info.Memory.DispSize = Instrux->DispLength;
    Operand->Info.Memory.Disp = Instrux->HasDisp ? ND_SIGN_EX(Instrux->DispLength, Instrux->Displacement) : 0;

    return ND_STATUS_SUCCESS;
}


//
// NdParseMemoryOperand3264
//
static NDSTATUS
NdParseMemoryOperand3264(
    const INSTRUX_MINI *Instrux,
    const ND_IDBE *Idbe,
    ND_OPERAND *Operand,
    ND_REG_SIZE VsibRegSize
)
{
    ND_UINT8 defsize = (Instrux->AddrMode == ND_ADDR_32 ? ND_SIZE_32BIT : ND_SIZE_64BIT);

    // Implicit segment is DS.
    Operand->Info.Memory.Seg = NDR_DS;

    if (Instrux->HasSib)
    {
        // Check for base.
        if ((Instrux->ModRm.mod == 0) && (Instrux->Sib.base == NDR_RBP))
        {
            // Mod is mem without displacement and base reg is RBP -> no base reg used.
            // Note that this addressing mode is not RIP relative.
        }
        else
        {
            Operand->Info.Memory.HasBase = ND_TRUE;
            Operand->Info.Memory.BaseSize = defsize;
            Operand->Info.Memory.Base = ND_DECODE_GPR_B(Instrux);

            if ((Operand->Info.Memory.Base == NDR_RSP) || 
                (Operand->Info.Memory.Base == NDR_RBP))
            {
                Operand->Info.Memory.Seg = NDR_SS;
            }
        }

        // Check for index.
        if (Idbe->Attributes & ND_FLAG_VSIB)
        {
            // With VSIB, the index reg can be 4 (RSP equivalent). Bit 4 of the 32-bit index register is given by the
            // EVEX.V' field.
            Operand->Info.Memory.HasIndex = ND_TRUE;
            Operand->Info.Memory.IndexSize = (ND_UINT8)VsibRegSize;
            Operand->Info.Memory.Index = ND_DECODE_VEC_X(Instrux);
            Operand->Info.Memory.Scale = 1 << Instrux->Sib.scale;
        }
        else
        {
            // Regular SIB, index RSP is ignored. Bit 4 of the 32-bit index register is given by the X4 field.
            Operand->Info.Memory.Index = ND_DECODE_GPR_X(Instrux);

            if (Operand->Info.Memory.Index != NDR_RSP)
            {
                // Index * Scale is present.
                Operand->Info.Memory.HasIndex = ND_TRUE;
                Operand->Info.Memory.IndexSize = defsize;
                Operand->Info.Memory.Scale = 1 << Instrux->Sib.scale;
            }
        }
    }
    else
    {
        if ((Instrux->ModRm.mod == 0) && (Instrux->ModRm.rm == NDR_RBP))
        {
            //
            // RIP relative addressing addresses a memory region relative to the current RIP; However,
            // the current RIP, when executing the current instruction, is already updated and points
            // to the next instruction, therefore, we must add the instruction length also to the final
            // address. Note that RIP relative addressing is used even if the instruction uses 32 bit
            // addressing, as long as we're in long mode.
            //
            Operand->Info.Memory.IsRipRel = Instrux->IsRipRelative ? 1 : 0;
        }
        else
        {
            Operand->Info.Memory.HasBase = ND_TRUE;
            Operand->Info.Memory.BaseSize = defsize;
            Operand->Info.Memory.Base = ND_DECODE_GPR_M(Instrux);

            if ((Operand->Info.Memory.Base == NDR_RSP) || 
                (Operand->Info.Memory.Base == NDR_RBP))
            {
                Operand->Info.Memory.Seg = NDR_SS;
            }
        }
    }

    Operand->Info.Memory.HasDisp = Instrux->HasDisp ? 1 : 0;
    Operand->Info.Memory.DispSize = Instrux->DispLength;
    Operand->Info.Memory.Disp = Instrux->HasDisp ? ND_SIGN_EX(Instrux->DispLength, Instrux->Displacement) : 0;

    return ND_STATUS_SUCCESS;
}



//
// NdParseOperand
//
NDSTATUS
NdParseOperand(
    const INSTRUX_MINI *Instrux,
    const ND_IDBE *Idbe,
    ND_OPERAND *Operand,
    ND_UINT64 Specifier
)
{
    NDSTATUS status;
    PND_OPERAND operand;
    ND_UINT8 opt, ops, opf, opa, opd, opb;
    ND_REG_SIZE vsibRegSize;
    ND_UINT8 vsibIndexSize, vsibIndexCount;
    ND_OPERAND_SIZE size, bcstSize;
    ND_BOOL width;

    // pre-init
    status = ND_STATUS_SUCCESS;
    vsibRegSize = 0;
    vsibIndexSize = vsibIndexCount = 0;
    size = bcstSize = 0;

    // Make sure the operand exists.
    if (Specifier == 0)
    {
        return ND_STATUS_HINT_OPERAND_NOT_PRESENT;
    }

    // Get actual width.
    width = Instrux->Exs.w && !(Idbe->Attributes & ND_FLAG_WIG);

    // Get operand components.
    opt = ND_OP_TYPE(Specifier);
    ops = ND_OP_SIZE(Specifier);
    opf = ND_OP_FLAGS(Specifier);
    opa = ND_OP_ACCESS(Specifier);
    opd = ND_OP_DECORATORS(Specifier);
    opb = ND_OP_BLOCK(Specifier);

    // Get a pointer to our op.
    operand = Operand;

    // Fill in the flags.
    operand->Flags.Flags = opf;

    // Store operand access modes.
    operand->Access.Access = opa;

    // Implicit operand access, by default.
    operand->Encoding = ND_OPE_S;


    //
    // Fill in operand size.
    //
    switch (ops)
    {
    case ND_OPS_asz:
        // Size given by the address mode.
        size = 2 << Instrux->AddrMode;
        break;

    case ND_OPS_ssz:
        // Size given by the stack mode.
        size = 2 << Instrux->DefStack;
        break;

    case ND_OPS_0:
        // No memory access. 0 operand size.
        size = 0;
        break;

    case ND_OPS_b:
        // 8 bits.
        size = ND_SIZE_8BIT;
        break;

    case ND_OPS_w:
        // 16 bits.
        size = ND_SIZE_16BIT;
        break;

    case ND_OPS_d:
        // 32 bits.
        size = ND_SIZE_32BIT;
        break;

    case ND_OPS_q:
        // 64 bits.
        size = ND_SIZE_64BIT;
        break;

    case ND_OPS_dq:
        // 128 bits. 
        size = ND_SIZE_128BIT;
        break;

    case ND_OPS_qq:
        // 256 bits.
        size = ND_SIZE_256BIT;
        break;

    case ND_OPS_oq:
        // 512 bits.
        size = ND_SIZE_512BIT;
        break;

    case ND_OPS_fa:
        // 80 bits packed BCD.
        size = ND_SIZE_80BIT;
        break;

    case ND_OPS_fw:
        // 16 bits real number.
        size = ND_SIZE_16BIT;
        break;

    case ND_OPS_fd:
        // 32 bits real number.
        size = ND_SIZE_32BIT;
        break;

    case ND_OPS_fq:
        // 64 bits real number.
        size = ND_SIZE_64BIT;
        break;

    case ND_OPS_ft:
        // 80 bits real number.
        size = ND_SIZE_80BIT;
        break;

    case ND_OPS_fe:
        // 14 bytes or 28 bytes FPU environment.
        size = (Instrux->EfOpMode == ND_OPSZ_16) ? ND_SIZE_112BIT : ND_SIZE_224BIT;
        break;

    case ND_OPS_fs:
        // 94 bytes or 108 bytes FPU state.
        size = (Instrux->EfOpMode == ND_OPSZ_16) ? ND_SIZE_752BIT : ND_SIZE_864BIT;
        break;

    case ND_OPS_rx:
        // 512 bytes extended state.
        size = ND_SIZE_4096BIT;
        break;

    case ND_OPS_cl:
        // The size of one cache line.
        size = ND_SIZE_CACHE_LINE;
        break;

    case ND_OPS_v:
        // 16, 32 or 64 bits.
        size = 2 << Instrux->EfOpMode;
        break;

    case ND_OPS_y:
        // 64 bits (64-bit opsize), 32 bits othwerwise.
        size = 4 << (Instrux->EfOpMode >> 1);
        break;

    case ND_OPS_yf:
        // 64 bits (64-bit mode), 32 bits (16, 32-bit opsize).
        size = 4 << (Instrux->DefCode >> 1);
        break;

    case ND_OPS_z:
        // 16 bits (16-bit opsize) or 32 bits (32 or 64-bit opsize).
        {
            static const ND_UINT8 szLut[4] = { ND_SIZE_16BIT, ND_SIZE_32BIT, ND_SIZE_32BIT, 0 };

            size = szLut[Instrux->EfOpMode];
        }
        break;

    case ND_OPS_a:
        // 2 x 16 bits (16-bit opsize) or 2 x 32 bits (32-bit opsize).
        {
            static const ND_UINT8 szLut[4] = { ND_SIZE_16BIT * 2, ND_SIZE_32BIT * 2, 0, 0};

            if (Instrux->DefCode > ND_CODE_32)
            {
                return ND_STATUS_INVALID_INSTRUX;
            }

            size = szLut[Instrux->EfOpMode];
        }
        break;

    case ND_OPS_c:
        // 8 bits (16-bit opsize) or 16 bits (32-bit opsize).
        switch (Instrux->DefCode)
        {
        case ND_CODE_16:
            size = Instrux->HasOpSize ? ND_SIZE_16BIT : ND_SIZE_8BIT;
            break;
        case ND_CODE_32:
            size = Instrux->HasOpSize ? ND_SIZE_16BIT : ND_SIZE_32BIT;
            break;
        case ND_CODE_64:
            size = ND_SIZE_64BIT;
            break;
        default:
            return ND_STATUS_INVALID_INSTRUX;
        }
        break;

    case ND_OPS_p:
        // 32, 48 or 80 bits pointer.
        {
            static const ND_UINT8 szLut[4] = { ND_SIZE_32BIT, ND_SIZE_48BIT, ND_SIZE_80BIT, 0 };

            size = szLut[Instrux->EfOpMode];
        }
        break;

    case ND_OPS_s:
        // 48 or 80 bits descriptor.
        {
            static const ND_UINT8 szLut[4] = { ND_SIZE_48BIT, ND_SIZE_48BIT, ND_SIZE_80BIT, 0 };

            size = szLut[Instrux->DefCode];
        }
        break;

    case ND_OPS_l:
        // 64 (16 or 32-bit opsize) or 128 bits (64-bit opsize).
        {
            static const ND_UINT8 szLut[4] = { ND_SIZE_64BIT, ND_SIZE_64BIT, ND_SIZE_128BIT, 0 };

            size = szLut[Instrux->DefCode];
        }
        break;

    case ND_OPS_x:
        // lower vector = 128 (128-bit vlen) or 256 bits (256-bit vlen).
        {
            static const ND_UINT8 szLut[4] = { ND_SIZE_128BIT, ND_SIZE_256BIT, ND_SIZE_512BIT, 0 };

            size = szLut[Instrux->EfVecMode];
        }
        break;

    case ND_OPS_fv:
        // full vector = 128, 256 or 512 bits.
        {
            static const ND_UINT8 szLut[4] = { ND_SIZE_128BIT, ND_SIZE_256BIT, ND_SIZE_512BIT, 0 };

            size = szLut[Instrux->EfVecMode];
        }
        break;

    case ND_OPS_uv:
        // upper vector = 256 bits (256-bit vlen) or 512 bits (512-bit vlen)
        {
            static const ND_UINT8 szLut[4] = { 0, ND_SIZE_256BIT, ND_SIZE_512BIT, 0 };

            if (ND_VECM_128 == Instrux->EfVecMode)
            {
                return ND_STATUS_INVALID_INSTRUX;
            }

            size = szLut[Instrux->EfVecMode];
        }
        break;

    case ND_OPS_ev:
        // eighth vector = 16, 32 or 64 bits.
        {
            static const ND_UINT8 szLut[4] = { ND_SIZE_16BIT, ND_SIZE_32BIT, ND_SIZE_64BIT, 0 };

            size = szLut[Instrux->EfVecMode];
        }
        break;

    case ND_OPS_qv:
        // quarter vector = 32, 64 or 128 bits.
        {
            static const ND_UINT8 szLut[4] = { ND_SIZE_32BIT, ND_SIZE_64BIT, ND_SIZE_128BIT, 0 };

            size = szLut[Instrux->EfVecMode];
        }
        break;

    case ND_OPS_hv:
        // half vector = 64, 128 or 256 bits.
        {
            static const ND_UINT8 szLut[4] = { ND_SIZE_64BIT, ND_SIZE_128BIT, ND_SIZE_256BIT, 0 };

            size = szLut[Instrux->EfVecMode];
        }
        break;

    case ND_OPS_pd:
    case ND_OPS_ps:
    case ND_OPS_ph:
        // 128 or 256 bits.
        {
            static const ND_UINT8 szLut[4] = { ND_SIZE_128BIT, ND_SIZE_256BIT, ND_SIZE_512BIT, 0 };

            size = szLut[Instrux->EfVecMode];
        }
        break;

    case ND_OPS_sd:
        // 128 bits scalar element (double precision).
        size = ND_SIZE_64BIT;
        break;

    case ND_OPS_ss:
        // 128 bits scalar element (single precision).
        size = ND_SIZE_32BIT;
        break;

    case ND_OPS_sh:
        // FP16 Scalar element.
        size = ND_SIZE_16BIT;
        break;

    case ND_OPS_mib:
        // MIB addressing, the base & the index are used to form a pointer.
        size = 0;
        break;

    case ND_OPS_vm32x:
    case ND_OPS_vm32y:
    case ND_OPS_vm32z:
        // 32 bit indexes from XMM, YMM or ZMM register.
        vsibIndexSize  = ND_SIZE_32BIT;
        vsibIndexCount = (Instrux->Exs.l == 0) ? 4 : ((Instrux->Exs.l == 1) ? 8 : 16);
        vsibRegSize = (ops == ND_OPS_vm32x) ? ND_SIZE_128BIT :
                      (ops == ND_OPS_vm32y) ? ND_SIZE_256BIT :
                                              ND_SIZE_512BIT;
        size = vsibIndexCount * (width ? ND_SIZE_64BIT : ND_SIZE_32BIT);
        break;

    case ND_OPS_vm32h:
        // 32 bit indexes from XMM or YMM.
        vsibIndexSize = ND_SIZE_32BIT;
        vsibIndexCount = (Instrux->Exs.l == 0) ? 2 : ((Instrux->Exs.l == 1) ? 4 : 8);
        vsibRegSize = (Instrux->Exs.l == 0) ? ND_SIZE_128BIT :
                      (Instrux->Exs.l == 1) ? ND_SIZE_128BIT :
                                              ND_SIZE_256BIT;
        size = vsibIndexCount * (width ? ND_SIZE_64BIT : ND_SIZE_32BIT);
        break;

    case ND_OPS_vm32n:
        // 32 bit indexes from XMM, YMM or ZMM register.
        vsibIndexSize = ND_SIZE_32BIT;
        vsibIndexCount = (Instrux->Exs.l == 0) ? 4 : ((Instrux->Exs.l == 1) ? 8 : 16);
        vsibRegSize = (Instrux->Exs.l == 0) ? ND_SIZE_128BIT :
                      (Instrux->Exs.l == 1) ? ND_SIZE_256BIT :
                                              ND_SIZE_512BIT;
        size = vsibIndexCount * (width ? ND_SIZE_64BIT : ND_SIZE_32BIT);
        break;

    case ND_OPS_vm64x:
    case ND_OPS_vm64y:
    case ND_OPS_vm64z:
        // 64 bit indexes from XMM, YMM or ZMM register.
        vsibIndexSize = ND_SIZE_64BIT;
        vsibIndexCount = (Instrux->Exs.l == 0) ? 2 : ((Instrux->Exs.l == 1) ? 4 : 8);
        vsibRegSize = (ops == ND_OPS_vm64x) ? ND_SIZE_128BIT :
                      (ops == ND_OPS_vm64y) ? ND_SIZE_256BIT :
                                              ND_SIZE_512BIT;
        size = vsibIndexCount * (width ? ND_SIZE_64BIT : ND_SIZE_32BIT);
        break;

    case ND_OPS_vm64h:
        // 64 bit indexes from XMM or YMM.
        vsibIndexSize = ND_SIZE_64BIT;
        vsibIndexCount = (Instrux->Exs.l == 0) ? 1 : ((Instrux->Exs.l == 1) ? 2 : 4);
        vsibRegSize = (Instrux->Exs.l == 0) ? ND_SIZE_128BIT :
                      (Instrux->Exs.l == 1) ? ND_SIZE_128BIT :
                                              ND_SIZE_256BIT;
        size = vsibIndexCount * (width ? ND_SIZE_64BIT : ND_SIZE_32BIT);
        break;

    case ND_OPS_vm64n:
        // 64 bit indexes from XMM, YMM or ZMM register.
        vsibIndexSize = ND_SIZE_64BIT;
        vsibIndexCount = (Instrux->Exs.l == 0) ? 2 : ((Instrux->Exs.l == 1) ? 4 : 8);
        vsibRegSize = (Instrux->Exs.l == 0) ? ND_SIZE_128BIT :
                      (Instrux->Exs.l == 1) ? ND_SIZE_256BIT :
                                              ND_SIZE_512BIT;
        size = vsibIndexCount * (width ? ND_SIZE_64BIT : ND_SIZE_32BIT);
        break;

    case ND_OPS_v2:
    case ND_OPS_v3:
    case ND_OPS_v4:
    case ND_OPS_v5:
    case ND_OPS_v8:
        // Multiple words accessed.
        {
            ND_UINT8 scale = 1;

            scale = (ops == ND_OPS_v2) ? 2 : 
                    (ops == ND_OPS_v3) ? 3 : 
                    (ops == ND_OPS_v4) ? 4 : 
                    (ops == ND_OPS_v5) ? 5 : 8;

            size =  scale * (2 << Instrux->EfOpMode);
        }
        break;

    case ND_OPS_12:
        // SAVPREVSSP instruction reads/writes 4 + 8 bytes from the shadow stack.
        size = 12;
        break;

    case ND_OPS_t:
        // Tile register. The actual size depends on how the TILECFG register has been programmed, but it can be 
        // up to 1K in size.
        size = ND_SIZE_1KB;
        break;

    case ND_OPS_384:
        // 384 bit Key Locker handle.
        size = ND_SIZE_384BIT;
        break;

    case ND_OPS_512:
        // 512 bit Key Locker handle.
        size = ND_SIZE_512BIT;
        break;

    case ND_OPS_4096:
        // 64 entries x 64 bit per entry = 4096 bit MSR address/value list.
        size = ND_SIZE_4096BIT;
        break;

    case ND_OPS_unknown:
        size = ND_SIZE_UNKNOWN;
        break;

    default:
        return ND_STATUS_INVALID_INSTRUX;
    }

    // Store operand info.
    operand->Size = bcstSize = size;

    //
    // Fill in the operand type.
    //
    switch (opt)
    {
    case ND_OPT_1:
        // operand is an implicit constant (used by shift/rotate instruction).
        operand->Type = ND_OP_CONST;
        operand->Encoding = ND_OPE_1;
        operand->Info.Constant.Const = 1;
        break;

    case ND_OPT_rIP:
        // The operand is the instruction pointer.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_RIP;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = 0;
        break;

    case ND_OPT_rAX:
        // Operand is the accumulator.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_GPR;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = NDR_RAX;
        break;

    case ND_OPT_AH:
        // Operand is the accumulator.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_GPR;
        operand->Info.Register.Size = ND_SIZE_8BIT;
        operand->Info.Register.Reg = NDR_AH;
        operand->Info.Register.IsHigh8 = ND_TRUE;
        break;

    case ND_OPT_rCX:
        // Operand is the counter register.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_GPR;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = NDR_RCX;
        break;

    case ND_OPT_rDX:
        // Operand is rDX.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_GPR;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = NDR_RDX;
        break;

    case ND_OPT_rBX:
        // Operand is BX.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_GPR;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = NDR_RBX;
        break;

    case ND_OPT_rBP:
        // Operand is rBP.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_GPR;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = NDR_RBP;
        break;

    case ND_OPT_rSP:
        // Operand is rSP.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_GPR;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = NDR_RSP;
        break;

    case ND_OPT_rSI:
        // Operand is rSI.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_GPR;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = NDR_RSI;
        break;

    case ND_OPT_rDI:
        // Operand is rDI.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_GPR;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = NDR_RDI;
        break;

    case ND_OPT_rR8:
        // Operand is R8.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_GPR;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = NDR_R8;
        break;

    case ND_OPT_rR9:
        // Operand is R9.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_GPR;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = NDR_R9;
        break;

    case ND_OPT_rR11:
        // Operand is R11.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_GPR;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = NDR_R11;
        break;

    case ND_OPT_CS:
        // Operand is the CS register.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_SEG;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = NDR_CS;
        break;

    case ND_OPT_SS:
        // Operand is the SS register.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_SEG;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = NDR_SS;
        break;

    case ND_OPT_DS:
        // Operand is the DS register.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_SEG;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = NDR_DS;
        break;

    case ND_OPT_ES:
        // Operand is the ES register.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_SEG;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = NDR_ES;
        break;

    case ND_OPT_FS:
        // Operand is the FS register.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_SEG;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = NDR_FS;
        break;

    case ND_OPT_GS:
        // Operand is the GS register.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_SEG;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = NDR_GS;
        break;

    case ND_OPT_ST0:
        // Operand is the ST(0) register.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_FPU;
        operand->Info.Register.Size = ND_SIZE_80BIT;
        operand->Info.Register.Reg = 0;
        break;

    case ND_OPT_STi:
        // Operand is the ST(i) register.
        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_M;
        operand->Info.Register.Type = ND_REG_FPU;
        operand->Info.Register.Size = ND_SIZE_80BIT;
        operand->Info.Register.Reg = Instrux->ModRm.rm;
        break;

    case ND_OPT_XMM0:
    case ND_OPT_XMM1:
    case ND_OPT_XMM2:
    case ND_OPT_XMM3:
    case ND_OPT_XMM4:
    case ND_OPT_XMM5:
    case ND_OPT_XMM6:
    case ND_OPT_XMM7:
        // Operand is a hard-coded XMM register.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_SSE;
        operand->Info.Register.Size = ND_SIZE_128BIT;
        operand->Info.Register.Reg = opt - ND_OPT_XMM0;
        break;

    // Special operands. These are always implicit, and can't be encoded inside the instruction.
    case ND_OPT_CR0:
        // The operand is implicit and is control register 0.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_CR;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = NDR_CR0;
        break;

    case ND_OPT_GDTR:
        // The operand is implicit and is the global descriptor table register.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_SYS;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = NDR_GDTR;
        break;

    case ND_OPT_IDTR:
        // The operand is implicit and is the interrupt descriptor table register.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_SYS;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = NDR_IDTR;
        break;

    case ND_OPT_LDTR:
        // The operand is implicit and is the local descriptor table register.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_SYS;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = NDR_LDTR;
        break;

    case ND_OPT_TR:
        // The operand is implicit and is the task register.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_SYS;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = NDR_TR;
        break;

    case ND_OPT_X87CONTROL:
        // The operand is implicit and is the x87 control word.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_SYS;
        operand->Info.Register.Size = ND_SIZE_16BIT;
        operand->Info.Register.Reg = NDR_X87_CONTROL;
        break;

    case ND_OPT_X87TAG:
        // The operand is implicit and is the x87 tag word.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_SYS;
        operand->Info.Register.Size = ND_SIZE_16BIT;
        operand->Info.Register.Reg = NDR_X87_TAG;
        break;

    case ND_OPT_X87STATUS:
        // The operand is implicit and is the x87 status word.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_SYS;
        operand->Info.Register.Size = ND_SIZE_16BIT;
        operand->Info.Register.Reg = NDR_X87_STATUS;
        break;

    case ND_OPT_MXCSR:
        // The operand is implicit and is the MXCSR.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_MXCSR;
        operand->Info.Register.Size = ND_SIZE_32BIT;
        operand->Info.Register.Reg = 0;
        break;

    case ND_OPT_PKRU:
        // The operand is the PKRU register.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_PKRU;
        operand->Info.Register.Size = ND_SIZE_32BIT;
        operand->Info.Register.Reg = 0;
        break;

    case ND_OPT_SSP:
        // The operand is the SSP register.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_SSP;
        operand->Info.Register.Size = operand->Size;
        operand->Info.Register.Reg = 0;
        break;

    case ND_OPT_UIF:
        // The operand is the User Interrupt Flag.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_UIF;
        operand->Info.Register.Size = ND_SIZE_8BIT; // 1 bit, in fact, but there is no size defined for one bit.
        operand->Info.Register.Reg = 0;
        break;

    case ND_OPT_MSR:
        // The operand is implicit and is a MSR (usually selected by the ECX register).
        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_E;
        operand->Info.Register.Type = ND_REG_MSR;
        operand->Info.Register.Size = ND_SIZE_64BIT;
        operand->Info.Register.Reg = 0xFFFFFFFF;
        break;

    case ND_OPT_TSC:
        // The operand is implicit and is the IA32_TSC.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_MSR;
        operand->Info.Register.Size = ND_SIZE_64BIT;
        operand->Info.Register.Reg = NDR_IA32_TSC;
        break;

    case ND_OPT_TSCAUX:
        // The operand is implicit and is the IA32_TSCAUX.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_MSR;
        operand->Info.Register.Size = ND_SIZE_64BIT;
        operand->Info.Register.Reg = NDR_IA32_TSC_AUX;
        break;

    case ND_OPT_SCS:
        // The operand is implicit and is the IA32_SYSENTER_CS.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_MSR;
        operand->Info.Register.Size = ND_SIZE_64BIT;
        operand->Info.Register.Reg = NDR_IA32_SYSENTER_CS;
        break;

    case ND_OPT_SESP:
        // The operand is implicit and is the IA32_SYSENTER_ESP.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_MSR;
        operand->Info.Register.Size = ND_SIZE_64BIT;
        operand->Info.Register.Reg = NDR_IA32_SYSENTER_ESP;
        break;

    case ND_OPT_SEIP:
        // The operand is implicit and is the IA32_SYSENTER_EIP.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_MSR;
        operand->Info.Register.Size = ND_SIZE_64BIT;
        operand->Info.Register.Reg = NDR_IA32_SYSENTER_EIP;
        break;

    case ND_OPT_STAR:
        // The operand is implicit and is the IA32_STAR.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_MSR;
        operand->Info.Register.Size = ND_SIZE_64BIT;
        operand->Info.Register.Reg = NDR_IA32_STAR;
        break;

    case ND_OPT_LSTAR:
        // The operand is implicit and is the IA32_LSTAR.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_MSR;
        operand->Info.Register.Size = ND_SIZE_64BIT;
        operand->Info.Register.Reg = NDR_IA32_LSTAR;
        break;

    case ND_OPT_FMASK:
        // The operand is implicit and is the IA32_FMASK.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_MSR;
        operand->Info.Register.Size = ND_SIZE_64BIT;
        operand->Info.Register.Reg = NDR_IA32_FMASK;
        break;

    case ND_OPT_FSBASE:
        // The operand is implicit and is the IA32_FS_BASE MSR.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_MSR;
        operand->Info.Register.Size = ND_SIZE_64BIT;
        operand->Info.Register.Reg = NDR_IA32_FS_BASE;
        break;

    case ND_OPT_GSBASE:
        // The operand is implicit and is the IA32_GS_BASE MSR.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_MSR;
        operand->Info.Register.Size = ND_SIZE_64BIT;
        operand->Info.Register.Reg = NDR_IA32_GS_BASE;
        break;

    case ND_OPT_KGSBASE:
        // The operand is implicit and is the IA32_KERNEL_GS_BASE MSR.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_MSR;
        operand->Info.Register.Size = ND_SIZE_64BIT;
        operand->Info.Register.Reg = NDR_IA32_KERNEL_GS_BASE;
        break;

    case ND_OPT_XCR:
        // The operand is implicit and is an extended control register (usually selected by ECX register).
        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_E;
        operand->Info.Register.Type = ND_REG_XCR;
        operand->Info.Register.Size = ND_SIZE_64BIT;
        operand->Info.Register.Reg = 0xFF;
        break;

    case ND_OPT_XCR0:
        // The operand is implicit and is XCR0.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_XCR;
        operand->Info.Register.Size = ND_SIZE_64BIT;
        operand->Info.Register.Reg = 0;
        break;

    case ND_OPT_BANK:
        // Multiple registers are accessed.
        if ((Idbe->Instruction == ND_INS_PUSHA) || (Idbe->Instruction == ND_INS_POPA))
        {
            operand->Type = ND_OP_REG;
            operand->Size = Instrux->WordLength;
            operand->Info.Register.Type = ND_REG_GPR;
            operand->Info.Register.Size = Instrux->WordLength;
            operand->Info.Register.Reg = NDR_EAX;
            operand->Info.Register.Count = 8;
            operand->Info.Register.IsBlock = ND_TRUE;
        }
        else
        {
            operand->Type = ND_OP_BANK;
        }
        break;

    case ND_OPT_A:
        // Fetch the address. NOTE: The size can't be larger than 8 bytes.
        if (ops == ND_OPS_p)
        {
            // Fill in operand info.
            operand->Type = ND_OP_ADDR_FAR;
            operand->Encoding = ND_OPE_D;
            operand->Info.Address.BaseSeg = Instrux->Address.Cs;
            operand->Info.Address.Offset = Instrux->Address.Ip;
        }
        else
        {
            // Fill in operand info.
            operand->Type = ND_OP_ADDR_NEAR;
            operand->Encoding = ND_OPE_D;
            operand->Info.AddressNear.Target = Instrux->AddressNear;
        }
        break;

    case ND_OPT_B:
        // General purpose register encoded in VEX.vvvv field.
        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_V;
        operand->Info.Register.Type = ND_REG_GPR;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = ND_DECODE_GPR_V(Instrux);
        break;

    case ND_OPT_C:
        // Control register, encoded in modrm.reg.
        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_R;
        operand->Info.Register.Type = ND_REG_CR;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = ND_DECODE_CR_R(Instrux);

        // On some AMD processors, the presence of the LOCK prefix before MOV to/from control registers allows accessing
        // higher 8 control registers.
        if ((ND_CODE_64 != Instrux->DefCode) && (Instrux->HasLock))
        {
            operand->Info.Register.Reg |= 0x8;
        }

        break;

    case ND_OPT_D:
        // Debug register, encoded in modrm.reg.
        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_R;
        operand->Info.Register.Type = ND_REG_DR;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = ND_DECODE_DR_R(Instrux);
        break;

    case ND_OPT_T:
        // Test register, encoded in modrm.reg.
        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_R;
        operand->Info.Register.Type = ND_REG_TR;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = ND_DECODE_TR_R(Instrux);
        break;

    case ND_OPT_S:
        // Segment register, encoded in modrm.reg.
        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_R;
        operand->Info.Register.Type = ND_REG_SEG;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = ND_DECODE_SEG_R(Instrux);
        break;

    case ND_OPT_E:
        // General purpose register or memory, encoded in modrm.rm.
        if (Instrux->ModRm.mod != 3)
        {
            goto memory;
        }

        // fall through

    case ND_OPT_R:
        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_M;
        operand->Info.Register.Type = ND_REG_GPR;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = ND_DECODE_GPR_M(Instrux);
        operand->Info.Register.IsHigh8 = (operand->Info.Register.Size == 1) &&
                                         (operand->Info.Register.Reg  >= 4) &&
                                         (ND_ENCM_LEGACY == Instrux->EncMode) &&
                                         !Instrux->HasRex && !Instrux->HasRex2;
        break;

    case ND_OPT_F:
        // The flags register.
        operand->Type = ND_OP_REG;
        operand->Info.Register.Type = ND_REG_FLG;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = 0;
        break;

    case ND_OPT_K:
        // The operand is the stack.
        operand->Type = ND_OP_MEM;
        operand->Info.Memory.IsStack = ND_TRUE;
        operand->Info.Memory.HasBase = ND_TRUE;
        operand->Info.Memory.Base = NDR_RSP;
        operand->Info.Memory.BaseSize = 2 << Instrux->DefStack;
        operand->Info.Memory.HasSeg = ND_TRUE;
        operand->Info.Memory.Seg = NDR_SS;
        break;

    case ND_OPT_G:
        // General purpose register encoded in modrm.reg.
        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_R;
        operand->Info.Register.Type = ND_REG_GPR;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = ND_DECODE_GPR_R(Instrux);
        operand->Info.Register.IsHigh8 = (operand->Info.Register.Size == 1) &&
                                         (operand->Info.Register.Reg  >= 4) &&
                                         (ND_ENCM_LEGACY == Instrux->EncMode) &&
                                         !Instrux->HasRex && !Instrux->HasRex2;
        break;

    case ND_OPT_I:
        // Immediate, encoded in instructon bytes.
        operand->Type = ND_OP_IMM;
        operand->Encoding = ND_OPE_I;
        operand->Info.Immediate.Imm = Instrux->Immediate1;
        operand->Info.Immediate.RawSize = (ND_UINT8)size;

        if (operand->Flags.SignExtendedDws || operand->Flags.SignExtendedOp1)
        {
            // Get the default word size: the immediate is sign extended to the default word size.
            operand->Size = 2 << Instrux->EfOpMode;

            operand->Info.Immediate.Imm = ND_SIGN_EX(size, operand->Info.Immediate.Imm);
        }

        break;

    case ND_OPT_I1:
        // Immediate, encoded in instructon bytes. This is the first of two immediates.
        operand->Type = ND_OP_IMM;
        operand->Encoding = ND_OPE_I;
        operand->Info.Immediate.RawSize = (ND_UINT8)size;
        operand->Info.Immediate.Imm = Instrux->Immediate1;
        break;

    case ND_OPT_I2:
        // Immediate, encoded in instructon bytes. This is the second of two immediates.
        operand->Type = ND_OP_IMM;
        operand->Encoding = ND_OPE_I;
        operand->Info.Immediate.RawSize = (ND_UINT8)size;
        operand->Info.Immediate.Imm = Instrux->Immediate2;
        break;

    case ND_OPT_m2zI:
        operand->Type = ND_OP_IMM;
        operand->Encoding = ND_OPE_L;
        operand->Info.Immediate.Imm = Instrux->SseImmediate & 3;
        operand->Info.Immediate.RawSize = (ND_UINT8)size;
        break;

    case ND_OPT_J:
        // Fetch the relative offset. NOTE: The size of the relative can't exceed 4 bytes.
        // The instruction is RIP relative.
        operand->Type = ND_OP_OFFS;
        operand->Encoding = ND_OPE_D;
        // The relative offset is forced to the default word length. Care must be taken with the 32 bit
        // branches that have 0x66 prefix (in 32 bit mode)!
        operand->Size = Instrux->WordLength;
        operand->Info.RelativeOffset.Rel = ND_SIGN_EX(size, Instrux->RelativeOffset);
        operand->Info.RelativeOffset.RawSize = (ND_UINT8)size;
        break;

    case ND_OPT_P:
        // The reg field of the ModR/M byte selects a packed quadword MMX technology register.
        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_R;
        operand->Info.Register.Type = ND_REG_MMX;
        operand->Info.Register.Size = ND_SIZE_64BIT;
        operand->Info.Register.Reg = ND_DECODE_MMX_R(Instrux);
        break;

    case ND_OPT_Q:
        // The R/M field of the ModR/M byte selects a packed-quadword, MMX technology register.
        if (Instrux->ModRm.mod != 3)
        {
            goto memory;
        }

        // fall through

    case ND_OPT_N:
        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_M;
        operand->Info.Register.Type = ND_REG_MMX;
        operand->Info.Register.Size = ND_SIZE_64BIT;
        operand->Info.Register.Reg = ND_DECODE_MMX_M(Instrux);
        break;

    case ND_OPT_O:
        // Absolute address, encoded in instruction bytes.
        // NOTE: The moffset len can't exceed 8 bytes.

        // operand info.
        operand->Type = ND_OP_MEM;
        operand->Encoding = ND_OPE_D;
        operand->Info.Memory.HasDisp = ND_TRUE;
        operand->Info.Memory.IsDirect = ND_TRUE;
        operand->Info.Memory.DispSize = Instrux->MoffsetLength;
        operand->Info.Memory.Disp = Instrux->Moffset;
        operand->Info.Memory.HasSeg = ND_TRUE;
        operand->Info.Memory.Seg = NdGetSegOverride(Instrux, NDR_DS);
        break;

    case ND_OPT_M:
        // Modrm based memory addressing.
    memory:
        operand->Type = ND_OP_MEM;
        operand->Encoding = ND_OPE_M;
        operand->Info.Memory.HasSeg = ND_TRUE;

        // Parse mode specific memory information.
        if (ND_ADDR_16 != Instrux->AddrMode)
        {
            status = NdParseMemoryOperand3264(Instrux, Idbe, operand, vsibRegSize);
            if (!ND_SUCCESS(status))
            {
                return status;
            }
        }
        else
        {
            status = NdParseMemoryOperand16(Instrux, operand);
            if (!ND_SUCCESS(status))
            {
                return status;
            }
        }

        // Get the segment. Note that in long mode, segment prefixes are ignored, except for FS and GS.
        if (Instrux->HasSeg)
        {
            operand->Info.Memory.Seg = NdGetSegOverride(Instrux, operand->Info.Memory.Seg);
        }

        // Handle VSIB addressing.
        if (Idbe->Attributes & ND_FLAG_VSIB)
        {
            operand->Info.Memory.IsVsib = ND_TRUE;

            operand->Info.Memory.Vsib.IndexSize = vsibIndexSize;
            operand->Info.Memory.Vsib.ElemCount = vsibIndexCount;
            operand->Info.Memory.Vsib.ElemSize = (ND_UINT8)(size / vsibIndexCount);
        }

        // Handle sibmem addressing, as used by Intel AMX instructions.
        if (Idbe->Attributes & ND_FLAG_SIBMEM)
        {
            operand->Info.Memory.IsSibMem = ND_TRUE;
        }

        // If we have broadcast, the operand size is fixed to either 16, 32 or 64 bit, depending on bcast size.
        // Therefore, we will override the rawSize with either 16, 32 or 64 bits. Note that bcstSize will save the 
        // total size of the access, and it will be used to compute the number of broadcasted elements: 
        // bcstSize / rawSize.
        if (Instrux->HasBroadcast)
        {
            operand->Info.Memory.HasBroadcast = ND_TRUE;

            if (opd & ND_OPD_B32)
            {
                size = ND_SIZE_32BIT;
            }
            else if (opd & ND_OPD_B64)
            {
                size = ND_SIZE_64BIT;
            }
            else if (opd & ND_OPD_B16)
            {
                size = ND_SIZE_16BIT;
            }
            else
            {
                size = width ? ND_SIZE_64BIT : ND_SIZE_32BIT;
            }

            // Override operand size.
            operand->Size = size;

            operand->Info.Memory.Broadcast.Size = (ND_UINT8)operand->Size;
            operand->Info.Memory.Broadcast.Count = (ND_UINT8)(bcstSize / operand->Size);
        }

        // Handle compressed displacement, if any. Note that most EVEX instructions with 8 bit displacement
        // use compressed displacement addressing.
        if (Instrux->HasCompDisp)
        {
            operand->Info.Memory.HasCompDisp = ND_TRUE;
            operand->Info.Memory.CompDispSize = NdGetCompDispSize(Instrux, Idbe, operand->Size);
        }

        // MIB, if any. Used by some MPX instructions.
        operand->Info.Memory.IsMib = !!(Idbe->Attributes & ND_FLAG_MIB);

        // Bitbase, if any. Used by BT* instructions when the first op is mem and the second one reg.
        operand->Info.Memory.IsBitbase = !!(Idbe->Attributes & ND_FLAG_BITBASE);

        // AG, if this is the case.
        if (Idbe->Attributes & ND_FLAG_AG)
        {
            operand->Info.Memory.IsAG = ND_TRUE;

            // Address generation instructions ignore the segment prefixes. Examples are LEA and MPX instructions.
            operand->Info.Memory.HasSeg = ND_FALSE;
            operand->Info.Memory.Seg = 0;
        }

        // Shadow Stack Access, if this is the case.
        if (Idbe->Attributes & ND_FLAG_SHS)
        {
            operand->Info.Memory.IsShadowStack = ND_TRUE;
            operand->Info.Memory.ShStkType = ND_SHSTK_EXPLICIT;
        }

        break;

    case ND_OPT_H:
        // Vector register, encoded in VEX/EVEX.vvvv.
        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_V;
        operand->Info.Register.Type = ND_REG_SSE;
        operand->Info.Register.Size = (ND_REG_SIZE)(size < ND_SIZE_128BIT ? ND_SIZE_128BIT : size);
        // V' will be 0 for any non-EVEX encoded instruction.
        operand->Info.Register.Reg = ND_DECODE_VEC_V(Instrux);
        break;

    case ND_OPT_L:
        // Vector register, encoded in immediate.
        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_L;
        operand->Info.Register.Type = ND_REG_SSE;
        operand->Info.Register.Size = (ND_REG_SIZE)(size < ND_SIZE_128BIT ? ND_SIZE_128BIT : size);
        operand->Info.Register.Reg = ND_DECODE_VEC_L(Instrux) & (Instrux->DefCode == ND_CODE_64 ? 0xF : 0x7);
        break;

    case ND_OPT_V:
        // Vector register encoded in modrm.reg.
        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_R;
        operand->Info.Register.Type = ND_REG_SSE;
        operand->Info.Register.Size = (ND_REG_SIZE)(size < ND_SIZE_128BIT ? ND_SIZE_128BIT : size);
        operand->Info.Register.Reg = ND_DECODE_VEC_R(Instrux);
        break;

    case ND_OPT_W:
        // Vector register or memory encoded in modrm.rm.
        if (Instrux->ModRm.mod != 3)
        {
            goto memory;
        }

        // fall through

    case ND_OPT_U:
        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_M;
        operand->Info.Register.Type = ND_REG_SSE;
        operand->Info.Register.Size = (ND_REG_SIZE)(size < ND_SIZE_128BIT ? ND_SIZE_128BIT : size);
        operand->Info.Register.Reg = ND_DECODE_VEC_M(Instrux);
        break;

    case ND_OPT_X:
    case ND_OPT_Y:
    case ND_OPT_pDI:
        // RSI/RDI based addressing, as used by string instructions.
        operand->Type = ND_OP_MEM;
        operand->Info.Memory.HasBase = ND_TRUE;
        operand->Info.Memory.BaseSize = 2 << Instrux->AddrMode;
        operand->Info.Memory.HasSeg = ND_TRUE;
        operand->Info.Memory.Base = (ND_UINT8)(((opt == ND_OPT_X) ? NDR_RSI : NDR_RDI));
        operand->Info.Memory.IsString = (ND_OPT_X == opt || ND_OPT_Y == opt);
        // DS:rSI supports segment overriding. ES:rDI does not.
        if (opt == ND_OPT_Y)
        {
            operand->Info.Memory.Seg = NDR_ES;
        }
        else
        {
            operand->Info.Memory.Seg = NdGetSegOverride(Instrux, NDR_DS);
        }
        break;

    case ND_OPT_pBXAL:
        // [rBX + AL], used by XLAT.
        operand->Type = ND_OP_MEM;
        operand->Info.Memory.HasBase = ND_TRUE;
        operand->Info.Memory.HasIndex = ND_TRUE;
        operand->Info.Memory.BaseSize = 2 << Instrux->AddrMode;
        operand->Info.Memory.IndexSize = ND_SIZE_8BIT;  // Always 1 Byte.
        operand->Info.Memory.Base = NDR_RBX;            // Always rBX.
        operand->Info.Memory.Index = NDR_AL;            // Always AL.
        operand->Info.Memory.Scale = 1;                 // Always 1.
        operand->Info.Memory.HasSeg = ND_TRUE;
        operand->Info.Memory.Seg = NdGetSegOverride(Instrux, NDR_DS);
        break;

    case ND_OPT_pAX:
        // [rAX], used implicitly by MONITOR, MONITORX and RMPADJUST instructions.
        operand->Type = ND_OP_MEM;
        operand->Info.Memory.HasBase = ND_TRUE;
        operand->Info.Memory.BaseSize = 2 << Instrux->AddrMode;
        operand->Info.Memory.Base = NDR_RAX;            // Always rAX.
        operand->Info.Memory.HasSeg = ND_TRUE;
        operand->Info.Memory.Seg = NdGetSegOverride(Instrux, NDR_DS);
        break;

    case ND_OPT_pCX:
        // [rCX], used implicitly by RMPUPDATE.
        operand->Type = ND_OP_MEM;
        operand->Info.Memory.HasBase = ND_TRUE;
        operand->Info.Memory.BaseSize = 2 << Instrux->AddrMode;
        operand->Info.Memory.Base = NDR_RCX;            // Always rCX.
        operand->Info.Memory.HasSeg = ND_TRUE;
        operand->Info.Memory.Seg = NdGetSegOverride(Instrux, NDR_DS);
        break;

    case ND_OPT_pBP:
        // [sBP], used implicitly by ENTER, when nesting level is > 1.
        // Operand size bytes accessed from memory. Base reg size determined by stack address size attribute.
        operand->Type = ND_OP_MEM;
        operand->Info.Memory.HasBase = ND_TRUE;
        operand->Info.Memory.BaseSize = 2 << Instrux->DefStack;
        operand->Info.Memory.Base = NDR_RBP;            // Always rBP.
        operand->Info.Memory.HasSeg = ND_TRUE;
        operand->Info.Memory.Seg = NDR_SS;
        break;

    case ND_OPT_SHS:
        // Shadow stack access using the current SSP.
        operand->Type = ND_OP_MEM;
        operand->Info.Memory.IsShadowStack = ND_TRUE;
        operand->Info.Memory.ShStkType = ND_SHSTK_SSP_LD_ST;
        break;

    case ND_OPT_SHS0:
        // Shadow stack access using the IA32_PL0_SSP.
        operand->Type = ND_OP_MEM;
        operand->Info.Memory.IsShadowStack = ND_TRUE;
        operand->Info.Memory.ShStkType = ND_SHSTK_PL0_SSP;
        break;

    case ND_OPT_SMT:
        // Table of MSR addresses, encoded in [RSI].
        operand->Type = ND_OP_MEM;
        operand->Info.Memory.HasBase = ND_TRUE;
        operand->Info.Memory.BaseSize = 2 << Instrux->AddrMode;
        operand->Info.Memory.Base = NDR_RSI;            // Always rSI.
        operand->Info.Memory.HasSeg = ND_FALSE;         // Linear Address directly, only useable in 64 bit mode.
        break;

    case ND_OPT_DMT:
        // Table of MSR addresses, encoded in [RDI].
        operand->Type = ND_OP_MEM;
        operand->Info.Memory.HasBase = ND_TRUE;
        operand->Info.Memory.BaseSize = 2 << Instrux->AddrMode;
        operand->Info.Memory.Base = NDR_RDI;            // Always rDI.
        operand->Info.Memory.HasSeg = ND_FALSE;         // Linear Address directly, only useable in 64 bit mode.
        break;

    case ND_OPT_SHSP:
        // Shadow stack push/pop access.
        operand->Type = ND_OP_MEM;
        operand->Info.Memory.IsShadowStack = ND_TRUE;
        operand->Info.Memory.ShStkType = ND_SHSTK_SSP_PUSH_POP;
        break;

    case ND_OPT_Z:
        // A GPR Register is selected by the low 3 bits inside the opcode. REX.B can be used to extend it.
        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_O;
        operand->Info.Register.Type = ND_REG_GPR;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = ND_DECODE_GPR_O(Instrux);
        operand->Info.Register.IsHigh8 = (operand->Info.Register.Size == 1) &&
                                         (operand->Info.Register.Reg  >= 4) &&
                                         (ND_ENCM_LEGACY == Instrux->EncMode) &&
                                         !Instrux->HasRex && !Instrux->HasRex2;
        break;

    case ND_OPT_rB:
        // reg inside modrm selects a BND register.
        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_R;
        operand->Info.Register.Type = ND_REG_BND;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = ND_DECODE_BND_R(Instrux);
        break;

    case ND_OPT_mB:
        // rm inside modrm selects either a BND register, either memory.
        if (Instrux->ModRm.mod != 3)
        {
            goto memory;
        }

        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_M;
        operand->Info.Register.Type = ND_REG_BND;
        operand->Info.Register.Size = (ND_REG_SIZE)size;
        operand->Info.Register.Reg = ND_DECODE_BND_M(Instrux);
        break;

    case ND_OPT_rK:
        // reg inside modrm selects a mask register.
        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_R;
        operand->Info.Register.Type = ND_REG_MSK;
        operand->Info.Register.Size = ND_SIZE_64BIT;
        operand->Info.Register.Reg = ND_DECODE_MSK_R(Instrux);
        break;

    case ND_OPT_vK:
        // vex.vvvv selects a mask register.
        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_V;
        operand->Info.Register.Type = ND_REG_MSK;
        operand->Info.Register.Size = ND_SIZE_64BIT;
        operand->Info.Register.Reg = ND_DECODE_MSK_V(Instrux);
        break;

    case ND_OPT_mK:
        // rm inside modrm selects either a mask register, either memory.
        if (Instrux->ModRm.mod != 3)
        {
            goto memory;
        }

        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_M;
        operand->Info.Register.Type = ND_REG_MSK;
        operand->Info.Register.Size = ND_SIZE_64BIT;
        // X and B are ignored when Msk registers are being addressed.
        operand->Info.Register.Reg = ND_DECODE_MSK_M(Instrux);
        break;

    case ND_OPT_aK:
        // aaa inside evex selects either a mask register, which is used for masking a destination operand.
        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_A;
        operand->Info.Register.Type = ND_REG_MSK;
        operand->Info.Register.Size = ND_SIZE_64BIT;
        operand->Info.Register.Reg = ND_DECODE_MSK_A(Instrux);
        break;

    case ND_OPT_rM:
        // Sigh. reg field inside mod r/m encodes memory. This encoding is used by MOVDIR64b and ENQCMD instructions.
        // When the ModR/M.reg field is used to select a memory operand, the following apply:
        // - The ES segment register is used as a base
        // - The ES segment register cannot be overridden
        // - The size of the base register is selected by the address size, not the operand size.
        operand->Type = ND_OP_MEM;
        operand->Encoding = ND_OPE_R;
        operand->Info.Memory.HasBase = ND_TRUE;
        operand->Info.Memory.Base = ND_DECODE_GPR_R(Instrux);
        operand->Info.Memory.BaseSize = 2 << Instrux->AddrMode;
        operand->Info.Memory.HasSeg = ND_TRUE;
        operand->Info.Memory.Seg = NDR_ES;
        break;

    case ND_OPT_mM:
        // Sigh. rm field inside mod r/m encodes memory, even if mod is 3.
        operand->Type = ND_OP_MEM;
        operand->Encoding = ND_OPE_M;
        operand->Info.Memory.HasBase = ND_TRUE;
        operand->Info.Memory.Base = ND_DECODE_GPR_M(Instrux);
        operand->Info.Memory.BaseSize = 2 << Instrux->AddrMode;
        operand->Info.Memory.HasSeg = ND_TRUE;
        operand->Info.Memory.Seg = NdGetSegOverride(Instrux, NDR_DS);
        break;

    case ND_OPT_rT:
        // Tile register encoded in ModR/M.reg field.
        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_R;
        operand->Info.Register.Type = ND_REG_TILE;
        operand->Info.Register.Size = size;
        operand->Info.Register.Reg = ND_DECODE_TMM_R(Instrux); 
        break;

    case ND_OPT_mT:
        // Tile register encoded in ModR/M.rm field.
        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_M;
        operand->Info.Register.Type = ND_REG_TILE;
        operand->Info.Register.Size = size;
        operand->Info.Register.Reg = ND_DECODE_TMM_M(Instrux); 
        break;

    case ND_OPT_vT:
        // Tile register encoded in vex.vvvv field.
        operand->Type = ND_OP_REG;
        operand->Encoding = ND_OPE_V;
        operand->Info.Register.Type = ND_REG_TILE;
        operand->Info.Register.Size = size;
        operand->Info.Register.Reg = ND_DECODE_TMM_V(Instrux);
        break;

    case ND_OPT_dfv:
        // Default flags value encoded in vex.vvvv field.
        operand->Type = ND_OP_DFV;
        operand->Encoding = ND_OPE_V;
        operand->Info.DefaultFlags.CF = Instrux->EvexCond.CF;
        operand->Info.DefaultFlags.ZF = Instrux->EvexCond.ZF;
        operand->Info.DefaultFlags.SF = Instrux->EvexCond.SF;
        operand->Info.DefaultFlags.OF = Instrux->EvexCond.OF;
        operand->Size = 0;
        break;

    default:
        return ND_STATUS_INVALID_INSTRUX;
    }

    if (operand->Type == ND_OP_REG)
    {
        // Handle block addressing - used by AVX512_4FMAPS and AVX512_4VNNIW instructions. Also used by VP2INTERSECTD/Q
        // instructions. Also note that in block addressing, the base of the block is masked using the size of the block;
        // for example, for a block size of 1, the first register must be even; For a block size of 4, the first register
        // must be divisible by 4.
        if (opb != 0)
        {
            operand->Info.Register.Count = opb;
            operand->Info.Register.Reg &= (ND_UINT32)~(opb - 1);
            operand->Info.Register.IsBlock = ND_TRUE;
        }
        else
        {
            operand->Info.Register.Count = 1;
        }

        // Handle zero-upper semantic for destination operands. Applies to destination registers only.
        if ((Instrux->HasNd || Instrux->HasZu) && operand->Access.Write && !operand->Flags.IsDefault)
        {
            operand->Info.Register.IsZeroUpper = 1;
        }
    }

    // Handle decorators. Note that only Mask, Zero and Broadcast are stored per-operand.
    if (0 != opd)
    {
        // Check for mask register. Mask if present only if the operand supports masking and if the
        // mask register is not k0 (which implies "no masking").
        if ((opd & ND_OPD_MASK) && (Instrux->HasMask))
        {
            operand->Decorator.HasMask = ND_TRUE;
            operand->Decorator.Msk = (ND_UINT8)Instrux->Exs.k;
        }

        // Check for zeroing. The operand must support zeroing and the z bit inside evex3 must be set. Note that
        // zeroing is allowed only for register destinations, and NOT for memory.
        if ((opd & ND_OPD_ZERO) && (Instrux->HasZero))
        {
            operand->Decorator.HasZero = ND_TRUE;
        }

        // Check for broadcast again. We've already filled the broadcast size before parsing the op size.
        if ((opd & ND_OPD_BCAST) && (Instrux->HasBroadcast))
        {
            operand->Decorator.HasBroadcast = ND_TRUE;
        }
    }

    return status;
}
