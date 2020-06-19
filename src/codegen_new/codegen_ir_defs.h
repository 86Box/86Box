#ifndef _CODEGEN_IR_DEFS_
#define _CODEGEN_IR_DEFS_

#include "codegen_reg.h"

#define UOP_REG(reg, size, version) ((reg) | (size) | (version << 8))

/*uOP is a barrier. All previous uOPs must have completed before this one executes.
  All registers must have been written back or discarded.
  This should be used when calling external functions that may change any emulated
  registers.*/
#define UOP_TYPE_BARRIER (1 << 31)

/*uOP is a barrier. All previous uOPs must have completed before this one executes.
  All registers must have been written back, but do not have to be discarded.
  This should be used when calling functions that preserve registers, but can cause
  the code block to exit (eg memory load/store functions).*/
#define UOP_TYPE_ORDER_BARRIER (1 << 27)

/*uOP uses source and dest registers*/
#define UOP_TYPE_PARAMS_REGS    (1 << 28)
/*uOP uses pointer*/
#define UOP_TYPE_PARAMS_POINTER (1 << 29)
/*uOP uses immediate data*/
#define UOP_TYPE_PARAMS_IMM     (1 << 30)

/*uOP is a jump, with the destination uOP in uop->jump_dest_uop. The compiler must
  set jump_dest in the destination uOP to the address of the branch offset to be
  written when known.*/
#define UOP_TYPE_JUMP           (1 << 26)
/*uOP is the destination of a jump, and must set the destination offset of the jump
  at compile time.*/
#define UOP_TYPE_JUMP_DEST      (1 << 25)


#define UOP_LOAD_FUNC_ARG_0       (UOP_TYPE_PARAMS_REGS    | 0x00)
#define UOP_LOAD_FUNC_ARG_1       (UOP_TYPE_PARAMS_REGS    | 0x01)
#define UOP_LOAD_FUNC_ARG_2       (UOP_TYPE_PARAMS_REGS    | 0x02)
#define UOP_LOAD_FUNC_ARG_3       (UOP_TYPE_PARAMS_REGS    | 0x03)
#define UOP_LOAD_FUNC_ARG_0_IMM   (UOP_TYPE_PARAMS_IMM     | 0x08 | UOP_TYPE_BARRIER)
#define UOP_LOAD_FUNC_ARG_1_IMM   (UOP_TYPE_PARAMS_IMM     | 0x09 | UOP_TYPE_BARRIER)
#define UOP_LOAD_FUNC_ARG_2_IMM   (UOP_TYPE_PARAMS_IMM     | 0x0a | UOP_TYPE_BARRIER)
#define UOP_LOAD_FUNC_ARG_3_IMM   (UOP_TYPE_PARAMS_IMM     | 0x0b | UOP_TYPE_BARRIER)
#define UOP_CALL_FUNC             (UOP_TYPE_PARAMS_POINTER | 0x10 | UOP_TYPE_BARRIER)
/*UOP_CALL_INSTRUCTION_FUNC - call instruction handler at p, check return value and exit block if non-zero*/
#define UOP_CALL_INSTRUCTION_FUNC (UOP_TYPE_PARAMS_POINTER | 0x11 | UOP_TYPE_BARRIER)
#define UOP_STORE_P_IMM           (UOP_TYPE_PARAMS_IMM     | 0x12)
#define UOP_STORE_P_IMM_8         (UOP_TYPE_PARAMS_IMM     | 0x13)
/*UOP_LOAD_SEG - load segment in src_reg_a to segment p via loadseg(), check return value and exit block if non-zero*/
#define UOP_LOAD_SEG              (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_POINTER | 0x14 | UOP_TYPE_BARRIER)
/*UOP_JMP - jump to ptr*/
#define UOP_JMP                   (UOP_TYPE_PARAMS_POINTER | 0x15 | UOP_TYPE_ORDER_BARRIER)
/*UOP_CALL_FUNC - call instruction handler at p, dest_reg = return value*/
#define UOP_CALL_FUNC_RESULT      (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_POINTER | 0x16 | UOP_TYPE_BARRIER)
/*UOP_JMP_DEST - jump to ptr*/
#define UOP_JMP_DEST              (UOP_TYPE_PARAMS_IMM | UOP_TYPE_PARAMS_POINTER | 0x17 | UOP_TYPE_ORDER_BARRIER | UOP_TYPE_JUMP)
#define UOP_NOP_BARRIER           (UOP_TYPE_BARRIER | 0x18)

#ifdef DEBUG_EXTRA
/*UOP_LOG_INSTR - log non-recompiled instruction in imm_data*/
#define UOP_LOG_INSTR             (UOP_TYPE_PARAMS_IMM | 0x1f)
#endif

/*UOP_MOV_PTR - dest_reg = p*/
#define UOP_MOV_PTR               (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_POINTER | 0x20)
/*UOP_MOV_IMM - dest_reg = imm_data*/
#define UOP_MOV_IMM               (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | 0x21)
/*UOP_MOV - dest_reg = src_reg_a*/
#define UOP_MOV                   (UOP_TYPE_PARAMS_REGS    | 0x22)
/*UOP_MOVZX - dest_reg = zero_extend(src_reg_a)*/
#define UOP_MOVZX                 (UOP_TYPE_PARAMS_REGS    | 0x23)
/*UOP_MOVSX - dest_reg = sign_extend(src_reg_a)*/
#define UOP_MOVSX                 (UOP_TYPE_PARAMS_REGS    | 0x24)
/*UOP_MOV_DOUBLE_INT - dest_reg = (double)src_reg_a*/
#define UOP_MOV_DOUBLE_INT        (UOP_TYPE_PARAMS_REGS    | 0x25)
/*UOP_MOV_INT_DOUBLE - dest_reg = (int)src_reg_a. New rounding control in src_reg_b, old rounding control in src_reg_c*/
#define UOP_MOV_INT_DOUBLE        (UOP_TYPE_PARAMS_REGS    | 0x26)
/*UOP_MOV_INT_DOUBLE_64 - dest_reg = (int)src_reg_a. New rounding control in src_reg_b, old rounding control in src_reg_c*/
#define UOP_MOV_INT_DOUBLE_64     (UOP_TYPE_PARAMS_REGS    | 0x27)
/*UOP_MOV_REG_PTR - dest_reg = *p*/
#define UOP_MOV_REG_PTR           (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_POINTER | 0x28)
/*UOP_MOVZX_REG_PTR_8 - dest_reg = *(uint8_t *)p*/
#define UOP_MOVZX_REG_PTR_8       (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_POINTER | 0x29)
/*UOP_MOVZX_REG_PTR_16 - dest_reg = *(uint16_t *)p*/
#define UOP_MOVZX_REG_PTR_16      (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_POINTER | 0x2a)
/*UOP_ADD - dest_reg = src_reg_a + src_reg_b*/
#define UOP_ADD                   (UOP_TYPE_PARAMS_REGS    | 0x30)
/*UOP_ADD_IMM - dest_reg = src_reg_a + immediate*/
#define UOP_ADD_IMM               (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | 0x31)
/*UOP_AND - dest_reg = src_reg_a & src_reg_b*/
#define UOP_AND                   (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | 0x32)
/*UOP_AND_IMM - dest_reg = src_reg_a & immediate*/
#define UOP_AND_IMM               (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | 0x33)
/*UOP_ADD_LSHIFT - dest_reg = src_reg_a + (src_reg_b << imm_data)
  Intended for EA calcluations, imm_data must be between 0 and 3*/
