#define BLOCK_SIZE        0x20000
#define BLOCK_MASK        0x1ffff
#define BLOCK_START       0

#define HASH_SIZE         0x20000
#define HASH_MASK         0x1ffff

#define HASH(l)           ((l) &0x1ffff)

#define BLOCK_EXIT_OFFSET 0x7e0
#ifdef OLD_GPF
#    define BLOCK_GPF_OFFSET (BLOCK_EXIT_OFFSET - 20)
/*The old stub calls x86gpf itself: a limit fault through SS is #GP there too*/
#    define BLOCK_SS_OFFSET BLOCK_GPF_OFFSET
#else
#    define BLOCK_GPF_OFFSET (BLOCK_EXIT_OFFSET - 12)
/*#SS(0): its abort, then the #GP stub's error code and exit*/
#    define BLOCK_SS_OFFSET (BLOCK_GPF_OFFSET - 6)
#endif

#define BLOCK_MAX 1608

enum {
    OP_RET = 0xc3
};

#define NR_HOST_REGS 4
extern int host_reg_mapping[NR_HOST_REGS];
#define NR_HOST_XMM_REGS 8
extern int host_reg_xmm_mapping[NR_HOST_XMM_REGS];
