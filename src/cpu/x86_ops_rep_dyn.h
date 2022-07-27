#define REP_OPS(size, CNT_REG, SRC_REG, DEST_REG) \
static int opREP_INSB_ ## size(uint32_t fetchdat)                               \
{                                                                               \
        addr64 = 0x00000000;                                                    \
                                                                                \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                uint8_t temp;                                                   \
                                                                                \
		SEG_CHECK_WRITE(&cpu_state.seg_es);                             \
                check_io_perm(DX);                                              \
                CHECK_WRITE(&cpu_state.seg_es, DEST_REG, DEST_REG);             \
                high_page = 0;                                                  \
                do_mmut_wb(es, DEST_REG, &addr64);                              \
                if (cpu_state.abrt) return 1;                                   \
                temp = inb(DX);                                                 \
                writememb_n(es, DEST_REG, addr64, temp); if (cpu_state.abrt) return 1;    \
                                                                                \
                if (cpu_state.flags & D_FLAG) DEST_REG--;                       \
                else                DEST_REG++;                                 \
                CNT_REG--;                                                      \
                cycles -= 15;                                                   \
        }                                                                       \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_INSW_ ## size(uint32_t fetchdat)                               \
{                                                                               \
        addr64a[0] = addr64a[1] = 0x00000000;                                   \
                                                                                \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                uint16_t temp;                                                  \
                                                                                \
		SEG_CHECK_WRITE(&cpu_state.seg_es);                             \
                check_io_perm(DX);                                              \
                check_io_perm(DX+1);                                            \
                CHECK_WRITE(&cpu_state.seg_es, DEST_REG, DEST_REG + 1UL);       \
                high_page = 0;                                                  \
                do_mmut_ww(es, DEST_REG, addr64a);                              \
                if (cpu_state.abrt) return 1;                                   \
                temp = inw(DX);                                                 \
                writememw_n(es, DEST_REG, addr64a, temp); if (cpu_state.abrt) return 1;    \
                                                                                \
                if (cpu_state.flags & D_FLAG) DEST_REG -= 2;                    \
                else                DEST_REG += 2;                              \
                CNT_REG--;                                                      \
                cycles -= 15;                                                   \
        }                                                                       \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_INSL_ ## size(uint32_t fetchdat)                               \
{                                                                               \
        addr64a[0] = addr64a[1] = addr64a[2] = addr64a[3] = 0x00000000;         \
                                                                                \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                uint32_t temp;                                                  \
                                                                                \
		SEG_CHECK_WRITE(&cpu_state.seg_es);                             \
                check_io_perm(DX);                                              \
                check_io_perm(DX+1);                                            \
                check_io_perm(DX+2);                                            \
                check_io_perm(DX+3);                                            \
                CHECK_WRITE(&cpu_state.seg_es, DEST_REG, DEST_REG + 3UL);       \
                high_page = 0;                                                  \
                do_mmut_wl(es, DEST_REG, addr64a);                              \
                if (cpu_state.abrt) return 1;                                   \
                temp = inl(DX);                                                 \
                writememl_n(es, DEST_REG, addr64a, temp); if (cpu_state.abrt) return 1;    \
                                                                                \
                if (cpu_state.flags & D_FLAG) DEST_REG -= 4;                    \
                else                DEST_REG += 4;                              \
                CNT_REG--;                                                      \
                cycles -= 15;                                                   \
        }                                                                       \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
                                                                                \