#define UOP_ADD_LSHIFT            (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | 0x34)
/*UOP_OR - dest_reg = src_reg_a | src_reg_b*/
#define UOP_OR                    (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | 0x35)
/*UOP_OR_IMM - dest_reg = src_reg_a | immediate*/
#define UOP_OR_IMM                (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | 0x36)
/*UOP_SUB - dest_reg = src_reg_a - src_reg_b*/
#define UOP_SUB                   (UOP_TYPE_PARAMS_REGS | 0x37)
/*UOP_SUB_IMM - dest_reg = src_reg_a - immediate*/
#define UOP_SUB_IMM               (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | 0x38)
/*UOP_XOR - dest_reg = src_reg_a ^ src_reg_b*/
#define UOP_XOR                   (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | 0x39)
/*UOP_XOR_IMM - dest_reg = src_reg_a ^ immediate*/
#define UOP_XOR_IMM               (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | 0x3a)
/*UOP_ANDN - dest_reg = ~src_reg_a & src_reg_b*/
#define UOP_ANDN                  (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | 0x3b)
/*UOP_MEM_LOAD_ABS - dest_reg = src_reg_a:[immediate]*/
#define UOP_MEM_LOAD_ABS          (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | 0x40 | UOP_TYPE_ORDER_BARRIER)
/*UOP_MEM_LOAD_REG - dest_reg = src_reg_a:[src_reg_b]*/
#define UOP_MEM_LOAD_REG          (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | 0x41 | UOP_TYPE_ORDER_BARRIER)
/*UOP_MEM_STORE_ABS - src_reg_a:[immediate] = src_reg_b*/
#define UOP_MEM_STORE_ABS         (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | 0x42 | UOP_TYPE_ORDER_BARRIER)
/*UOP_MEM_STORE_REG - src_reg_a:[src_reg_b] = src_reg_c*/
#define UOP_MEM_STORE_REG         (UOP_TYPE_PARAMS_REGS | 0x43 | UOP_TYPE_ORDER_BARRIER)
/*UOP_MEM_STORE_IMM_8 - byte src_reg_a:[src_reg_b] = imm_data*/
#define UOP_MEM_STORE_IMM_8       (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | 0x44 | UOP_TYPE_ORDER_BARRIER)
/*UOP_MEM_STORE_IMM_16 - word src_reg_a:[src_reg_b] = imm_data*/
#define UOP_MEM_STORE_IMM_16      (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | 0x45 | UOP_TYPE_ORDER_BARRIER)
/*UOP_MEM_STORE_IMM_32 - long src_reg_a:[src_reg_b] = imm_data*/
#define UOP_MEM_STORE_IMM_32      (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | 0x46 | UOP_TYPE_ORDER_BARRIER)
/*UOP_MEM_LOAD_SINGLE - dest_reg = (float)src_reg_a:[src_reg_b]*/
#define UOP_MEM_LOAD_SINGLE       (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | 0x47 | UOP_TYPE_ORDER_BARRIER)
/*UOP_CMP_IMM_JZ - if (src_reg_a == imm_data) then jump to ptr*/
#define UOP_CMP_IMM_JZ            (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | UOP_TYPE_PARAMS_POINTER | 0x48 | UOP_TYPE_ORDER_BARRIER)
/*UOP_MEM_LOAD_DOUBLE - dest_reg = (double)src_reg_a:[src_reg_b]*/
#define UOP_MEM_LOAD_DOUBLE       (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | 0x49 | UOP_TYPE_ORDER_BARRIER)
/*UOP_MEM_STORE_SINGLE - src_reg_a:[src_reg_b] = src_reg_c*/
#define UOP_MEM_STORE_SINGLE      (UOP_TYPE_PARAMS_REGS | 0x4a | UOP_TYPE_ORDER_BARRIER)
/*UOP_MEM_STORE_DOUBLE - src_reg_a:[src_reg_b] = src_reg_c*/
#define UOP_MEM_STORE_DOUBLE      (UOP_TYPE_PARAMS_REGS | 0x4b | UOP_TYPE_ORDER_BARRIER)
/*UOP_CMP_JB - if (src_reg_a < src_reg_b) then jump to ptr*/
#define UOP_CMP_JB                (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_POINTER | 0x4c | UOP_TYPE_ORDER_BARRIER)
/*UOP_CMP_JNBE - if (src_reg_a > src_reg_b) then jump to ptr*/
#define UOP_CMP_JNBE              (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_POINTER | 0x4d | UOP_TYPE_ORDER_BARRIER)

/*UOP_SAR - dest_reg = src_reg_a >> src_reg_b*/
#define UOP_SAR                   (UOP_TYPE_PARAMS_REGS | 0x50)
/*UOP_SAR_IMM - dest_reg = src_reg_a >> immediate*/
#define UOP_SAR_IMM               (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | 0x51)
/*UOP_SHL - dest_reg = src_reg_a << src_reg_b*/
#define UOP_SHL                   (UOP_TYPE_PARAMS_REGS | 0x52)
/*UOP_SHL_IMM - dest_reg = src_reg_a << immediate*/
#define UOP_SHL_IMM               (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | 0x53)
/*UOP_SHR - dest_reg = src_reg_a >> src_reg_b*/
#define UOP_SHR                   (UOP_TYPE_PARAMS_REGS | 0x54)
/*UOP_SHR_IMM - dest_reg = src_reg_a >> immediate*/
#define UOP_SHR_IMM               (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | 0x55)
/*UOP_ROL - dest_reg = src_reg_a rotate<< src_reg_b*/
#define UOP_ROL                   (UOP_TYPE_PARAMS_REGS | 0x56)
/*UOP_ROL_IMM - dest_reg = src_reg_a rotate<< immediate*/
#define UOP_ROL_IMM               (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | 0x57)
/*UOP_ROR - dest_reg = src_reg_a rotate>> src_reg_b*/
#define UOP_ROR                   (UOP_TYPE_PARAMS_REGS | 0x58)
/*UOP_ROR_IMM - dest_reg = src_reg_a rotate>> immediate*/
#define UOP_ROR_IMM               (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | 0x59)

/*UOP_CMP_IMM_JZ_DEST - if (src_reg_a == imm_data) then jump to ptr*/
#define UOP_CMP_IMM_JZ_DEST       (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | UOP_TYPE_PARAMS_POINTER | 0x60 | UOP_TYPE_ORDER_BARRIER | UOP_TYPE_JUMP)
/*UOP_CMP_IMM_JNZ_DEST - if (src_reg_a != imm_data) then jump to ptr*/
#define UOP_CMP_IMM_JNZ_DEST      (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | UOP_TYPE_PARAMS_POINTER | 0x61 | UOP_TYPE_ORDER_BARRIER | UOP_TYPE_JUMP)

/*UOP_CMP_JB_DEST - if (src_reg_a < src_reg_b) then jump to ptr*/
#define UOP_CMP_JB_DEST       (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | UOP_TYPE_PARAMS_POINTER | 0x62 | UOP_TYPE_ORDER_BARRIER | UOP_TYPE_JUMP)
/*UOP_CMP_JNB_DEST - if (src_reg_a >= src_reg_b) then jump to ptr*/
#define UOP_CMP_JNB_DEST      (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | UOP_TYPE_PARAMS_POINTER | 0x63 | UOP_TYPE_ORDER_BARRIER | UOP_TYPE_JUMP)
/*UOP_CMP_JO_DEST - if (src_reg_a < src_reg_b) then jump to ptr*/
#define UOP_CMP_JO_DEST       (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | UOP_TYPE_PARAMS_POINTER | 0x64 | UOP_TYPE_ORDER_BARRIER | UOP_TYPE_JUMP)
/*UOP_CMP_JNO_DEST - if (src_reg_a >= src_reg_b) then jump to ptr*/
#define UOP_CMP_JNO_DEST      (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | UOP_TYPE_PARAMS_POINTER | 0x65 | UOP_TYPE_ORDER_BARRIER | UOP_TYPE_JUMP)
/*UOP_CMP_JZ_DEST - if (src_reg_a == src_reg_b) then jump to ptr*/
#define UOP_CMP_JZ_DEST       (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | UOP_TYPE_PARAMS_POINTER | 0x66 | UOP_TYPE_ORDER_BARRIER | UOP_TYPE_JUMP)
/*UOP_CMP_JNZ_DEST - if (src_reg_a != src_reg_b) then jump to ptr*/
#define UOP_CMP_JNZ_DEST      (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | UOP_TYPE_PARAMS_POINTER | 0x67 | UOP_TYPE_ORDER_BARRIER | UOP_TYPE_JUMP)
/*UOP_CMP_JL_DEST - if (signed)(src_reg_a < src_reg_b) then jump to ptr*/
#define UOP_CMP_JL_DEST       (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | UOP_TYPE_PARAMS_POINTER | 0x68 | UOP_TYPE_ORDER_BARRIER | UOP_TYPE_JUMP)
/*UOP_CMP_JNL_DEST - if (signed)(src_reg_a >= src_reg_b) then jump to ptr*/
#define UOP_CMP_JNL_DEST      (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | UOP_TYPE_PARAMS_POINTER | 0x69 | UOP_TYPE_ORDER_BARRIER | UOP_TYPE_JUMP)
/*UOP_CMP_JBE_DEST - if (src_reg_a <= src_reg_b) then jump to ptr*/
#define UOP_CMP_JBE_DEST      (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | UOP_TYPE_PARAMS_POINTER | 0x6a | UOP_TYPE_ORDER_BARRIER | UOP_TYPE_JUMP)
/*UOP_CMP_JNBE_DEST - if (src_reg_a > src_reg_b) then jump to ptr*/
#define UOP_CMP_JNBE_DEST     (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | UOP_TYPE_PARAMS_POINTER | 0x6b | UOP_TYPE_ORDER_BARRIER | UOP_TYPE_JUMP)
/*UOP_CMP_JLE_DEST - if (signed)(src_reg_a <= src_reg_b) then jump to ptr*/
#define UOP_CMP_JLE_DEST      (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | UOP_TYPE_PARAMS_POINTER | 0x6c | UOP_TYPE_ORDER_BARRIER | UOP_TYPE_JUMP)
/*UOP_CMP_JNLE_DEST - if (signed)(src_reg_a > src_reg_b) then jump to ptr*/
#define UOP_CMP_JNLE_DEST     (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | UOP_TYPE_PARAMS_POINTER | 0x6d | UOP_TYPE_ORDER_BARRIER | UOP_TYPE_JUMP)


