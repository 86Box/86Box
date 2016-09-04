#define BLOCK_SIZE 0x4000
#define BLOCK_MASK 0x3fff
#define BLOCK_START 0

#define HASH_SIZE 0x20000
#define HASH_MASK 0x1ffff

#define HASH(l) ((l) & 0x1ffff)

#define BLOCK_EXIT_OFFSET 0x7f0
#define BLOCK_GPF_OFFSET (BLOCK_EXIT_OFFSET - 20)

enum
{
        OP_RET = 0xc3
};

#define NR_HOST_REGS 3
extern int host_reg_mapping[NR_HOST_REGS];
#define NR_HOST_XMM_REGS 7
extern int host_reg_xmm_mapping[NR_HOST_XMM_REGS];
