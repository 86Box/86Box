uint32_t x87_pc_off,x87_op_off;
uint16_t x87_pc_seg,x87_op_seg;
extern int TOP;
extern uint16_t npxs, npxc;
extern uint8_t tag[8];
extern int ismmx;
extern double ST[8];
extern uint64_t ST_i64[8];

typedef union MMX_REG
{
        uint64_t q;
        int64_t  sq;
        uint32_t l[2];
        int32_t  sl[2];
        uint16_t w[5];
        int16_t  sw[4];
        uint8_t  b[8];
        int8_t   sb[8];
} MMX_REG;

extern MMX_REG MM[8];

static inline void x87_set_mmx();
static inline void x87_emms();

uint16_t x87_gettag();
void x87_settag(uint16_t new_tag);

/*Hack for FPU copy. If set then ST_i64 contains the 64-bit integer loaded by FILD*/
#define TAG_UINT64 (1 << 2)
