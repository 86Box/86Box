uint32_t x87_pc_off,x87_op_off;
uint16_t x87_pc_seg,x87_op_seg;

static __inline void x87_set_mmx()
{
	uint64_t *p;
        cpu_state.TOP = 0;
	p = (uint64_t *)cpu_state.tag;
        *p = 0;
        cpu_state.ismmx = 1;
}

static __inline void x87_emms()
{
	uint64_t *p;
	p = (uint64_t *)cpu_state.tag;
        *p = 0;
        cpu_state.ismmx = 0;
}


uint16_t x87_gettag();
void x87_settag(uint16_t new_tag);

/*Hack for FPU copy. If set then MM[].q contains the 64-bit integer loaded by FILD*/
#define TAG_UINT64 (1 << 2)
