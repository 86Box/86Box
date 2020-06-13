#include "codegen_ir_defs.h"

ir_data_t *codegen_ir_init();

void codegen_ir_set_unroll(int count, int start, int first_instruction);
void codegen_ir_compile(ir_data_t *ir, codeblock_t *block);
