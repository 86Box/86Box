enum {
    ACCREG_cycles = 0,

    ACCREG_COUNT
};

struct ir_data_t;

void codegen_accumulate(struct ir_data_t *ir, int acc_reg, int delta);
/*The count not yet flushed, for an exit that charges it on its own path
  and leaves the accumulator to the path that goes on*/
int  codegen_accumulate_pending(int acc_reg);
void codegen_accumulate_flush(struct ir_data_t *ir);
void codegen_accumulate_reset(void);

enum {
    ACC_START,
    ACC_PREFIX,
    ACC_OPCODE,
    ACC_TIMING,
    ACC_DELTA,
    ACC_JUMP,
    ACC_FLUSH,
    ACC_EXIT,
    ACC_ROUTE,
    ACC_END
};

int  codegen_accumulate_record(int type, int pos, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
void codegen_accumulate_begin(void);
void codegen_accumulate_timing(struct ir_data_t *ir);
void codegen_accumulate_jump(struct ir_data_t *ir, int scale, int fallback);
int  codegen_accumulate_unroll(struct ir_data_t *ir, int first, int start, int branch);
void codegen_accumulate_copy(struct ir_data_t *ir, int pos);
