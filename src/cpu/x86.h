uint16_t oldcs;
extern uint32_t rmdat32;
int oldcpl;

extern int nmi_enable;

int tempc;
int output;
int firstrepcycle;

uint32_t easeg,ealimit,ealimitw;

int skipnextprint;
int inhlt;

uint8_t opcode;
int noint;

uint16_t lastcs,lastpc;
extern int timetolive,keyboardtimer;

#define setznp168 setznp16

#define getr8(r)   ((r&4)?cpu_state.regs[r&3].b.h:cpu_state.regs[r&3].b.l)
#define getr16(r)  cpu_state.regs[r].w
#define getr32(r)  cpu_state.regs[r].l

#define setr8(r,v) if (r&4) cpu_state.regs[r&3].b.h=v; \
                   else     cpu_state.regs[r&3].b.l=v;
#define setr16(r,v) cpu_state.regs[r].w=v
#define setr32(r,v) cpu_state.regs[r].l=v

uint8_t znptable8[256];
uint16_t znptable16[65536];

int use32;
int stack32;

#define fetchea()   { rmdat=readmemb(cs+pc); pc++;  \
                    reg=(rmdat>>3)&7;               \
                    mod=(rmdat>>6)&3;               \
                    rm=rmdat&7;                   \
                    if (mod!=3) fetcheal(); }


int optype;
#define JMP 1
#define CALL 2
#define IRET 3
#define OPTYPE_INT 4

uint32_t oxpc;

extern uint16_t *mod1add[2][8];
extern uint32_t *mod1seg[8];


#define IRQTEST ((flags&I_FLAG) && (pic.pend&~pic.mask) && !noint)

extern int cgate32;


extern uint32_t *eal_r, *eal_w;


extern uint32_t flags_zn;
extern uint8_t flags_p;
#define FLAG_N (flags_zn>>31)
#define FLAG_Z (flags_zn)
#define FLAG_P (znptable8[flags_p]&P_FLAG)

extern int gpf;


enum
{
        ABRT_NONE = 0,
        ABRT_GEN,
        ABRT_TS  = 0xA,
        ABRT_NP  = 0xB,
        ABRT_SS  = 0xC,
        ABRT_GPF = 0xD,
        ABRT_PF  = 0xE
};

extern uint32_t abrt_error;

void x86_doabrt(int x86_abrt);

extern uint8_t opcode2;

extern uint16_t rds;
extern uint32_t rmdat32;

extern int inscounts[256];

void x86illegal();

void x86seg_reset();
void x86gpf(char *s, uint16_t error);

extern uint16_t zero;

extern int x86_was_reset;

extern int codegen_flat_ds;
extern int codegen_flat_ss;
