#include "codegen_ir_defs.h"

ir_data_t *codegen_ir_init(void);

void codegen_ir_set_unroll(int count, int start, int first_instruction);
void codegen_ir_note_smc_check(int uop);
int  codegen_ir_unrolling(void);
void codegen_ir_compile(ir_data_t *ir, codeblock_t *block);
#ifdef CODEGEN_BACKEND_HAS_COLD_REGION
/*Uop c is the FERR# test's, left out of the unrolling's counts*/
void codegen_ir_set_uncounted(int c);
/*How many of the uops from start on are*/
int codegen_ir_uncounted_since(ir_data_t *ir, int start);
#endif

/*The uop codegen_ir_compile() compiles, and whether a jump goes to it*/
extern int codegen_ir_uop_nr;
extern int codegen_ir_uop_is_dest;
/*The backend's REG_TEMP holds an address it may reuse (codegen_ir_compile()
  clears this before any uop that may change it)*/
extern int codegen_temp_ptr_valid;