/*UOP_TEST_JNS_DEST - if (src_reg_a positive) then jump to ptr*/
#define UOP_TEST_JNS_DEST         (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | UOP_TYPE_PARAMS_POINTER | 0x70 | UOP_TYPE_ORDER_BARRIER | UOP_TYPE_JUMP)
/*UOP_TEST_JS_DEST - if (src_reg_a positive) then jump to ptr*/
#define UOP_TEST_JS_DEST          (UOP_TYPE_PARAMS_REGS | UOP_TYPE_PARAMS_IMM | UOP_TYPE_PARAMS_POINTER | 0x71 | UOP_TYPE_ORDER_BARRIER | UOP_TYPE_JUMP)

/*UOP_FP_ENTER - must be called before any FPU register accessed*/
#define UOP_FP_ENTER              (UOP_TYPE_PARAMS_IMM | 0x80 | UOP_TYPE_BARRIER)
/*UOP_FADD - (floating point) dest_reg = src_reg_a + src_reg_b*/
#define UOP_FADD                  (UOP_TYPE_PARAMS_REGS | 0x81)
/*UOP_FSUB - (floating point) dest_reg = src_reg_a - src_reg_b*/
#define UOP_FSUB                  (UOP_TYPE_PARAMS_REGS | 0x82)
/*UOP_FMUL - (floating point) dest_reg = src_reg_a * src_reg_b*/
#define UOP_FMUL                  (UOP_TYPE_PARAMS_REGS | 0x83)
/*UOP_FDIV - (floating point) dest_reg = src_reg_a / src_reg_b*/
#define UOP_FDIV                  (UOP_TYPE_PARAMS_REGS | 0x84)
/*UOP_FCOM - dest_reg = flags from compare(src_reg_a, src_reg_b)*/
#define UOP_FCOM                  (UOP_TYPE_PARAMS_REGS | 0x85)
/*UOP_FABS - dest_reg = fabs(src_reg_a)*/
#define UOP_FABS                  (UOP_TYPE_PARAMS_REGS | 0x86)
/*UOP_FCHS - dest_reg = fabs(src_reg_a)*/
#define UOP_FCHS                  (UOP_TYPE_PARAMS_REGS | 0x87)
/*UOP_FTST - dest_reg = flags from compare(src_reg_a, 0)*/
#define UOP_FTST                  (UOP_TYPE_PARAMS_REGS | 0x88)
/*UOP_FSQRT - dest_reg = fsqrt(src_reg_a)*/
#define UOP_FSQRT                 (UOP_TYPE_PARAMS_REGS | 0x89)

/*UOP_MMX_ENTER - must be called before any MMX registers accessed*/
#define UOP_MMX_ENTER             (UOP_TYPE_PARAMS_IMM | 0x90 | UOP_TYPE_BARRIER)
/*UOP_PADDB - (packed byte) dest_reg = src_reg_a + src_reg_b*/
#define UOP_PADDB                 (UOP_TYPE_PARAMS_REGS | 0x91)
/*UOP_PADDW - (packed word) dest_reg = src_reg_a + src_reg_b*/
#define UOP_PADDW                 (UOP_TYPE_PARAMS_REGS | 0x92)
/*UOP_PADDD - (packed long) dest_reg = src_reg_a + src_reg_b*/
#define UOP_PADDD                 (UOP_TYPE_PARAMS_REGS | 0x93)
/*UOP_PADDSB - (packed byte with signed saturation) dest_reg = src_reg_a + src_reg_b*/
#define UOP_PADDSB                (UOP_TYPE_PARAMS_REGS | 0x94)
/*UOP_PADDSW - (packed word with signed saturation) dest_reg = src_reg_a + src_reg_b*/
#define UOP_PADDSW                (UOP_TYPE_PARAMS_REGS | 0x95)
/*UOP_PADDUSB - (packed byte with unsigned saturation) dest_reg = src_reg_a + src_reg_b*/
#define UOP_PADDUSB               (UOP_TYPE_PARAMS_REGS | 0x96)
/*UOP_PADDUSW - (packed word with unsigned saturation) dest_reg = src_reg_a + src_reg_b*/
#define UOP_PADDUSW               (UOP_TYPE_PARAMS_REGS | 0x97)
/*UOP_PSUBB - (packed byte) dest_reg = src_reg_a - src_reg_b*/
#define UOP_PSUBB                 (UOP_TYPE_PARAMS_REGS | 0x98)
/*UOP_PSUBW - (packed word) dest_reg = src_reg_a - src_reg_b*/
#define UOP_PSUBW                 (UOP_TYPE_PARAMS_REGS | 0x99)
/*UOP_PSUBD - (packed long) dest_reg = src_reg_a - src_reg_b*/
#define UOP_PSUBD                 (UOP_TYPE_PARAMS_REGS | 0x9a)
/*UOP_PSUBSB - (packed byte with signed saturation) dest_reg = src_reg_a - src_reg_b*/
#define UOP_PSUBSB                (UOP_TYPE_PARAMS_REGS | 0x9b)
/*UOP_PSUBSW - (packed word with signed saturation) dest_reg = src_reg_a - src_reg_b*/
#define UOP_PSUBSW                (UOP_TYPE_PARAMS_REGS | 0x9c)
/*UOP_PSUBUSB - (packed byte with unsigned saturation) dest_reg = src_reg_a - src_reg_b*/
#define UOP_PSUBUSB               (UOP_TYPE_PARAMS_REGS | 0x9d)
/*UOP_PSUBUSW - (packed word with unsigned saturation) dest_reg = src_reg_a - src_reg_b*/
#define UOP_PSUBUSW               (UOP_TYPE_PARAMS_REGS | 0x9e)
/*UOP_PSLLW_IMM - (packed word) dest_reg = src_reg_a << immediate*/
#define UOP_PSLLW_IMM             (UOP_TYPE_PARAMS_REGS | 0x9f)
/*UOP_PSLLD_IMM - (packed long) dest_reg = src_reg_a << immediate*/
#define UOP_PSLLD_IMM             (UOP_TYPE_PARAMS_REGS | 0xa0)
/*UOP_PSLLQ_IMM - (packed quad) dest_reg = src_reg_a << immediate*/
#define UOP_PSLLQ_IMM             (UOP_TYPE_PARAMS_REGS | 0xa1)
/*UOP_PSRAW_IMM - (packed word) dest_reg = src_reg_a >> immediate*/
#define UOP_PSRAW_IMM             (UOP_TYPE_PARAMS_REGS | 0xa2)
/*UOP_PSRAD_IMM - (packed long) dest_reg = src_reg_a >> immediate*/
#define UOP_PSRAD_IMM             (UOP_TYPE_PARAMS_REGS | 0xa3)
/*UOP_PSRAQ_IMM - (packed quad) dest_reg = src_reg_a >> immediate*/
#define UOP_PSRAQ_IMM             (UOP_TYPE_PARAMS_REGS | 0xa4)
/*UOP_PSRLW_IMM - (packed word) dest_reg = src_reg_a >> immediate*/
#define UOP_PSRLW_IMM             (UOP_TYPE_PARAMS_REGS | 0xa5)
/*UOP_PSRLD_IMM - (packed long) dest_reg = src_reg_a >> immediate*/
#define UOP_PSRLD_IMM             (UOP_TYPE_PARAMS_REGS | 0xa6)
/*UOP_PSRLQ_IMM - (packed quad) dest_reg = src_reg_a >> immediate*/
#define UOP_PSRLQ_IMM             (UOP_TYPE_PARAMS_REGS | 0xa7)
/*UOP_PCMPEQB - (packed byte) dest_reg = (src_reg_a == src_reg_b) ? ~0 : 0*/
#define UOP_PCMPEQB               (UOP_TYPE_PARAMS_REGS | 0xa8)
/*UOP_PCMPEQW - (packed word) dest_reg = (src_reg_a == src_reg_b) ? ~0 : 0*/
#define UOP_PCMPEQW               (UOP_TYPE_PARAMS_REGS | 0xa9)
/*UOP_PCMPEQD - (packed long) dest_reg = (src_reg_a == src_reg_b) ? ~0 : 0*/
#define UOP_PCMPEQD               (UOP_TYPE_PARAMS_REGS | 0xaa)
/*UOP_PCMPGTB - (packed signed byte) dest_reg = (src_reg_a > src_reg_b) ? ~0 : 0*/
#define UOP_PCMPGTB               (UOP_TYPE_PARAMS_REGS | 0xab)
/*UOP_PCMPGTW - (packed signed word) dest_reg = (src_reg_a > src_reg_b) ? ~0 : 0*/
#define UOP_PCMPGTW               (UOP_TYPE_PARAMS_REGS | 0xac)
/*UOP_PCMPGTD - (packed signed long) dest_reg = (src_reg_a > src_reg_b) ? ~0 : 0*/
#define UOP_PCMPGTD               (UOP_TYPE_PARAMS_REGS | 0xad)
/*UOP_PUNPCKLBW - (packed byte) dest_reg = interleave low src_reg_a/src_reg_b*/
#define UOP_PUNPCKLBW             (UOP_TYPE_PARAMS_REGS | 0xae)
/*UOP_PUNPCKLWD - (packed word) dest_reg = interleave low src_reg_a/src_reg_b*/
#define UOP_PUNPCKLWD             (UOP_TYPE_PARAMS_REGS | 0xaf)
/*UOP_PUNPCKLDQ - (packed long) dest_reg = interleave low src_reg_a/src_reg_b*/
#define UOP_PUNPCKLDQ             (UOP_TYPE_PARAMS_REGS | 0xb0)
/*UOP_PUNPCKHBW - (packed byte) dest_reg = interleave high src_reg_a/src_reg_b*/
#define UOP_PUNPCKHBW             (UOP_TYPE_PARAMS_REGS | 0xb1)
/*UOP_PUNPCKHWD - (packed word) dest_reg = interleave high src_reg_a/src_reg_b*/
#define UOP_PUNPCKHWD             (UOP_TYPE_PARAMS_REGS | 0xb2)
/*UOP_PUNPCKHDQ - (packed long) dest_reg = interleave high src_reg_a/src_reg_b*/
#define UOP_PUNPCKHDQ             (UOP_TYPE_PARAMS_REGS | 0xb3)
/*UOP_PACKSSWB - dest_reg = interleave src_reg_a/src_reg_b, converting words to bytes with signed saturation*/
#define UOP_PACKSSWB              (UOP_TYPE_PARAMS_REGS | 0xb4)
/*UOP_PACKSSDW - dest_reg = interleave src_reg_a/src_reg_b, converting longs to words with signed saturation*/
#define UOP_PACKSSDW              (UOP_TYPE_PARAMS_REGS | 0xb5)
/*UOP_PACKUSWB - dest_reg = interleave src_reg_a/src_reg_b, converting words to bytes with unsigned saturation*/
#define UOP_PACKUSWB              (UOP_TYPE_PARAMS_REGS | 0xb6)
/*UOP_PMULLW - (packed word) dest_reg = (src_reg_a * src_reg_b) & 0xffff*/
#define UOP_PMULLW                (UOP_TYPE_PARAMS_REGS | 0xb7)
/*UOP_PMULHW - (packed word) dest_reg = (src_reg_a * src_reg_b) >> 16*/
#define UOP_PMULHW                (UOP_TYPE_PARAMS_REGS | 0xb8)
/*UOP_PMADDWD - (packed word) dest_reg = (src_reg_a * src_reg_b) >> 16*/
#define UOP_PMADDWD               (UOP_TYPE_PARAMS_REGS | 0xb9)

