#include "codegen_ir_defs.h"

ir_data_t *codegen_ir_init(void);

void codegen_ir_set_unroll(int count, int start, int first_instruction);
int  codegen_ir_unrolling(void);
void codegen_ir_compile(ir_data_t *ir, codeblock_t *block);
#ifdef CODEGEN_BACKEND_HAS_COLD_REGION
/*Uop c is the FERR# test's, left out of the unrolling's counts*/
void codegen_ir_set_uncounted(int c);
/*How many of the uops from start on are*/
int codegen_ir_uncounted_since(ir_data_t *ir, int start);
#endif
