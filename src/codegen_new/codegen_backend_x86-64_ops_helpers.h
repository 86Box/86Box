#define JMP_LEN_BYTES 5

static inline void
codegen_addbyte(UNUSED(codeblock_t *block), uint8_t val)
{
    if (block_pos >= BLOCK_MAX) {
        fatal("codegen_addbyte over! %i\n", block_pos);
#if 0
        CPU_BLOCK_END();
#endif
    }
    block_write_data[block_pos++] = val;
}
static inline void
codegen_addbyte2(UNUSED(codeblock_t *block), uint8_t vala, uint8_t valb)
{
    if (block_pos > (BLOCK_MAX - 2)) {
        fatal("codegen_addbyte2 over! %i\n", block_pos);
        CPU_BLOCK_END();
    }
    block_write_data[block_pos++] = vala;
    block_write_data[block_pos++] = valb;
}
static inline void
codegen_addbyte3(UNUSED(codeblock_t *block), uint8_t vala, uint8_t valb, uint8_t valc)
{
    if (block_pos > (BLOCK_MAX - 3)) {
        fatal("codegen_addbyte3 over! %i\n", block_pos);
        CPU_BLOCK_END();
    }
    block_write_data[block_pos++] = vala;
    block_write_data[block_pos++] = valb;
    block_write_data[block_pos++] = valc;
}
static inline void
codegen_addbyte4(UNUSED(codeblock_t *block), uint8_t vala, uint8_t valb, uint8_t valc, uint8_t vald)
{
    if (block_pos > (BLOCK_MAX - 4)) {
        fatal("codegen_addbyte4 over! %i\n", block_pos);
        CPU_BLOCK_END();
    }
    block_write_data[block_pos++] = vala;
    block_write_data[block_pos++] = valb;
    block_write_data[block_pos++] = valc;
    block_write_data[block_pos++] = vald;
}

static inline void
codegen_addword(UNUSED(codeblock_t *block), uint16_t val)
{
    if (block_pos > (BLOCK_MAX - 2)) {
        fatal("codegen_addword over! %i\n", block_pos);
        CPU_BLOCK_END();
    }
    *(uint16_t *) &block_write_data[block_pos] = val;
    block_pos += 2;
}

static inline void
codegen_addlong(UNUSED(codeblock_t *block), uint32_t val)
{
    if (block_pos > (BLOCK_MAX - 4)) {
        fatal("codegen_addlong over! %i\n", block_pos);
        CPU_BLOCK_END();
    }
    *(uint32_t *) &block_write_data[block_pos] = val;
    block_pos += 4;
}

static inline void
codegen_addquad(UNUSED(codeblock_t *block), uint64_t val)
{
    if (block_pos > (BLOCK_MAX - 8)) {
        fatal("codegen_addquad over! %i\n", block_pos);
        CPU_BLOCK_END();
    }
    *(uint64_t *) &block_write_data[block_pos] = val;
    block_pos += 8;
}

static inline void
codegen_alloc_bytes(codeblock_t *block, int size)
{
    if (block_pos > ((BLOCK_MAX - size) - JMP_LEN_BYTES)) {
        /*Current block is full. Allocate a new block*/
        struct mem_block_t *new_block = codegen_allocator_allocate(block->head_mem_block, get_block_nr(block));
        uint8_t            *new_ptr   = codeblock_allocator_get_ptr(new_block);

        /*Add a jump instruction to the new block*/
        codegen_addbyte(block, 0xe9); /*JMP*/
        codegen_addlong(block, (uintptr_t) new_ptr - (uintptr_t) &block_write_data[block_pos + 4]);

        /*Set write address to start of new block*/
        block_pos        = 0;
        block_write_data = new_ptr;
    }
}

static inline int
is_imm8(uint32_t imm_data)
{
    if (imm_data <= 0x7f || imm_data >= 0xffffff80)
        return 1;
    return 0;
}

/*Instruction encoding. Every emitter below goes through these, so any
  general purpose register, R8-R15 included, can be passed to any of them.
  Byte registers 4-7 are AH-BH; those have no encoding in an instruction
  that carries a REX prefix, so the allocator never places a register that
  is accessed by byte in R8-R15.*/
#    define DISP_AUTO 0 /*shortest displacement that encodes*/
#    define DISP_8    1 /*at least disp8*/
#    define DISP_32   2
#    define NO_BASE   -1
#    define NO_INDEX  -1

static inline uint8_t
rex_prefix(int w, int reg, int index, int base)
{
    uint8_t rex = 0;

    if (w)
        rex |= 0x48;
    if (reg & 8)
        rex |= 0x44;
    if (index & 8)
        rex |= 0x42;
    if (base & 8)
        rex |= 0x41;
    return rex;
}

static inline void
check_byte_reg(uint8_t rex, int reg)
{
    if (rex && (reg >= 4) && (reg < 8))
        fatal("x86-64 codegen: byte register %i used with a REX prefix\n", reg);
}

/*Prefix, REX and opcode (one to three bytes, most significant first), with
  room reserved for tail_len more bytes so the instruction never straddles
  two code blocks.*/
