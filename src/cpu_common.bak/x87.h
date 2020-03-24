#define C0 (1<<8)
#define C1 (1<<9)
#define C2 (1<<10)
#define C3 (1<<14)

uint32_t x87_pc_off,x87_op_off;
uint16_t x87_pc_seg,x87_op_seg;

static inline void x87_set_mmx()
{
        cpu_state.TOP = 0;
        *(uint64_t *)cpu_state.tag = 0x0101010101010101ull;
        cpu_state.ismmx = 1;
}

static inline void x87_emms()
{
        *(uint64_t *)cpu_state.tag = 0;
        cpu_state.ismmx = 0;
}

uint16_t x87_gettag();
void x87_settag(uint16_t new_tag);
void x87_dumpregs();
void x87_reset();

#define TAG_EMPTY  0
#define TAG_VALID  (1 << 0)
/*Hack for FPU copy. If set then MM[].q contains the 64-bit integer loaded by FILD*/
#define TAG_UINT64 (1 << 7)

#define X87_ROUNDING_NEAREST 0
#define X87_ROUNDING_DOWN    1
#define X87_ROUNDING_UP      2
#define X87_ROUNDING_CHOP    3

void codegen_set_rounding_mode(int mode);
