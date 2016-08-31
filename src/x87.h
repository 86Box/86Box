uint32_t x87_pc_off,x87_op_off;
uint16_t x87_pc_seg,x87_op_seg;

static inline void x87_set_mmx();
static inline void x87_emms();

uint16_t x87_gettag();
void x87_settag(uint16_t new_tag);

/*Hack for FPU copy. If set then MM[].q contains the 64-bit integer loaded by FILD*/
#define TAG_UINT64 (1 << 2)
