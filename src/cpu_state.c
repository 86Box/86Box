#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef union
{
        uint32_t l;
        uint16_t w;
        struct
        {
                uint8_t l,h;
        } b;
} x86reg;

typedef struct
{
        uint32_t base;
        uint32_t limit;
        uint8_t access;
        uint16_t seg;
        uint32_t limit_low, limit_high;
        int checked; /*Non-zero if selector is known to be valid*/
} x86seg;

typedef union MMX_REG
{
        uint64_t q;
        int64_t  sq;
        uint32_t l[2];
        int32_t  sl[2];
        uint16_t w[4];
        int16_t  sw[4];
        uint8_t  b[8];
        int8_t   sb[8];
} MMX_REG;

struct _cpustate_ {
    x86reg	regs[8];

    uint8_t	tag[8];

    x86seg	*ea_seg;
    uint32_t	eaaddr;

    int		flags_op;
    uint32_t	flags_res;
    uint32_t	flags_op1,
		flags_op2;

    uint32_t	pc;
    uint32_t	oldpc;
    uint32_t	op32;  

    int		TOP;

    union {
	struct {
	    int8_t	rm,
			mod,
			reg;
	} rm_mod_reg;
	int32_t		rm_mod_reg_data;
    }		rm_data;

    int8_t	ssegs;
    int8_t	ismmx;
    int8_t	abrt;

    int		_cycles;
    int		cpu_recomp_ins;

    uint16_t	npxs,
		npxc;

    double	ST[8];

    uint16_t	MM_w4[8];

    MMX_REG	MM[8];

    uint16_t	old_npxc,
		new_npxc;
    uint32_t	last_ea;
} cpu_state;


int main(int argc, char *argv[])
{
	printf("sizeof(cpu_state) = %i\n", sizeof(cpu_state));

	return 0;
}