static int opREP_OUTSB_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                uint8_t temp;                                                   \
                SEG_CHECK_READ(cpu_state.ea_seg);                               \
                CHECK_READ(cpu_state.ea_seg, SRC_REG, SRC_REG);                 \
                temp = readmemb(cpu_state.ea_seg->base, SRC_REG); if (cpu_state.abrt) return 1;    \
                check_io_perm(DX);                                               \
                outb(DX, temp);                                                 \
                if (cpu_state.flags & D_FLAG) SRC_REG--;                                  \
                else                SRC_REG++;                                  \
                CNT_REG--;                                                      \
                cycles -= 14;                                                   \
        }                                                                       \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_OUTSW_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                uint16_t temp;                                                  \
                SEG_CHECK_READ(cpu_state.ea_seg);                               \
                CHECK_READ(cpu_state.ea_seg, SRC_REG, SRC_REG + 1UL);           \
                temp = readmemw(cpu_state.ea_seg->base, SRC_REG); if (cpu_state.abrt) return 1;   \
                check_io_perm(DX);                                               \
                check_io_perm(DX+1);                                             \
                outw(DX, temp);                                                 \
                if (cpu_state.flags & D_FLAG) SRC_REG -= 2;                               \
                else                SRC_REG += 2;                               \
                CNT_REG--;                                                      \
                cycles -= 14;                                                   \
        }                                                                       \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_OUTSL_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                uint32_t temp;                                                  \
                SEG_CHECK_READ(cpu_state.ea_seg);                               \
                CHECK_READ(cpu_state.ea_seg, SRC_REG, SRC_REG + 3UL);           \
                temp = readmeml(cpu_state.ea_seg->base, SRC_REG); if (cpu_state.abrt) return 1;   \
                check_io_perm(DX);                                               \
                check_io_perm(DX+1);                                             \
                check_io_perm(DX+2);                                             \
                check_io_perm(DX+3);                                             \
                outl(DX, temp);                                                 \
                if (cpu_state.flags & D_FLAG) SRC_REG -= 4;                               \
                else                SRC_REG += 4;                               \
                CNT_REG--;                                                      \
                cycles -= 14;                                                   \
        }                                                                       \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
                                                                                \