/*UOP_PFADD - (packed float) dest_reg = src_reg_a + src_reg_b*/
#define UOP_PFADD                 (UOP_TYPE_PARAMS_REGS | 0xba)
/*UOP_PFSUB - (packed float) dest_reg = src_reg_a - src_reg_b*/
#define UOP_PFSUB                 (UOP_TYPE_PARAMS_REGS | 0xbb)
/*UOP_PFMUL - (packed float) dest_reg = src_reg_a * src_reg_b*/
#define UOP_PFMUL                 (UOP_TYPE_PARAMS_REGS | 0xbc)
/*UOP_PFMAX - (packed float) dest_reg = MAX(src_reg_a, src_reg_b)*/
#define UOP_PFMAX                 (UOP_TYPE_PARAMS_REGS | 0xbd)
/*UOP_PFMIN - (packed float) dest_reg = MIN(src_reg_a, src_reg_b)*/
#define UOP_PFMIN                 (UOP_TYPE_PARAMS_REGS | 0xbe)
/*UOP_PFCMPEQ - (packed float) dest_reg = (src_reg_a == src_reg_b) ? ~0 : 0*/
#define UOP_PFCMPEQ               (UOP_TYPE_PARAMS_REGS | 0xbf)
/*UOP_PFCMPGE - (packed float) dest_reg = (src_reg_a >= src_reg_b) ? ~0 : 0*/
#define UOP_PFCMPGE               (UOP_TYPE_PARAMS_REGS | 0xc0)
/*UOP_PFCMPGT - (packed float) dest_reg = (src_reg_a > src_reg_b) ? ~0 : 0*/
#define UOP_PFCMPGT               (UOP_TYPE_PARAMS_REGS | 0xc1)
/*UOP_PF2ID - (packed long)dest_reg = (packed float)src_reg_a*/
#define UOP_PF2ID                 (UOP_TYPE_PARAMS_REGS | 0xc2)
/*UOP_PI2FD - (packed float)dest_reg = (packed long)src_reg_a*/
#define UOP_PI2FD                 (UOP_TYPE_PARAMS_REGS | 0xc3)
/*UOP_PFRCP - (packed float) dest_reg[0] = dest_reg[1] = 1.0 / src_reg[0]*/
#define UOP_PFRCP                 (UOP_TYPE_PARAMS_REGS | 0xc4)
/*UOP_PFRSQRT - (packed float) dest_reg[0] = dest_reg[1] = 1.0 / sqrt(src_reg[0])*/
#define UOP_PFRSQRT               (UOP_TYPE_PARAMS_REGS | 0xc5)

#define UOP_MAX 0xc6

#define UOP_INVALID 0xff

#define UOP_MASK 0xffff

typedef struct uop_t
{
        uint32_t type;
        ir_reg_t dest_reg_a;
        ir_reg_t src_reg_a;
        ir_reg_t src_reg_b;
        ir_reg_t src_reg_c;
        uint32_t imm_data;
        void *p;
        ir_host_reg_t dest_reg_a_real;
        ir_host_reg_t src_reg_a_real, src_reg_b_real, src_reg_c_real;
        int jump_dest_uop;
        int jump_list_next;
        void *jump_dest;
        uint32_t pc;
} uop_t;

#define UOP_NR_MAX 4096

typedef struct ir_data_t
{
        uop_t uops[UOP_NR_MAX];
        int wr_pos;
        struct codeblock_t *block;
} ir_data_t;

static inline uop_t *uop_alloc(ir_data_t *ir, uint32_t uop_type)
{
        uop_t *uop;
        
        if (ir->wr_pos >= UOP_NR_MAX)
                fatal("Exceeded uOP max\n");
        
        uop = &ir->uops[ir->wr_pos++];
        
        uop->dest_reg_a = invalid_ir_reg;
        uop->src_reg_a = invalid_ir_reg;
        uop->src_reg_b = invalid_ir_reg;
        uop->src_reg_c = invalid_ir_reg;
        
        uop->pc = cpu_state.oldpc;
        
        uop->jump_dest_uop = -1;
        uop->jump_list_next = -1;

        if (uop_type & (UOP_TYPE_BARRIER | UOP_TYPE_ORDER_BARRIER))
                codegen_reg_mark_as_required();

        return uop;
}