static inline void
emit_head(codeblock_t *block, uint8_t prefix, uint8_t rex, int op_len, uint32_t op, int tail_len)
{
    codegen_alloc_bytes(block, (prefix ? 1 : 0) + (rex ? 1 : 0) + op_len + tail_len);
    if (prefix)
        codegen_addbyte(block, prefix);
    if (rex)
        codegen_addbyte(block, rex);
    if (op_len > 2)
        codegen_addbyte(block, op >> 16);
    if (op_len > 1)
        codegen_addbyte(block, op >> 8);
    codegen_addbyte(block, op);
}

static inline void
emit_imm(codeblock_t *block, int imm_len, uint32_t imm)
{
    if (imm_len == 1)
        codegen_addbyte(block, imm);
    else if (imm_len == 2)
        codegen_addword(block, imm);
    else if (imm_len == 4)
        codegen_addlong(block, imm);
}

/*Register to register: ModRM.reg = reg, ModRM.rm = rm.*/
static inline void
emit_rr(codeblock_t *block, uint8_t prefix, int w, int is_byte, int op_len, uint32_t op, int reg, int rm)
{
    uint8_t rex = rex_prefix(w, reg, 0, rm);

    if (is_byte) {
        check_byte_reg(rex, reg);
        check_byte_reg(rex, rm);
    }
    emit_head(block, prefix, rex, op_len, op, 1);
    codegen_addbyte(block, 0xc0 | ((reg & 7) << 3) | (rm & 7));
}

/*Register operand with an opcode extension in ModRM.reg (ext is already
  shifted, as the RM_OP_ values are), and an optional immediate.*/
static inline void
emit_ext(codeblock_t *block, uint8_t prefix, int w, int is_byte, int op_len, uint32_t op, int ext, int rm, int imm_len, uint32_t imm)
{
    uint8_t rex = rex_prefix(w, 0, 0, rm);

    if (is_byte)
        check_byte_reg(rex, rm);
    emit_head(block, prefix, rex, op_len, op, 1 + imm_len);
    codegen_addbyte(block, 0xc0 | ext | (rm & 7));
    emit_imm(block, imm_len, imm);
}

/*Memory operand [base + index << shift + disp]. base NO_BASE is an absolute
  32-bit address. reg is ModRM.reg, a register or an opcode extension.*/
static inline void
emit_mem(codeblock_t *block, uint8_t prefix, int w, int is_byte, int op_len, uint32_t op, int reg,
         int base, int index, int shift, int32_t disp, int disp_mode, int imm_len, uint32_t imm)
{
    uint8_t rex     = rex_prefix(w, reg, (index == NO_INDEX) ? 0 : index, (base == NO_BASE) ? 0 : base);
    int     use_sib = (index != NO_INDEX) || (base == NO_BASE) || ((base & 7) == 4);
    int     mod;
    int     disp_len;

    if (is_byte)
        check_byte_reg(rex, reg);
    if ((index != NO_INDEX) && ((index & 15) == 4))
        fatal("x86-64 codegen: RSP can not be an index register\n");

    if (base == NO_BASE) {
        mod      = 0;
        disp_len = 4;
    } else if ((disp_mode == DISP_32) || (disp < -128) || (disp > 127)) {
        mod      = 2;
        disp_len = 4;
    } else if ((disp_mode == DISP_8) || disp || ((base & 7) == 5)) {
        mod      = 1;
        disp_len = 1;
    } else {
        mod      = 0;
        disp_len = 0;
    }

    emit_head(block, prefix, rex, op_len, op, 1 + use_sib + disp_len + imm_len);
    codegen_addbyte(block, (mod << 6) | ((reg & 7) << 3) | (use_sib ? 4 : (base & 7)));
    if (use_sib)
        codegen_addbyte(block, (shift << 6) | (((index == NO_INDEX) ? 4 : (index & 7)) << 3) | ((base == NO_BASE) ? 5 : (base & 7)));
    if (disp_len == 1)
        codegen_addbyte(block, disp);
    else if (disp_len == 4)
        codegen_addlong(block, disp);
    emit_imm(block, imm_len, imm);
}

/* A no-base disp32 normally sign-extends in 64-bit address mode. Use the
   address-size override for pointers in the upper half of the low 4 GiB. */
static inline void
emit_abs32(codeblock_t *block, uint8_t prefix, int w, int is_byte, int op_len, uint32_t op,
           int reg, uint32_t addr, int imm_len, uint32_t imm)
{
    if (addr > INT32_MAX) {
        /* Reserve the whole instruction before writing its extra prefix. */
        codegen_alloc_bytes(block, 16);
        codegen_addbyte(block, 0x67);
    }
    emit_mem(block, prefix, w, is_byte, op_len, op, reg,
             NO_BASE, NO_INDEX, 0, (int32_t) addr, DISP_32, imm_len, imm);
}

/*Opcode with the register in its low three bits (MOV reg, imm).*/
static inline void
emit_opreg(codeblock_t *block, uint8_t prefix, int w, int is_byte, uint8_t op, int reg, int imm_len, uint32_t imm)
{
    uint8_t rex = rex_prefix(w, 0, 0, reg);

    if (is_byte)
        check_byte_reg(rex, reg);
    emit_head(block, prefix, rex, 1, op | (reg & 7), imm_len);
    emit_imm(block, imm_len, imm);
}