static int opREP_MOVSB_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);    \
        addr64 = addr64_2 = 0x00000000;                                         \
        if (trap)                                                               \
                cycles_end = cycles+1; /*Force the instruction to end after only one iteration when trap flag set*/     \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                SEG_CHECK_READ(cpu_state.ea_seg);                               \
                SEG_CHECK_WRITE(&cpu_state.seg_es);                             \
        }  									\
        while (CNT_REG > 0)                                                     \
        {                                                                       \
                uint8_t temp;                                                   \
                                                                                \
                CHECK_READ_REP(cpu_state.ea_seg, SRC_REG, SRC_REG);             \
                high_page = 0;                                                  \
                do_mmut_rb(cpu_state.ea_seg->base, SRC_REG, &addr64) ;          \
                if (cpu_state.abrt) break;                                      \
                CHECK_WRITE_REP(&cpu_state.seg_es, DEST_REG, DEST_REG);         \
                do_mmut_wb(es, DEST_REG, &addr64_2);                            \
                if (cpu_state.abrt) break;                                      \
                temp = readmemb_n(cpu_state.ea_seg->base, SRC_REG, addr64); if (cpu_state.abrt) return 1;    \
                writememb_n(es, DEST_REG, addr64_2, temp); if (cpu_state.abrt) return 1;    \
                                                                                \
                if (cpu_state.flags & D_FLAG) { DEST_REG--; SRC_REG--; }        \
                else                { DEST_REG++; SRC_REG++; }                  \
                CNT_REG--;                                                      \
                cycles -= is486 ? 3 : 4;                                        \
                if (cycles < cycles_end)                                        \
                        break;                                                  \
        }                                                                       \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_MOVSW_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);    \
        addr64a[0] = addr64a[1] = 0x00000000;                                   \
        addr64a_2[0] = addr64a_2[1] = 0x00000000;                               \
        if (trap)                                                               \
                cycles_end = cycles+1; /*Force the instruction to end after only one iteration when trap flag set*/     \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                SEG_CHECK_READ(cpu_state.ea_seg);                               \
                SEG_CHECK_WRITE(&cpu_state.seg_es);                             \
        }  									\
        while (CNT_REG > 0)                                                     \
        {                                                                       \
                uint16_t temp;                                                  \
                                                                                \
                CHECK_READ_REP(cpu_state.ea_seg, SRC_REG, SRC_REG + 1UL);       \
                high_page = 0;                                                  \
                do_mmut_rw(cpu_state.ea_seg->base, SRC_REG, addr64a);           \
                if (cpu_state.abrt) break;                                      \
                CHECK_WRITE_REP(&cpu_state.seg_es, DEST_REG, DEST_REG + 1UL);   \
                do_mmut_ww(es, DEST_REG, addr64a_2);                             \
                if (cpu_state.abrt) break;                                      \
                temp = readmemw_n(cpu_state.ea_seg->base, SRC_REG, addr64a); if (cpu_state.abrt) return 1;    \
                writememw_n(es, DEST_REG, addr64a_2, temp); if (cpu_state.abrt) return 1;    \
                                                                                \
                if (cpu_state.flags & D_FLAG) { DEST_REG -= 2; SRC_REG -= 2; }  \
                else                { DEST_REG += 2; SRC_REG += 2; }            \
                CNT_REG--;                                                      \
                cycles -= is486 ? 3 : 4;                                        \
                if (cycles < cycles_end)                                        \
                        break;                                                  \
        }                                                                       \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_MOVSL_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);    \
        addr64a[0] = addr64a[1] = addr64a[2] = addr64a[3] = 0x00000000;         \
        addr64a_2[0] = addr64a_2[1] = addr64a_2[2] = addr64a_2[3] = 0x00000000; \
        if (trap)                                                               \
                cycles_end = cycles+1; /*Force the instruction to end after only one iteration when trap flag set*/     \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                SEG_CHECK_READ(cpu_state.ea_seg);                               \
                SEG_CHECK_WRITE(&cpu_state.seg_es);                             \
        }  									\
        while (CNT_REG > 0)                                                     \
        {                                                                       \
                uint32_t temp;                                                  \
                                                                                \
                CHECK_READ_REP(cpu_state.ea_seg, SRC_REG, SRC_REG + 3UL);       \
                high_page = 0;                                                  \
                do_mmut_rl(cpu_state.ea_seg->base, SRC_REG, addr64a);           \
                if (cpu_state.abrt) break;                                      \
                CHECK_WRITE_REP(&cpu_state.seg_es, DEST_REG, DEST_REG + 3UL);   \
                do_mmut_wl(es, DEST_REG, addr64a_2);                            \
                if (cpu_state.abrt) break;                                      \
                temp = readmeml_n(cpu_state.ea_seg->base, SRC_REG, addr64a); if (cpu_state.abrt) return 1;    \
                writememl_n(es, DEST_REG, addr64a_2, temp); if (cpu_state.abrt) return 1;    \
                                                                                \
                if (cpu_state.flags & D_FLAG) { DEST_REG -= 4; SRC_REG -= 4; }  \
                else                { DEST_REG += 4; SRC_REG += 4; }            \
                CNT_REG--;                                                      \
                cycles -= is486 ? 3 : 4;                                        \
                if (cycles < cycles_end)                                        \
                        break;                                                  \
        }                                                                       \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
                                                                                \
                                                                                \
