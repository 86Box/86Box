#define BLOCK_SIZE        0x4000
#define BLOCK_MASK        0x3fff
#define BLOCK_START       0

#define HASH_SIZE         0x20000
#define HASH_MASK         0x1ffff

#define HASH(l)           ((l) &0x1ffff)

#define BLOCK_EXIT_OFFSET 0x7f0
#ifdef OLD_GPF
#    define BLOCK_GPF_OFFSET (BLOCK_EXIT_OFFSET - 20)
#else
#    define BLOCK_GPF_OFFSET (BLOCK_EXIT_OFFSET - 14)
#endif

#define BLOCK_MAX 1720

enum {
    OP_RET = 0xc3
};

#define NR_HOST_REGS 4
extern int host_reg_mapping[NR_HOST_REGS];
#define NR_HOST_XMM_REGS 8
extern int host_reg_xmm_mapping[NR_HOST_XMM_REGS];

extern uint32_t mem_load_addr_ea_b;
extern uint32_t mem_load_addr_ea_w;
extern uint32_t mem_load_addr_ea_l;
extern uint32_t mem_load_addr_ea_q;
extern uint32_t mem_store_addr_ea_b;
extern uint32_t mem_store_addr_ea_w;
extern uint32_t mem_store_addr_ea_l;
extern uint32_t mem_store_addr_ea_q;

extern uint32_t mem_load_addr_ea_b_no_abrt;
extern uint32_t mem_store_addr_ea_b_no_abrt;
extern uint32_t mem_load_addr_ea_w_no_abrt;
extern uint32_t mem_store_addr_ea_w_no_abrt;
extern uint32_t mem_load_addr_ea_l_no_abrt;
extern uint32_t mem_store_addr_ea_l_no_abrt;
extern uint32_t mem_check_write;
extern uint32_t mem_check_write_w;
extern uint32_t mem_check_write_l;
