enum
{
        ACCREG_cycles = 0,

        ACCREG_COUNT
};

struct ir_data_t;

void codegen_accumulate(int acc_reg, int delta);
void codegen_accumulate_flush(void);
void codegen_accumulate_reset();