static int opREP_STOSB_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);    \
        if (trap)                                                               \
                cycles_end = cycles+1; /*Force the instruction to end after only one iteration when trap flag set*/     \
        if (CNT_REG > 0)                                                        \
                SEG_CHECK_WRITE(&cpu_state.seg_es);                             \
        while (CNT_REG > 0)                                                     \
        {                                                                       \
                CHECK_WRITE_REP(&cpu_state.seg_es, DEST_REG, DEST_REG);         \
                writememb(es, DEST_REG, AL); if (cpu_state.abrt) return 1;      \
                if (cpu_state.flags & D_FLAG) DEST_REG--;                       \
                else                DEST_REG++;                                 \
                CNT_REG--;                                                      \
                cycles -= is486 ? 4 : 5;                                        \
                if (cycles < cycles_end)                                        \
                        break;                                                  \
        }                                                                       \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_STOSW_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);    \
        if (trap)                                                               \
                cycles_end = cycles+1; /*Force the instruction to end after only one iteration when trap flag set*/     \
        if (CNT_REG > 0)                                                        \
                SEG_CHECK_WRITE(&cpu_state.seg_es);                             \
        while (CNT_REG > 0)                                                     \
        {                                                                       \
                CHECK_WRITE_REP(&cpu_state.seg_es, DEST_REG, DEST_REG + 1UL);   \
                writememw(es, DEST_REG, AX); if (cpu_state.abrt) return 1;      \
                if (cpu_state.flags & D_FLAG) DEST_REG -= 2;                    \
                else                DEST_REG += 2;                              \
                CNT_REG--;                                                      \
                cycles -= is486 ? 4 : 5;                                        \
                if (cycles < cycles_end)                                        \
                        break;                                                  \
        }                                                                       \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_STOSL_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);    \
        if (trap)                                                               \
                cycles_end = cycles+1; /*Force the instruction to end after only one iteration when trap flag set*/     \
        if (CNT_REG > 0)                                                        \
                SEG_CHECK_WRITE(&cpu_state.seg_es);                             \
        while (CNT_REG > 0)                                                     \
        {                                                                       \
                CHECK_WRITE_REP(&cpu_state.seg_es, DEST_REG, DEST_REG + 3UL);   \
                writememl(es, DEST_REG, EAX); if (cpu_state.abrt) return 1;     \
                if (cpu_state.flags & D_FLAG) DEST_REG -= 4;                    \
                else                DEST_REG += 4;                              \
                CNT_REG--;                                                      \
                cycles -= is486 ? 4 : 5;                                        \
                if (cycles < cycles_end)                                        \
                        break;                                                  \
        }                                                                       \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
                                                                                \
static int opREP_LODSB_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);    \
        if (trap)                                                               \
                cycles_end = cycles+1; /*Force the instruction to end after only one iteration when trap flag set*/     \
        if (CNT_REG > 0)                                                        \
                SEG_CHECK_READ(cpu_state.ea_seg);                               \
        while (CNT_REG > 0)                                                     \
        {                                                                       \
                CHECK_READ_REP(cpu_state.ea_seg, SRC_REG, SRC_REG);             \
                AL = readmemb(cpu_state.ea_seg->base, SRC_REG); if (cpu_state.abrt) return 1;      \
                if (cpu_state.flags & D_FLAG) SRC_REG--;                       \
                else                SRC_REG++;                                  \
                CNT_REG--;                                                      \
                cycles -= is486 ? 4 : 5;                                        \
                if (cycles < cycles_end)                                        \
                        break;                                                  \
        }                                                                       \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_LODSW_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);    \
        if (trap)                                                               \
                cycles_end = cycles+1; /*Force the instruction to end after only one iteration when trap flag set*/     \
        if (CNT_REG > 0)                                                        \
                SEG_CHECK_READ(cpu_state.ea_seg);                               \
        while (CNT_REG > 0)                                                     \
        {                                                                       \
                CHECK_READ_REP(cpu_state.ea_seg, SRC_REG, SRC_REG + 1UL);       \
                AX = readmemw(cpu_state.ea_seg->base, SRC_REG); if (cpu_state.abrt) return 1;      \
                if (cpu_state.flags & D_FLAG) SRC_REG -= 2;                     \
                else                SRC_REG += 2;                               \
                CNT_REG--;                                                      \
                cycles -= is486 ? 4 : 5;                                        \
                if (cycles < cycles_end)                                        \
                        break;                                                  \
        }                                                                       \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_LODSL_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);    \
        if (trap)                                                               \
                cycles_end = cycles+1; /*Force the instruction to end after only one iteration when trap flag set*/     \
        if (CNT_REG > 0)                                                        \
                SEG_CHECK_READ(cpu_state.ea_seg);                               \
        while (CNT_REG > 0)                                                     \
        {                                                                       \
                CHECK_READ_REP(cpu_state.ea_seg, SRC_REG, SRC_REG + 3UL);       \
                EAX = readmeml(cpu_state.ea_seg->base, SRC_REG); if (cpu_state.abrt) return 1;     \
                if (cpu_state.flags & D_FLAG) SRC_REG -= 4;                     \
                else                SRC_REG += 4;                               \
                CNT_REG--;                                                      \
                cycles -= is486 ? 4 : 5;                                        \
                if (cycles < cycles_end)                                        \
                        break;                                                  \
        }                                                                       \
        if (CNT_REG > 0)                                                        \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \


