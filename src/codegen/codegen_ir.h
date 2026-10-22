#include "codegen_ir_defs.h"

ir_data_t *codegen_ir_init(void);

void codegen_ir_set_unroll(int count, int start, int first_instruction);
int  codegen_ir_unrolling(void);
void codegen_ir_compile(ir_data_t *ir, codeblock_t *block);