static inline void uop_set_jump_dest(ir_data_t *ir, int jump_uop)
{
        uop_t *uop = &ir->uops[jump_uop];
        
        uop->jump_dest_uop = ir->wr_pos;
}

static inline int uop_gen(uint32_t uop_type, ir_data_t *ir)
{
        uop_t *uop = uop_alloc(ir, uop_type);

        uop->type = uop_type;

        return ir->wr_pos-1;
}

static inline int uop_gen_reg_src1(uint32_t uop_type, ir_data_t *ir, int src_reg_a)
{
        uop_t *uop = uop_alloc(ir, uop_type);
        
        uop->type = uop_type;
        uop->src_reg_a = codegen_reg_read(src_reg_a);

        return ir->wr_pos-1;
}

static inline void uop_gen_reg_src1_arg(uint32_t uop_type, ir_data_t *ir, int arg, int src_reg_a)
{
        uop_t *uop = uop_alloc(ir, uop_type);

        uop->type = uop_type;
        uop->src_reg_a = codegen_reg_read(src_reg_a);
}

static inline int uop_gen_reg_src1_imm(uint32_t uop_type, ir_data_t *ir, int src_reg, uint32_t imm)
{
        uop_t *uop = uop_alloc(ir, uop_type);

        uop->type = uop_type;
        uop->src_reg_a = codegen_reg_read(src_reg);
        uop->imm_data = imm;
        
        return ir->wr_pos-1;
}

static inline void uop_gen_reg_dst_imm(uint32_t uop_type, ir_data_t *ir, int dest_reg, uint32_t imm)
{
        uop_t *uop = uop_alloc(ir, uop_type);

        uop->type = uop_type;
        uop->dest_reg_a = codegen_reg_write(dest_reg, ir->wr_pos - 1);
        uop->imm_data = imm;
}

static inline void uop_gen_reg_dst_pointer(uint32_t uop_type, ir_data_t *ir, int dest_reg, void *p)
{
        uop_t *uop = uop_alloc(ir, uop_type);

        uop->type = uop_type;
        uop->dest_reg_a = codegen_reg_write(dest_reg, ir->wr_pos - 1);
        uop->p = p;
}

static inline void uop_gen_reg_dst_src1(uint32_t uop_type, ir_data_t *ir, int dest_reg, int src_reg)
{
        uop_t *uop = uop_alloc(ir, uop_type);

        uop->type = uop_type;
        uop->src_reg_a = codegen_reg_read(src_reg);
        uop->dest_reg_a = codegen_reg_write(dest_reg, ir->wr_pos - 1);
}

static inline void uop_gen_reg_dst_src1_imm(uint32_t uop_type, ir_data_t *ir, int dest_reg, int src_reg_a, uint32_t imm)
{
        uop_t *uop = uop_alloc(ir, uop_type);

        uop->type = uop_type;
        uop->src_reg_a = codegen_reg_read(src_reg_a);
        uop->dest_reg_a = codegen_reg_write(dest_reg, ir->wr_pos - 1);
        uop->imm_data = imm;
}

static inline void uop_gen_reg_dst_src2(uint32_t uop_type, ir_data_t *ir, int dest_reg, int src_reg_a, int src_reg_b)
{
        uop_t *uop = uop_alloc(ir, uop_type);

        uop->type = uop_type;
        uop->src_reg_a = codegen_reg_read(src_reg_a);
        uop->src_reg_b = codegen_reg_read(src_reg_b);
        uop->dest_reg_a = codegen_reg_write(dest_reg, ir->wr_pos - 1);
}

static inline void uop_gen_reg_dst_src2_imm(uint32_t uop_type, ir_data_t *ir, int dest_reg, int src_reg_a, int src_reg_b, uint32_t imm)
{
        uop_t *uop = uop_alloc(ir, uop_type);

        uop->type = uop_type;
        uop->src_reg_a = codegen_reg_read(src_reg_a);
        uop->src_reg_b = codegen_reg_read(src_reg_b);
        uop->dest_reg_a = codegen_reg_write(dest_reg, ir->wr_pos - 1);
        uop->imm_data = imm;
}

static inline void uop_gen_reg_dst_src3(uint32_t uop_type, ir_data_t *ir, int dest_reg, int src_reg_a, int src_reg_b, int src_reg_c)
{
        uop_t *uop = uop_alloc(ir, uop_type);

        uop->type = uop_type;
        uop->src_reg_a = codegen_reg_read(src_reg_a);
        uop->src_reg_b = codegen_reg_read(src_reg_b);
        uop->src_reg_c = codegen_reg_read(src_reg_c);
        uop->dest_reg_a = codegen_reg_write(dest_reg, ir->wr_pos - 1);
}

static inline void uop_gen_reg_dst_src_imm(uint32_t uop_type, ir_data_t *ir, int dest_reg, int src_reg, uint32_t imm)
{
        uop_t *uop = uop_alloc(ir, uop_type);

        uop->type = uop_type;
        uop->src_reg_a = codegen_reg_read(src_reg);
        uop->dest_reg_a = codegen_reg_write(dest_reg, ir->wr_pos - 1);
        uop->imm_data = imm;
}

static inline int uop_gen_reg_src2(uint32_t uop_type, ir_data_t *ir, int src_reg_a, int src_reg_b)
{
        uop_t *uop = uop_alloc(ir, uop_type);

        uop->type = uop_type;
        uop->src_reg_a = codegen_reg_read(src_reg_a);
        uop->src_reg_b = codegen_reg_read(src_reg_b);
        
        return ir->wr_pos-1;
}

static inline void uop_gen_reg_src2_imm(uint32_t uop_type, ir_data_t *ir, int src_reg_a, int src_reg_b, uint32_t imm)
{
        uop_t *uop = uop_alloc(ir, uop_type);

        uop->type = uop_type;
        uop->src_reg_a = codegen_reg_read(src_reg_a);
        uop->src_reg_b = codegen_reg_read(src_reg_b);
        uop->imm_data = imm;
}

static inline void uop_gen_reg_src3(uint32_t uop_type, ir_data_t *ir, int src_reg_a, int src_reg_b, int src_reg_c)
{
        uop_t *uop = uop_alloc(ir, uop_type);

        uop->type = uop_type;
        uop->src_reg_a = codegen_reg_read(src_reg_a);
        uop->src_reg_b = codegen_reg_read(src_reg_b);
        uop->src_reg_c = codegen_reg_read(src_reg_c);
}

static inline void uop_gen_reg_src3_imm(uint32_t uop_type, ir_data_t *ir, int src_reg_a, int src_reg_b, int src_reg_c, uint32_t imm)
{
        uop_t *uop = uop_alloc(ir, uop_type);

        uop->type = uop_type;
        uop->src_reg_a = codegen_reg_read(src_reg_a);
        uop->src_reg_b = codegen_reg_read(src_reg_b);
        uop->src_reg_c = codegen_reg_read(src_reg_c);
        uop->imm_data = imm;
}

static inline void uop_gen_imm(uint32_t uop_type, ir_data_t *ir, uint32_t imm)
{
        uop_t *uop = uop_alloc(ir, uop_type);
        
        uop->type = uop_type;
        uop->imm_data = imm;
}

static inline void uop_gen_pointer(uint32_t uop_type, ir_data_t *ir, void *p)
{
        uop_t *uop = uop_alloc(ir, uop_type);
        
        uop->type = uop_type;
        uop->p = p;
}

static inline void uop_gen_pointer_imm(uint32_t uop_type, ir_data_t *ir, void *p, uint32_t imm)
{
        uop_t *uop = uop_alloc(ir, uop_type);
        
        uop->type = uop_type;
        uop->p = p;
        uop->imm_data = imm;
}

static inline void uop_gen_reg_src_pointer(uint32_t uop_type, ir_data_t *ir, int src_reg_a, void *p)
{
        uop_t *uop = uop_alloc(ir, uop_type);

        uop->type = uop_type;
        uop->src_reg_a = codegen_reg_read(src_reg_a);
        uop->p = p;
}

static inline void uop_gen_reg_src_pointer_imm(uint32_t uop_type, ir_data_t *ir, int src_reg_a, void *p, uint32_t imm)
{
        uop_t *uop = uop_alloc(ir, uop_type);

        uop->type = uop_type;
        uop->src_reg_a = codegen_reg_read(src_reg_a);
        uop->p = p;
        uop->imm_data = imm;
}