#define CHEK_READ(a, b, c)


#define REP_OPS_CMPS_SCAS(size, CNT_REG, SRC_REG, DEST_REG, FV) \
static int opREP_CMPSB_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int tempz;                                                              \
                                                                                \
        addr64 = addr64_2 = 0x00000000;                                         \
                                                                                \
        tempz = FV;                                                             \
        if ((CNT_REG > 0) && (FV == tempz))                                     \
        {                                                                       \
                uint8_t temp, temp2;                                            \
                SEG_CHECK_READ(cpu_state.ea_seg);                               \
                SEG_CHECK_READ(&cpu_state.seg_es);                              \
                CHECK_READ(cpu_state.ea_seg, SRC_REG, SRC_REG);                 \
                high_page = uncached = 0;                                       \
                do_mmut_rb(cpu_state.ea_seg->base, SRC_REG, &addr64);           \
                if (cpu_state.abrt) return 1;                                   \
                CHECK_READ(&cpu_state.seg_es, DEST_REG, DEST_REG);              \
                do_mmut_rb2(es, DEST_REG, &addr64_2);                           \
                if (cpu_state.abrt) return 1;                                   \
                temp = readmemb_n(cpu_state.ea_seg->base, SRC_REG, addr64); if (cpu_state.abrt) return 1;   \
                if (uncached)                                                   \
                        readlookup2[(uint32_t)(es+DEST_REG)>>12] = old_rl2;     \
                temp2 = readmemb_n(es, DEST_REG, addr64_2); if (cpu_state.abrt) return 1;   \
                if (uncached)                                                   \
                        readlookup2[(uint32_t)(es+DEST_REG)>>12] = (uintptr_t) LOOKUP_INV; \
                                                                                \
                if (cpu_state.flags & D_FLAG) { DEST_REG--; SRC_REG--; }        \
                else                { DEST_REG++; SRC_REG++; }                  \
                CNT_REG--;                                                      \
                cycles -= is486 ? 7 : 9;                                        \
                setsub8(temp, temp2);                                           \
                tempz = (ZF_SET()) ? 1 : 0;                                     \
        }                                                                       \
        if ((CNT_REG > 0) && (FV == tempz))                                     \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_CMPSW_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int tempz;                                                              \
                                                                                \
        addr64a[0] = addr64a[1] = 0x00000000;                                   \
        addr64a_2[0] = addr64a_2[1] = 0x00000000;                               \
                                                                                \
        tempz = FV;                                                             \
        if ((CNT_REG > 0) && (FV == tempz))                                     \
        {                                                                       \
                uint16_t temp, temp2;                                           \
                SEG_CHECK_READ(cpu_state.ea_seg);                               \
                SEG_CHECK_READ(&cpu_state.seg_es);                              \
                CHECK_READ(cpu_state.ea_seg, SRC_REG, SRC_REG + 1UL);           \
                high_page = uncached = 0;                                       \
                do_mmut_rw(cpu_state.ea_seg->base, SRC_REG, addr64a);           \
                if (cpu_state.abrt) return 1;                                   \
                CHECK_READ(&cpu_state.seg_es, DEST_REG, DEST_REG + 1UL);        \
                do_mmut_rw2(es, DEST_REG, addr64a_2);                           \
                if (cpu_state.abrt) return 1;                                   \
                temp = readmemw_n(cpu_state.ea_seg->base, SRC_REG, addr64a); if (cpu_state.abrt) return 1;   \
                if (uncached)                                                   \
                        readlookup2[(uint32_t)(es+DEST_REG)>>12] = old_rl2;     \
                temp2 = readmemw_n(es, DEST_REG, addr64a_2); if (cpu_state.abrt) return 1;   \
                if (uncached)                                                   \
                        readlookup2[(uint32_t)(es+DEST_REG)>>12] = (uintptr_t) LOOKUP_INV; \
                                                                                \
                if (cpu_state.flags & D_FLAG) { DEST_REG -= 2; SRC_REG -= 2; }            \
                else                { DEST_REG += 2; SRC_REG += 2; }            \
                CNT_REG--;                                                      \
                cycles -= is486 ? 7 : 9;                                        \
                setsub16(temp, temp2);                                          \
                tempz = (ZF_SET()) ? 1 : 0;                                     \
        }                                                                       \
        if ((CNT_REG > 0) && (FV == tempz))                                     \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_CMPSL_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int tempz;                                                              \
                                                                                \
        addr64a[0] = addr64a[1] = addr64a[2] = addr64a[3] = 0x00000000;         \
        addr64a_2[0] = addr64a_2[1] = addr64a_2[2] = addr64a_2[3] = 0x00000000; \
                                                                                \
        tempz = FV;                                                             \
        if ((CNT_REG > 0) && (FV == tempz))                                     \
        {                                                                       \
                uint32_t temp, temp2;                                           \
                SEG_CHECK_READ(cpu_state.ea_seg);                               \
                SEG_CHECK_READ(&cpu_state.seg_es);                              \
                CHECK_READ(cpu_state.ea_seg, SRC_REG, SRC_REG + 3UL);           \
                high_page = uncached = 0;                                       \
                do_mmut_rl(cpu_state.ea_seg->base, SRC_REG, addr64a);           \
                if (cpu_state.abrt) return 1;                                   \
                CHECK_READ(&cpu_state.seg_es, DEST_REG, DEST_REG + 3UL);        \
                do_mmut_rl2(es, DEST_REG, addr64a_2);                           \
                if (cpu_state.abrt) return 1;                                   \
                temp = readmeml_n(cpu_state.ea_seg->base, SRC_REG, addr64a); if (cpu_state.abrt) return 1;   \
                if (uncached)                                                   \
                        readlookup2[(uint32_t)(es+DEST_REG)>>12] = old_rl2;     \
                temp2 = readmeml_n(es, DEST_REG, addr64a_2); if (cpu_state.abrt) return 1;   \
                if (uncached)                                                   \
                        readlookup2[(uint32_t)(es+DEST_REG)>>12] = (uintptr_t) LOOKUP_INV; \
                                                                                \
                if (cpu_state.flags & D_FLAG) { DEST_REG -= 4; SRC_REG -= 4; }  \
                else                { DEST_REG += 4; SRC_REG += 4; }            \
                CNT_REG--;                                                      \
                cycles -= is486 ? 7 : 9;                                        \
                setsub32(temp, temp2);                                          \
                tempz = (ZF_SET()) ? 1 : 0;                                     \
        }                                                                       \
        if ((CNT_REG > 0) && (FV == tempz))                                     \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
                                                                                \
