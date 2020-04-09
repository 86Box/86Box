#define C0 (1<<8)
#define C1 (1<<9)
#define C2 (1<<10)
#define C3 (1<<14)

uint32_t x87_pc_off,x87_op_off;
uint16_t x87_pc_seg,x87_op_seg;

static inline void x87_set_mmx()
{
#ifdef USE_NEW_DYNAREC
        cpu_state.TOP = 0;
        *(uint64_t *)cpu_state.tag = 0x0101010101010101ull;
        cpu_state.ismmx = 1;
#else
	uint64_t *p;
        cpu_state.TOP = 0;
	p = (uint64_t *)cpu_state.tag;
        *p = 0;
        cpu_state.ismmx = 1;
#endif
}

static inline void x87_emms()
{
#ifdef USE_NEW_DYNAREC
        *(uint64_t *)cpu_state.tag = 0;
        cpu_state.ismmx = 0;
#else
	uint64_t *p;
	p = (uint64_t *)cpu_state.tag;
        *p = 0;
        cpu_state.ismmx = 0;
#endif
}


uint16_t x87_gettag();
void x87_settag(uint16_t new_tag);

#ifdef USE_NEW_DYNAREC
#define TAG_EMPTY  0
#define TAG_VALID  (1 << 0)
/*Hack for FPU copy. If set then MM[].q contains the 64-bit integer loaded by FILD*/
#define TAG_UINT64 (1 << 7)

#define X87_ROUNDING_NEAREST 0
#define X87_ROUNDING_DOWN    1
#define X87_ROUNDING_UP      2
#define X87_ROUNDING_CHOP    3

void codegen_set_rounding_mode(int mode);
#else
#define TAG_EMPTY  3
#define TAG_VALID  (1 << 0)
/*Hack for FPU copy. If set then MM[].q contains the 64-bit integer loaded by FILD*/
#define TAG_UINT64 (1 << 2)
#endif
