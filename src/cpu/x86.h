#define ABRT_MASK 0x7f
/*An 'expected' exception is one that would be expected to occur on every execution
  of this code path; eg a GPF due to being in v86 mode. An 'unexpected' exception is
  one that would be unlikely to occur on the next exception, eg a page fault may be
  fixed up by the exception handler and the next execution would not hit it.

  This distinction is used by the dynarec; a block that hits an 'expected' exception
  would be compiled, a block that hits an 'unexpected' exception would be rejected so
  that we don't end up with an unnecessarily short block*/
#define ABRT_EXPECTED  0x80

extern uint8_t	opcode, opcode2;
extern uint8_t	flags_p;
extern uint8_t	znptable8[256];

extern uint16_t	zero, oldcs;
extern uint16_t	lastcs, lastpc;
extern uint16_t *mod1add[2][8];
extern uint16_t znptable16[65536];

extern int	x86_was_reset, trap;
extern int	codegen_flat_ss, codegen_flat_ds;
extern int	timetolive, keyboardtimer, trap;
extern int	optype, stack32;
extern int	oldcpl, cgate32, cpl_override;
extern int	nmi_enable;
extern int	oddeven, inttype;

extern uint32_t use32;
extern uint32_t rmdat, easeg;
extern uint32_t	oxpc, flags_zn;
extern uint32_t abrt_error;
extern uint32_t	backupregs[16];
extern uint32_t *mod1seg[8];
extern uint32_t *eal_r, *eal_w;

#define fetchdat rmdat

#define setznp168 setznp16

#define getr8(r)   ((r&4)?cpu_state.regs[r&3].b.h:cpu_state.regs[r&3].b.l)
#define getr16(r)  cpu_state.regs[r].w
#define getr32(r)  cpu_state.regs[r].l

#define setr8(r,v) if (r&4) cpu_state.regs[r&3].b.h=v; \
                   else     cpu_state.regs[r&3].b.l=v;
#define setr16(r,v) cpu_state.regs[r].w=v
#define setr32(r,v) cpu_state.regs[r].l=v

#define fetchea()	{					\
				rmdat = readmemb(cs + pc);	\
				pc++;				\
				reg = (rmdat >> 3) & 7;		\
				mod = (rmdat >> 6) & 3;		\
				rm = rmdat & 7;			\
				if (mod!=3)			\
					fetcheal();		\
			}

#define JMP 1
#define CALL 2
#define IRET 3
#define OPTYPE_INT 4


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


extern void	x86_doabrt(int x86_abrt);
extern void	x86illegal();
extern void	x86seg_reset();
extern void	x86gpf(char *s, uint16_t error);
extern void	x86gpf_expected(char *s, uint16_t error);