static int opREP_SCASB_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int tempz;                                                              \
        int cycles_end = cycles - 1000;                                         \
        if (trap)                                                               \
                cycles_end = cycles+1; /*Force the instruction to end after only one iteration when trap flag set*/     \
        tempz = FV;                                                             \
        if ((CNT_REG > 0) && (FV == tempz))                                     \
                SEG_CHECK_READ(&cpu_state.seg_es);                              \
        while ((CNT_REG > 0) && (FV == tempz))                                  \
        {                                                                       \
                CHECK_READ_REP(&cpu_state.seg_es, DEST_REG, DEST_REG);          \
                uint8_t temp = readmemb(es, DEST_REG); if (cpu_state.abrt) break;\
                setsub8(AL, temp);                                              \
                tempz = (ZF_SET()) ? 1 : 0;                                     \
                if (cpu_state.flags & D_FLAG) DEST_REG--;                       \
                else                DEST_REG++;                                 \
                CNT_REG--;                                                      \
                cycles -= is486 ? 5 : 8;                                        \
                if (cycles < cycles_end)                                        \
                        break;                                                  \
        }                                                                       \
        if ((CNT_REG > 0) && (FV == tempz))                                     \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_SCASW_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int tempz;                                                              \
        int cycles_end = cycles - 1000;                                         \
        if (trap)                                                               \
                cycles_end = cycles+1; /*Force the instruction to end after only one iteration when trap flag set*/     \
        tempz = FV;                                                             \
        if ((CNT_REG > 0) && (FV == tempz))                                     \
                SEG_CHECK_READ(&cpu_state.seg_es);                              \
        while ((CNT_REG > 0) && (FV == tempz))                                  \
        {                                                                       \
                CHECK_READ_REP(&cpu_state.seg_es, DEST_REG, DEST_REG + 1UL);    \
                uint16_t temp = readmemw(es, DEST_REG); if (cpu_state.abrt) break;\
                setsub16(AX, temp);                                             \
                tempz = (ZF_SET()) ? 1 : 0;                                     \
                if (cpu_state.flags & D_FLAG) DEST_REG -= 2;                    \
                else                DEST_REG += 2;                              \
                CNT_REG--;                                                      \
                cycles -= is486 ? 5 : 8;                                        \
                if (cycles < cycles_end)                                        \
                        break;                                                  \
        }                                                                       \
        if ((CNT_REG > 0) && (FV == tempz))                                     \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}                                                                               \
