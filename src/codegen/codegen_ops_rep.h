uint32_t ropREP_MOVSB(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropREP_MOVSW(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropREP_MOVSL(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropREP_STOSB(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropREP_STOSW(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropREP_STOSL(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);

/*The elements of REP MOVS/STOS from one the compiled routine does not copy
  itself up to the next one it does, indexed by [STOS][log2 element size][32-bit addressing]*/
extern int (*const codegen_rep_run[2][3][2])(x86seg *seg, int32_t cycles_end);