static inline void uop_gen_reg_src2_pointer(uint32_t uop_type, ir_data_t *ir, int src_reg_a, int src_reg_b, void *p)
{
        uop_t *uop = uop_alloc(ir, uop_type);

        uop->type = uop_type;
        uop->src_reg_a = codegen_reg_read(src_reg_a);
        uop->src_reg_b = codegen_reg_read(src_reg_b);
        uop->p = p;
}

#define uop_LOAD_FUNC_ARG_REG(ir, arg, reg)  uop_gen_reg_src1(UOP_LOAD_FUNC_ARG_0 + arg, ir, reg)

#define uop_LOAD_FUNC_ARG_IMM(ir, arg, imm)  uop_gen_imm(UOP_LOAD_FUNC_ARG_0_IMM + arg, ir, imm)

#define uop_ADD(ir, dst_reg, src_reg_a, src_reg_b)  uop_gen_reg_dst_src2(UOP_ADD, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_ADD_IMM(ir, dst_reg, src_reg, imm)      uop_gen_reg_dst_src_imm(UOP_ADD_IMM, ir, dst_reg, src_reg, imm)
#define uop_ADD_LSHIFT(ir, dst_reg, src_reg_a, src_reg_b, shift) uop_gen_reg_dst_src2_imm(UOP_ADD_LSHIFT, ir, dst_reg, src_reg_a, src_reg_b, shift)
#define uop_AND(ir, dst_reg, src_reg_a, src_reg_b)  uop_gen_reg_dst_src2(UOP_AND, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_AND_IMM(ir, dst_reg, src_reg, imm)      uop_gen_reg_dst_src_imm(UOP_AND_IMM, ir, dst_reg, src_reg, imm)
#define uop_ANDN(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_ANDN, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_OR(ir, dst_reg, src_reg_a, src_reg_b)   uop_gen_reg_dst_src2(UOP_OR, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_OR_IMM(ir, dst_reg, src_reg, imm)       uop_gen_reg_dst_src_imm(UOP_OR_IMM, ir, dst_reg, src_reg, imm)
#define uop_SUB(ir, dst_reg, src_reg_a, src_reg_b)  uop_gen_reg_dst_src2(UOP_SUB, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_SUB_IMM(ir, dst_reg, src_reg, imm)      uop_gen_reg_dst_src_imm(UOP_SUB_IMM, ir, dst_reg, src_reg, imm)
#define uop_XOR(ir, dst_reg, src_reg_a, src_reg_b)  uop_gen_reg_dst_src2(UOP_XOR, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_XOR_IMM(ir, dst_reg, src_reg, imm)      uop_gen_reg_dst_src_imm(UOP_XOR_IMM, ir, dst_reg, src_reg, imm)

#define uop_SAR(ir, dst_reg, src_reg, shift_reg)   uop_gen_reg_dst_src2(UOP_SAR, ir, dst_reg, src_reg, shift_reg)
#define uop_SAR_IMM(ir, dst_reg, src_reg, imm)     uop_gen_reg_dst_src_imm(UOP_SAR_IMM, ir, dst_reg, src_reg, imm)
#define uop_SHL(ir, dst_reg, src_reg, shift_reg)   uop_gen_reg_dst_src2(UOP_SHL, ir, dst_reg, src_reg, shift_reg)
#define uop_SHL_IMM(ir, dst_reg, src_reg, imm)     uop_gen_reg_dst_src_imm(UOP_SHL_IMM, ir, dst_reg, src_reg, imm)
#define uop_SHR(ir, dst_reg, src_reg, shift_reg)   uop_gen_reg_dst_src2(UOP_SHR, ir, dst_reg, src_reg, shift_reg)
#define uop_SHR_IMM(ir, dst_reg, src_reg, imm)     uop_gen_reg_dst_src_imm(UOP_SHR_IMM, ir, dst_reg, src_reg, imm)
#define uop_ROL(ir, dst_reg, src_reg, shift_reg)   uop_gen_reg_dst_src2(UOP_ROL, ir, dst_reg, src_reg, shift_reg)
#define uop_ROL_IMM(ir, dst_reg, src_reg, imm)     uop_gen_reg_dst_src_imm(UOP_ROL_IMM, ir, dst_reg, src_reg, imm)
#define uop_ROR(ir, dst_reg, src_reg, shift_reg)   uop_gen_reg_dst_src2(UOP_ROR, ir, dst_reg, src_reg, shift_reg)
#define uop_ROR_IMM(ir, dst_reg, src_reg, imm)     uop_gen_reg_dst_src_imm(UOP_ROR_IMM, ir, dst_reg, src_reg, imm)

#define uop_CALL_FUNC(ir, p)                 uop_gen_pointer(UOP_CALL_FUNC, ir, p)
#define uop_CALL_FUNC_RESULT(ir, dst_reg, p) uop_gen_reg_dst_pointer(UOP_CALL_FUNC_RESULT, ir, dst_reg, p)
#define uop_CALL_INSTRUCTION_FUNC(ir, p)     uop_gen_pointer(UOP_CALL_INSTRUCTION_FUNC, ir, p)

#define uop_CMP_IMM_JZ(ir, src_reg, imm, p)  uop_gen_reg_src_pointer_imm(UOP_CMP_IMM_JZ, ir, src_reg, p, imm)

#define uop_CMP_IMM_JNZ_DEST(ir, src_reg, imm) uop_gen_reg_src1_imm(UOP_CMP_IMM_JNZ_DEST, ir, src_reg, imm)
#define uop_CMP_IMM_JZ_DEST(ir, src_reg, imm)  uop_gen_reg_src1_imm(UOP_CMP_IMM_JZ_DEST, ir, src_reg, imm)

#define uop_CMP_JB(ir, src_reg_a, src_reg_b, p)   uop_gen_reg_src2_pointer(UOP_CMP_JB, ir, src_reg_a, src_reg_b, p)
#define uop_CMP_JNBE(ir, src_reg_a, src_reg_b, p) uop_gen_reg_src2_pointer(UOP_CMP_JNBE, ir, src_reg_a, src_reg_b, p)

#define uop_CMP_JNB_DEST(ir, src_reg_a, src_reg_b)  uop_gen_reg_src2(UOP_CMP_JNB_DEST, ir, src_reg_a, src_reg_b)
#define uop_CMP_JNBE_DEST(ir, src_reg_a, src_reg_b) uop_gen_reg_src2(UOP_CMP_JNBE_DEST, ir, src_reg_a, src_reg_b)
#define uop_CMP_JNL_DEST(ir, src_reg_a, src_reg_b)  uop_gen_reg_src2(UOP_CMP_JNL_DEST, ir, src_reg_a, src_reg_b)
#define uop_CMP_JNLE_DEST(ir, src_reg_a, src_reg_b) uop_gen_reg_src2(UOP_CMP_JNLE_DEST, ir, src_reg_a, src_reg_b)
#define uop_CMP_JNO_DEST(ir, src_reg_a, src_reg_b)  uop_gen_reg_src2(UOP_CMP_JNO_DEST, ir, src_reg_a, src_reg_b)
#define uop_CMP_JNZ_DEST(ir, src_reg_a, src_reg_b)  uop_gen_reg_src2(UOP_CMP_JNZ_DEST, ir, src_reg_a, src_reg_b)
#define uop_CMP_JB_DEST(ir, src_reg_a, src_reg_b)   uop_gen_reg_src2(UOP_CMP_JB_DEST, ir, src_reg_a, src_reg_b)
#define uop_CMP_JBE_DEST(ir, src_reg_a, src_reg_b)  uop_gen_reg_src2(UOP_CMP_JBE_DEST, ir, src_reg_a, src_reg_b)
#define uop_CMP_JL_DEST(ir, src_reg_a, src_reg_b)   uop_gen_reg_src2(UOP_CMP_JL_DEST, ir, src_reg_a, src_reg_b)
#define uop_CMP_JLE_DEST(ir, src_reg_a, src_reg_b)  uop_gen_reg_src2(UOP_CMP_JLE_DEST, ir, src_reg_a, src_reg_b)
#define uop_CMP_JO_DEST(ir, src_reg_a, src_reg_b)   uop_gen_reg_src2(UOP_CMP_JO_DEST, ir, src_reg_a, src_reg_b)
#define uop_CMP_JZ_DEST(ir, src_reg_a, src_reg_b)   uop_gen_reg_src2(UOP_CMP_JZ_DEST, ir, src_reg_a, src_reg_b)