static int opREP_SCASL_ ## size(uint32_t fetchdat)                              \
{                                                                               \
        int tempz;                                                              \
        int cycles_end = cycles - 1000;                                         \
        if (trap)                                                               \
                cycles_end = cycles+1; /*Force the instruction to end after only one iteration when trap flag set*/     \
        tempz = FV;                                                             \
        if ((CNT_REG > 0) && (FV == tempz))                                     \
                SEG_CHECK_READ(&cpu_state.seg_es);                              \
        while ((CNT_REG > 0) && (FV == tempz))                                  \
        {                                                                       \
                CHECK_READ_REP(&cpu_state.seg_es, DEST_REG, DEST_REG + 3UL);    \
                uint32_t temp = readmeml(es, DEST_REG); if (cpu_state.abrt) break;\
                setsub32(EAX, temp);                                            \
                tempz = (ZF_SET()) ? 1 : 0;                                     \
                if (cpu_state.flags & D_FLAG) DEST_REG -= 4;                    \
                else                DEST_REG += 4;                              \
                CNT_REG--;                                                      \
                cycles -= is486 ? 5 : 8;                                        \
                if (cycles < cycles_end)                                        \
                        break;                                                  \
        }                                                                       \
        if ((CNT_REG > 0) && (FV == tempz))                                     \
        {                                                                       \
                CPU_BLOCK_END();                                                \
                cpu_state.pc = cpu_state.oldpc;                                 \
                return 1;                                                       \
        }                                                                       \
        return cpu_state.abrt;                                                  \
}

REP_OPS(a16, CX, SI, DI)
REP_OPS(a32, ECX, ESI, EDI)
REP_OPS_CMPS_SCAS(a16_NE, CX, SI, DI, 0)
REP_OPS_CMPS_SCAS(a16_E,  CX, SI, DI, 1)
REP_OPS_CMPS_SCAS(a32_NE, ECX, ESI, EDI, 0)
REP_OPS_CMPS_SCAS(a32_E,  ECX, ESI, EDI, 1)

static int opREPNE(uint32_t fetchdat)
{
        fetchdat = fastreadl(cs + cpu_state.pc);
        if (cpu_state.abrt) return 1;
        cpu_state.pc++;

        CLOCK_CYCLES(2);
        if (x86_opcodes_REPNE[(fetchdat & 0xff) | cpu_state.op32])
                return x86_opcodes_REPNE[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
        return x86_opcodes[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
}
static int opREPE(uint32_t fetchdat)
{
        fetchdat = fastreadl(cs + cpu_state.pc);
        if (cpu_state.abrt) return 1;
        cpu_state.pc++;

        CLOCK_CYCLES(2);
        if (x86_opcodes_REPE[(fetchdat & 0xff) | cpu_state.op32])
                return x86_opcodes_REPE[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
        return x86_opcodes[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
}
