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
    ACC_BLOCK_END,
    ACC_BLOCK_START,
    ACC_END
};

int  codegen_accumulate_record(int type, int pos, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
void codegen_accumulate_begin(void);
void codegen_accumulate_timing(struct ir_data_t *ir);
void codegen_accumulate_jump(struct ir_data_t *ir, int scale, int fallback);
/*The timing model's block end and a new block's start inside the IR block
  (a trace), recorded so an unrolled loop's copies are timed the same*/
void codegen_accumulate_block_end(void);
void codegen_accumulate_block_start(void);
int  codegen_accumulate_unroll(struct ir_data_t *ir, int first, int start, int branch);
void codegen_accumulate_copy(struct ir_data_t *ir, int pos);
/*Notes the timing model's state before an opcode that may be probed is
  timed*/
void codegen_accumulate_probe_mark(void);
/*The cycles the timing model charges for the last instruction timed, with
  its opcode replaced by opcode, and a block end right after it, plus an
  amount the same for every opcode of that instruction: only differences
  mean anything. From the state codegen_accumulate_probe_mark() noted just
  before its opcode, else by timing the block again from its start. Changes
  the model's state: once done probing, codegen_accumulate_probe_done() puts
  back the state the block's own instructions left*/
int  codegen_accumulate_probe(int opcode);
void codegen_accumulate_probe_done(void);
