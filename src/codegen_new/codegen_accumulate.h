enum
{
        ACCREG_cycles = 0,

        ACCREG_COUNT
};

struct ir_data_t;

void codegen_accumulate(struct ir_data_t *ir, int acc_reg, int delta);
void codegen_accumulate_flush(struct ir_data_t *ir);
void codegen_accumulate_reset();