#define uop_FADD(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_FADD, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_FCOM(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_FCOM, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_FDIV(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_FDIV, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_FMUL(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_FMUL, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_FSUB(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_FSUB, ir, dst_reg, src_reg_a, src_reg_b)

#define uop_FABS(ir, dst_reg, src_reg) uop_gen_reg_dst_src1(UOP_FABS, ir, dst_reg, src_reg)
#define uop_FCHS(ir, dst_reg, src_reg) uop_gen_reg_dst_src1(UOP_FCHS, ir, dst_reg, src_reg)
#define uop_FSQRT(ir, dst_reg, src_reg) uop_gen_reg_dst_src1(UOP_FSQRT, ir, dst_reg, src_reg)
#define uop_FTST(ir, dst_reg, src_reg) uop_gen_reg_dst_src1(UOP_FTST, ir, dst_reg, src_reg)

#define uop_FP_ENTER(ir)                 do { if (!codegen_fpu_entered) uop_gen_imm(UOP_FP_ENTER,  ir, cpu_state.oldpc); codegen_fpu_entered = 1; codegen_mmx_entered = 0; } while (0)
#define uop_MMX_ENTER(ir)                do { if (!codegen_mmx_entered) uop_gen_imm(UOP_MMX_ENTER, ir, cpu_state.oldpc); codegen_mmx_entered = 1; codegen_fpu_entered = 0; } while (0)

#define uop_JMP(ir, p)                   uop_gen_pointer(UOP_JMP, ir, p)
#define uop_JMP_DEST(ir)                 uop_gen(UOP_JMP_DEST, ir)

#define uop_LOAD_SEG(ir, p, src_reg) uop_gen_reg_src_pointer(UOP_LOAD_SEG, ir, src_reg, p)

#define uop_MEM_LOAD_ABS(ir, dst_reg, seg_reg, imm) uop_gen_reg_dst_src_imm(UOP_MEM_LOAD_ABS, ir, dst_reg, seg_reg, imm)
#define uop_MEM_LOAD_REG(ir, dst_reg, seg_reg, addr_reg) uop_gen_reg_dst_src2_imm(UOP_MEM_LOAD_REG, ir, dst_reg, seg_reg, addr_reg, 0)
#define uop_MEM_LOAD_REG_OFFSET(ir, dst_reg, seg_reg, addr_reg, offset) uop_gen_reg_dst_src2_imm(UOP_MEM_LOAD_REG, ir, dst_reg, seg_reg, addr_reg, offset)
#define uop_MEM_LOAD_SINGLE(ir, dst_reg, seg_reg, addr_reg) uop_gen_reg_dst_src2_imm(UOP_MEM_LOAD_SINGLE, ir, dst_reg, seg_reg, addr_reg, 0)
#define uop_MEM_LOAD_DOUBLE(ir, dst_reg, seg_reg, addr_reg) uop_gen_reg_dst_src2_imm(UOP_MEM_LOAD_DOUBLE, ir, dst_reg, seg_reg, addr_reg, 0)
#define uop_MEM_STORE_ABS(ir, seg_reg, imm, src_reg) uop_gen_reg_src2_imm(UOP_MEM_STORE_ABS, ir, seg_reg, src_reg, imm)
#define uop_MEM_STORE_REG(ir, seg_reg, addr_reg, src_reg) uop_gen_reg_src3_imm(UOP_MEM_STORE_REG, ir, seg_reg, addr_reg, src_reg, 0)
#define uop_MEM_STORE_REG_OFFSET(ir, seg_reg, addr_reg, offset, src_reg) uop_gen_reg_src3_imm(UOP_MEM_STORE_REG, ir, seg_reg, addr_reg, src_reg, offset)
#define uop_MEM_STORE_IMM_8(ir, seg_reg, addr_reg, imm) uop_gen_reg_src2_imm(UOP_MEM_STORE_IMM_8, ir, seg_reg, addr_reg, imm)
#define uop_MEM_STORE_IMM_16(ir, seg_reg, addr_reg, imm) uop_gen_reg_src2_imm(UOP_MEM_STORE_IMM_16, ir, seg_reg, addr_reg, imm)
#define uop_MEM_STORE_IMM_32(ir, seg_reg, addr_reg, imm) uop_gen_reg_src2_imm(UOP_MEM_STORE_IMM_32, ir, seg_reg, addr_reg, imm)
#define uop_MEM_STORE_SINGLE(ir, seg_reg, addr_reg, src_reg) uop_gen_reg_src3_imm(UOP_MEM_STORE_SINGLE, ir, seg_reg, addr_reg, src_reg, 0)
#define uop_MEM_STORE_DOUBLE(ir, seg_reg, addr_reg, src_reg) uop_gen_reg_src3_imm(UOP_MEM_STORE_DOUBLE, ir, seg_reg, addr_reg, src_reg, 0)

#define uop_MOV(ir, dst_reg, src_reg)            uop_gen_reg_dst_src1(UOP_MOV, ir, dst_reg, src_reg)
#define uop_MOV_IMM(ir, reg, imm)                uop_gen_reg_dst_imm(UOP_MOV_IMM, ir, reg, imm)
#define uop_MOV_PTR(ir, reg, p)                  uop_gen_reg_dst_pointer(UOP_MOV_PTR, ir, reg, p)
#define uop_MOV_REG_PTR(ir, reg, p)              uop_gen_reg_dst_pointer(UOP_MOV_REG_PTR, ir, reg, p)
#define uop_MOVZX_REG_PTR_8(ir, reg, p)          uop_gen_reg_dst_pointer(UOP_MOVZX_REG_PTR_8, ir, reg, p)
#define uop_MOVZX_REG_PTR_16(ir, reg, p)         uop_gen_reg_dst_pointer(UOP_MOVZX_REG_PTR_16, ir, reg, p)
#define uop_MOVSX(ir, dst_reg, src_reg)          uop_gen_reg_dst_src1(UOP_MOVSX, ir, dst_reg, src_reg)
#define uop_MOVZX(ir, dst_reg, src_reg)          uop_gen_reg_dst_src1(UOP_MOVZX, ir, dst_reg, src_reg)
#define uop_MOV_DOUBLE_INT(ir, dst_reg, src_reg) uop_gen_reg_dst_src1(UOP_MOV_DOUBLE_INT, ir, dst_reg, src_reg)
#define uop_MOV_INT_DOUBLE(ir, dst_reg, src_reg/*, nrc, orc*/) uop_gen_reg_dst_src1(UOP_MOV_INT_DOUBLE, ir, dst_reg, src_reg/*, nrc, orc*/)
#define uop_MOV_INT_DOUBLE_64(ir, dst_reg, src_reg_d, src_reg_q, tag) uop_gen_reg_dst_src3(UOP_MOV_INT_DOUBLE_64, ir, dst_reg, src_reg_d, src_reg_q, tag)

#define uop_NOP_BARRIER(ir) uop_gen(UOP_NOP_BARRIER, ir)

#define uop_PACKSSWB(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_PACKSSWB, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PACKSSDW(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_PACKSSDW, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PACKUSWB(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_PACKUSWB, ir, dst_reg, src_reg_a, src_reg_b)

#define uop_PADDB(ir, dst_reg, src_reg_a, src_reg_b)   uop_gen_reg_dst_src2(UOP_PADDB, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PADDW(ir, dst_reg, src_reg_a, src_reg_b)   uop_gen_reg_dst_src2(UOP_PADDW, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PADDD(ir, dst_reg, src_reg_a, src_reg_b)   uop_gen_reg_dst_src2(UOP_PADDD, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PADDSB(ir, dst_reg, src_reg_a, src_reg_b)  uop_gen_reg_dst_src2(UOP_PADDSB, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PADDSW(ir, dst_reg, src_reg_a, src_reg_b)  uop_gen_reg_dst_src2(UOP_PADDSW, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PADDUSB(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_PADDUSB, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PADDUSW(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_PADDUSW, ir, dst_reg, src_reg_a, src_reg_b)

#define uop_PCMPEQB(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_PCMPEQB, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PCMPEQW(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_PCMPEQW, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PCMPEQD(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_PCMPEQD, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PCMPGTB(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_PCMPGTB, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PCMPGTW(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_PCMPGTW, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PCMPGTD(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_PCMPGTD, ir, dst_reg, src_reg_a, src_reg_b)

#define uop_PF2ID(ir, dst_reg, src_reg)                uop_gen_reg_dst_src1(UOP_PF2ID, ir, dst_reg, src_reg)
#define uop_PFADD(ir, dst_reg, src_reg_a, src_reg_b)   uop_gen_reg_dst_src2(UOP_PFADD, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PFCMPEQ(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_PFCMPEQ, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PFCMPGE(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_PFCMPGE, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PFCMPGT(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_PFCMPGT, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PFMAX(ir, dst_reg, src_reg_a, src_reg_b)   uop_gen_reg_dst_src2(UOP_PFMAX, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PFMIN(ir, dst_reg, src_reg_a, src_reg_b)   uop_gen_reg_dst_src2(UOP_PFMIN, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PFMUL(ir, dst_reg, src_reg_a, src_reg_b)   uop_gen_reg_dst_src2(UOP_PFMUL, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PFRCP(ir, dst_reg, src_reg)                uop_gen_reg_dst_src1(UOP_PFRCP, ir, dst_reg, src_reg)
#define uop_PFRSQRT(ir, dst_reg, src_reg)              uop_gen_reg_dst_src1(UOP_PFRSQRT, ir, dst_reg, src_reg)
#define uop_PFSUB(ir, dst_reg, src_reg_a, src_reg_b)   uop_gen_reg_dst_src2(UOP_PFSUB, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PI2FD(ir, dst_reg, src_reg)                uop_gen_reg_dst_src1(UOP_PI2FD, ir, dst_reg, src_reg)

#define uop_PMADDWD(ir, dst_reg, src_reg_a, src_reg_b)  uop_gen_reg_dst_src2(UOP_PMADDWD, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PMULHW(ir, dst_reg, src_reg_a, src_reg_b)  uop_gen_reg_dst_src2(UOP_PMULHW, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PMULLW(ir, dst_reg, src_reg_a, src_reg_b)  uop_gen_reg_dst_src2(UOP_PMULLW, ir, dst_reg, src_reg_a, src_reg_b)

#define uop_PSLLW_IMM(ir, dst_reg, src_reg, imm)       uop_gen_reg_dst_src_imm(UOP_PSLLW_IMM, ir, dst_reg, src_reg, imm)
#define uop_PSLLD_IMM(ir, dst_reg, src_reg, imm)       uop_gen_reg_dst_src_imm(UOP_PSLLD_IMM, ir, dst_reg, src_reg, imm)
#define uop_PSLLQ_IMM(ir, dst_reg, src_reg, imm)       uop_gen_reg_dst_src_imm(UOP_PSLLQ_IMM, ir, dst_reg, src_reg, imm)
#define uop_PSRAW_IMM(ir, dst_reg, src_reg, imm)       uop_gen_reg_dst_src_imm(UOP_PSRAW_IMM, ir, dst_reg, src_reg, imm)
#define uop_PSRAD_IMM(ir, dst_reg, src_reg, imm)       uop_gen_reg_dst_src_imm(UOP_PSRAD_IMM, ir, dst_reg, src_reg, imm)
#define uop_PSRAQ_IMM(ir, dst_reg, src_reg, imm)       uop_gen_reg_dst_src_imm(UOP_PSRAQ_IMM, ir, dst_reg, src_reg, imm)
#define uop_PSRLW_IMM(ir, dst_reg, src_reg, imm)       uop_gen_reg_dst_src_imm(UOP_PSRLW_IMM, ir, dst_reg, src_reg, imm)
#define uop_PSRLD_IMM(ir, dst_reg, src_reg, imm)       uop_gen_reg_dst_src_imm(UOP_PSRLD_IMM, ir, dst_reg, src_reg, imm)
#define uop_PSRLQ_IMM(ir, dst_reg, src_reg, imm)       uop_gen_reg_dst_src_imm(UOP_PSRLQ_IMM, ir, dst_reg, src_reg, imm)

#define uop_PSUBB(ir, dst_reg, src_reg_a, src_reg_b)   uop_gen_reg_dst_src2(UOP_PSUBB, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PSUBW(ir, dst_reg, src_reg_a, src_reg_b)   uop_gen_reg_dst_src2(UOP_PSUBW, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PSUBD(ir, dst_reg, src_reg_a, src_reg_b)   uop_gen_reg_dst_src2(UOP_PSUBD, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PSUBSB(ir, dst_reg, src_reg_a, src_reg_b)  uop_gen_reg_dst_src2(UOP_PSUBSB, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PSUBSW(ir, dst_reg, src_reg_a, src_reg_b)  uop_gen_reg_dst_src2(UOP_PSUBSW, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PSUBUSB(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_PSUBUSB, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PSUBUSW(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_PSUBUSW, ir, dst_reg, src_reg_a, src_reg_b)

#define uop_PUNPCKHBW(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_PUNPCKHBW, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PUNPCKHWD(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_PUNPCKHWD, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PUNPCKHDQ(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_PUNPCKHDQ, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PUNPCKLBW(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_PUNPCKLBW, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PUNPCKLWD(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_PUNPCKLWD, ir, dst_reg, src_reg_a, src_reg_b)
#define uop_PUNPCKLDQ(ir, dst_reg, src_reg_a, src_reg_b) uop_gen_reg_dst_src2(UOP_PUNPCKLDQ, ir, dst_reg, src_reg_a, src_reg_b)

#define uop_STORE_PTR_IMM(ir, p, imm)    uop_gen_pointer_imm(UOP_STORE_P_IMM, ir, p, imm)
#define uop_STORE_PTR_IMM_8(ir, p, imm)  uop_gen_pointer_imm(UOP_STORE_P_IMM_8, ir, p, imm)

#define uop_TEST_JNS_DEST(ir, src_reg) uop_gen_reg_src1(UOP_TEST_JNS_DEST, ir, src_reg)
#define uop_TEST_JS_DEST(ir, src_reg)  uop_gen_reg_src1(UOP_TEST_JS_DEST, ir, src_reg)

#ifdef DEBUG_EXTRA
#define uop_LOG_INSTR(ir, imm) uop_gen_imm(UOP_LOG_INSTR, ir, imm)
#endif

void codegen_direct_read_8(codeblock_t *block, int host_reg, void *p);
void codegen_direct_read_16(codeblock_t *block, int host_reg, void *p);
void codegen_direct_read_32(codeblock_t *block, int host_reg, void *p);
void codegen_direct_read_64(codeblock_t *block, int host_reg, void *p);
void codegen_direct_read_pointer(codeblock_t *block, int host_reg, void *p);
void codegen_direct_read_double(codeblock_t *block, int host_reg, void *p);
void codegen_direct_read_st_8(codeblock_t *block, int host_reg, void *base, int reg_idx);
void codegen_direct_read_st_64(codeblock_t *block, int host_reg, void *base, int reg_idx);
void codegen_direct_read_st_double(codeblock_t *block, int host_reg, void *base, int reg_idx);

void codegen_direct_write_8(codeblock_t *block, void *p, int host_reg);
void codegen_direct_write_16(codeblock_t *block, void *p, int host_reg);
void codegen_direct_write_32(codeblock_t *block, void *p, int host_reg);
void codegen_direct_write_64(codeblock_t *block, void *p, int host_reg);
void codegen_direct_write_pointer(codeblock_t *block, void *p, int host_reg);
void codegen_direct_write_ptr(codeblock_t *block, void *p, int host_reg);
void codegen_direct_write_double(codeblock_t *block, void *p, int host_reg);
void codegen_direct_write_st_8(codeblock_t *block, void *base, int reg_idx, int host_reg);
void codegen_direct_write_st_64(codeblock_t *block, void *base, int reg_idx, int host_reg);
void codegen_direct_write_st_double(codeblock_t *block, void *base, int reg_idx, int host_reg);

void codegen_direct_read_16_stack(codeblock_t *block, int host_reg, int stack_offset);
void codegen_direct_read_32_stack(codeblock_t *block, int host_reg, int stack_offset);
void codegen_direct_read_64_stack(codeblock_t *block, int host_reg, int stack_offset);
void codegen_direct_read_pointer_stack(codeblock_t *block, int host_reg, int stack_offset);
void codegen_direct_read_double_stack(codeblock_t *block, int host_reg, int stack_offset);

void codegen_direct_write_32_stack(codeblock_t *block, int stack_offset, int host_reg);
void codegen_direct_write_64_stack(codeblock_t *block, int stack_offset, int host_reg);
void codegen_direct_write_pointer_stack(codeblock_t *block, int stack_offset, int host_reg);
void codegen_direct_write_double_stack(codeblock_t *block, int stack_offset, int host_reg);

void codegen_set_jump_dest(codeblock_t *block, void *p);

#endif
