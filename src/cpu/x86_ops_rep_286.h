#define REP_OPS_286(size, CNT_REG, SRC_REG, DEST_REG)                                                             \
    static int opREP_INSB_286_##size(UNUSED(uint32_t fetchdat))                                                   \
    {                                                                                                             \
        int reads = 0, writes = 0, total_cycles = 0;                                                              \
        uint16_t ins_addr;                                                                                        \
                                                                                                                  \
        addr64 = 0x00000000;                                                                                      \
                                                                                                                  \
        if (CNT_REG > 0) {                                                                                        \
            uint8_t temp;                                                                                         \
                                                                                                                  \
            ins_addr = DEST_REG;                                                                                  \
            if (cpu_state.flags & D_FLAG)                                                                         \
                DEST_REG--;                                                                                       \
            else                                                                                                  \
                DEST_REG++;                                                                                       \
                                                                                                                  \
            check_io_perm(DX, 1);                                                                                 \
                                                                                                                  \
            CNT_REG--;                                                                                            \
                                                                                                                  \
            SEG_CHECK_WRITE(&cpu_state.seg_es);                                                                   \
            CHECK_WRITE(&cpu_state.seg_es, ins_addr, ins_addr);                                                   \
            high_page = 0;                                                                                        \
            do_mmut_wb(es, ins_addr, &addr64);                                                                    \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
                                                                                                                  \
            temp = inb(DX);                                                                                       \
            writememb_n(es, ins_addr, addr64, temp);                                                              \
                                                                                                                  \
            cycles -= 15;                                                                                         \
            reads++;                                                                                              \
            writes++;                                                                                             \
            total_cycles += 15;                                                                                   \
        }                                                                                                         \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, writes, 0, 0);                                                \
        if (CNT_REG > 0) {                                                                                        \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt;                                                                                    \
    }                                                                                                             \
    static int opREP_INSW_286_##size(UNUSED(uint32_t fetchdat))                                                   \
    {                                                                                                             \
        int reads = 0, writes = 0, total_cycles = 0;                                                              \
        uint16_t ins_addr;                                                                                        \
                                                                                                                  \
        addr64a[0] = addr64a[1] = 0x00000000;                                                                     \
                                                                                                                  \
        if (CNT_REG > 0) {                                                                                        \
            uint16_t temp;                                                                                        \
                                                                                                                  \
            ins_addr = DEST_REG;                                                                                  \
            if (cpu_state.flags & D_FLAG)                                                                         \
                DEST_REG -= 2;                                                                                    \
            else                                                                                                  \
                DEST_REG += 2;                                                                                    \
                                                                                                                  \
            check_io_perm(DX, 2);                                                                                 \
                                                                                                                  \
            CNT_REG--;                                                                                            \
                                                                                                                  \
            SEG_CHECK_WRITE(&cpu_state.seg_es);                                                                   \
            CHECK_WRITE(&cpu_state.seg_es, ins_addr, ins_addr + 1UL);                                             \
            high_page = 0;                                                                                        \
            do_mmut_ww(es, ins_addr, addr64a);                                                                    \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
                                                                                                                  \
            temp = inw(DX);                                                                                       \
            writememw_n(es, ins_addr, addr64a, temp);                                                             \
                                                                                                                  \
            cycles -= 15;                                                                                         \
            reads++;                                                                                              \
            writes++;                                                                                             \
            total_cycles += 15;                                                                                   \
        }                                                                                                         \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, writes, 0, 0);                                                \
        if (CNT_REG > 0) {                                                                                        \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt;                                                                                    \
    }                                                                                                             \
                                                                                                                  \
    static int opREP_OUTSB_286_##size(UNUSED(uint32_t fetchdat))                                                  \
    {                                                                                                             \
        int reads = 0, writes = 0, total_cycles = 0;                                                              \
        uint16_t ins_addr;                                                                                        \
                                                                                                                  \
        addr64 = 0x00000000;                                                                                      \
                                                                                                                  \
        if (CNT_REG > 0) {                                                                                        \
            uint8_t temp;                                                                                         \
                                                                                                                  \
            ins_addr = SRC_REG;                                                                                   \
            if (cpu_state.flags & D_FLAG)                                                                         \
                SRC_REG--;                                                                                        \
            else                                                                                                  \
                SRC_REG++;                                                                                        \
            CNT_REG--;                                                                                            \
                                                                                                                  \
            SEG_CHECK_READ(cpu_state.ea_seg);                                                                     \
            CHECK_READ(cpu_state.ea_seg, ins_addr, ins_addr);                                                     \
            high_page = 0;                                                                                        \
            do_mmut_rb(cpu_state.ea_seg->base, ins_addr, &addr64);                                                \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
            check_io_perm(DX, 1);                                                                                 \
                                                                                                                  \
            temp = readmemb_n(cpu_state.ea_seg->base, ins_addr, addr64);                                          \
            outb(DX, temp);                                                                                       \
                                                                                                                  \
            cycles -= 14;                                                                                         \
            reads++;                                                                                              \
            writes++;                                                                                             \
            total_cycles += 14;                                                                                   \
        }                                                                                                         \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, writes, 0, 0);                                                \
        if (CNT_REG > 0) {                                                                                        \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt;                                                                                    \
    }                                                                                                             \
    static int opREP_OUTSW_286_##size(UNUSED(uint32_t fetchdat))                                                  \
    {                                                                                                             \
        int reads = 0, writes = 0, total_cycles = 0;                                                              \
        uint16_t ins_addr;                                                                                        \
                                                                                                                  \
        addr64a[0] = addr64a[1] = 0x00000000;                                                                     \
                                                                                                                  \
        if (CNT_REG > 0) {                                                                                        \
            uint16_t temp;                                                                                        \
                                                                                                                  \
            ins_addr = SRC_REG;                                                                                   \
            if (cpu_state.flags & D_FLAG)                                                                         \
                SRC_REG -= 2;                                                                                     \
            else                                                                                                  \
                SRC_REG += 2;                                                                                     \
            CNT_REG--;                                                                                            \
                                                                                                                  \
            SEG_CHECK_READ(cpu_state.ea_seg);                                                                     \
            CHECK_READ(cpu_state.ea_seg, ins_addr, ins_addr + 1UL);                                               \
            high_page = 0;                                                                                        \
            do_mmut_rw(cpu_state.ea_seg->base, ins_addr, addr64a);                                                \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
            check_io_perm(DX, 2);                                                                                 \
                                                                                                                  \
            temp = readmemw_n(cpu_state.ea_seg->base, ins_addr, addr64a);                                         \
            outw(DX, temp);                                                                                       \
                                                                                                                  \
            cycles -= 14;                                                                                         \
            reads++;                                                                                              \
            writes++;                                                                                             \
            total_cycles += 14;                                                                                   \
        }                                                                                                         \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, writes, 0, 0);                                                \
        if (CNT_REG > 0) {                                                                                        \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt;                                                                                    \
    }                                                                                                             \
                                                                                                                  \
    static int opREP_MOVSB_286_##size(UNUSED(uint32_t fetchdat))                                                  \
    {                                                                                                             \
        int reads = 0, writes = 0, total_cycles = 0;                                                              \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);                                      \
        uint16_t ins_addr, ins_addr_2;                                                                            \
        addr64 = addr64_2 = 0x00000000;                                                                           \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*Force the instruction to end after only one iteration when trap flag set*/ \
        while (CNT_REG > 0) {                                                                                     \
            uint8_t temp;                                                                                         \
                                                                                                                  \
            ins_addr = SRC_REG;                                                                                   \
            if (cpu_state.flags & D_FLAG)                                                                         \
                SRC_REG--;                                                                                        \
            else                                                                                                  \
                SRC_REG++;                                                                                        \
                                                                                                                  \
            SEG_CHECK_READ_REP(cpu_state.ea_seg);                                                                 \
            CHECK_READ_REP(cpu_state.ea_seg, ins_addr, ins_addr);                                                 \
            high_page = 0;                                                                                        \
            do_mmut_rb(cpu_state.ea_seg->base, ins_addr, &addr64);                                                \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            ins_addr_2 = DEST_REG;                                                                                \
            if (cpu_state.flags & D_FLAG)                                                                         \
                DEST_REG--;                                                                                       \
            else                                                                                                  \
                DEST_REG++;                                                                                       \
            CNT_REG--;                                                                                            \
                                                                                                                  \
            SEG_CHECK_WRITE_REP(&cpu_state.seg_es);                                                               \
            CHECK_WRITE_REP(&cpu_state.seg_es, ins_addr_2, ins_addr_2);                                           \
            high_page = 0;                                                                                        \
            do_mmut_wb(es, ins_addr_2, &addr64_2);                                                                \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            temp = readmemb_n(cpu_state.ea_seg->base, ins_addr, addr64);                                          \
            writememb_n(es, ins_addr_2, addr64_2, temp);                                                          \
                                                                                                                  \
            cycles -= is486 ? 3 : 4;                                                                              \
            reads++;                                                                                              \
            writes++;                                                                                             \
            total_cycles += is486 ? 3 : 4;                                                                        \
            if (cycles < cycles_end)                                                                              \
                break;                                                                                            \
        }                                                                                                         \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, writes, 0, 0);                                                \
        if (CNT_REG > 0) {                                                                                        \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt;                                                                                    \
    }                                                                                                             \
    static int opREP_MOVSW_286_##size(UNUSED(uint32_t fetchdat))                                                  \
    {                                                                                                             \
        int reads = 0, writes = 0, total_cycles = 0;                                                              \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);                                      \
        uint16_t ins_addr, ins_addr_2;                                                                            \
        addr64a[0] = addr64a[1] = 0x00000000;                                                                     \
        addr64a_2[0] = addr64a_2[1] = 0x00000000;                                                                 \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*Force the instruction to end after only one iteration when trap flag set*/ \
        while (CNT_REG > 0) {                                                                                     \
            uint16_t temp;                                                                                        \
                                                                                                                  \
            ins_addr = SRC_REG;                                                                                   \
            if (cpu_state.flags & D_FLAG)                                                                         \
                SRC_REG -= 2;                                                                                     \
            else                                                                                                  \
                SRC_REG += 2;                                                                                     \
                                                                                                                  \
            SEG_CHECK_READ_REP(cpu_state.ea_seg);                                                                 \
            CHECK_READ_REP(cpu_state.ea_seg, ins_addr, ins_addr + 1UL);                                           \
            high_page = 0;                                                                                        \
            do_mmut_rw(cpu_state.ea_seg->base, ins_addr, addr64a);                                                \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            ins_addr_2 = DEST_REG;                                                                                \
            if (cpu_state.flags & D_FLAG)                                                                         \
                DEST_REG -= 2;                                                                                    \
            else                                                                                                  \
                DEST_REG += 2;                                                                                    \
            CNT_REG--;                                                                                            \
                                                                                                                  \
            SEG_CHECK_WRITE_REP(&cpu_state.seg_es);                                                               \
            CHECK_WRITE_REP(&cpu_state.seg_es, ins_addr_2, ins_addr_2 + 1UL);                                     \
            high_page = 0;                                                                                        \
            do_mmut_ww(es, ins_addr_2, addr64a_2);                                                                \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            temp = readmemw_n(cpu_state.ea_seg->base, ins_addr, addr64a);                                         \
            writememw_n(es, ins_addr_2, addr64a_2, temp);                                                         \
                                                                                                                  \
            cycles -= is486 ? 3 : 4;                                                                              \
            reads++;                                                                                              \
            writes++;                                                                                             \
            total_cycles += is486 ? 3 : 4;                                                                        \
            if (cycles < cycles_end)                                                                              \
                break;                                                                                            \
        }                                                                                                         \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, writes, 0, 0);                                                \
        if (CNT_REG > 0) {                                                                                        \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt;                                                                                    \
    }                                                                                                             \
                                                                                                                  \
    static int opREP_STOSB_286_##size(UNUSED(uint32_t fetchdat))                                                  \
    {                                                                                                             \
        int writes = 0, total_cycles = 0;                                                                         \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);                                      \
        uint16_t ins_addr;                                                                                        \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*Force the instruction to end after only one iteration when trap flag set*/ \
        while (CNT_REG > 0) {                                                                                     \
            ins_addr = DEST_REG;                                                                                  \
            if (cpu_state.flags & D_FLAG)                                                                         \
                DEST_REG--;                                                                                       \
            else                                                                                                  \
                DEST_REG++;                                                                                       \
            CNT_REG--;                                                                                            \
                                                                                                                  \
            SEG_CHECK_WRITE_REP(&cpu_state.seg_es);                                                               \
            CHECK_WRITE_REP(&cpu_state.seg_es, ins_addr, ins_addr);                                               \
            writememb(es, ins_addr, AL);                                                                          \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            cycles -= is486 ? 4 : 5;                                                                              \
            writes++;                                                                                             \
            total_cycles += is486 ? 4 : 5;                                                                        \
            if (cycles < cycles_end)                                                                              \
                break;                                                                                            \
        }                                                                                                         \
        PREFETCH_RUN(total_cycles, 1, -1, 0, 0, writes, 0, 0);                                                    \
        if (CNT_REG > 0) {                                                                                        \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt;                                                                                    \
    }                                                                                                             \
    static int opREP_STOSW_286_##size(UNUSED(uint32_t fetchdat))                                                  \
    {                                                                                                             \
        int writes = 0, total_cycles = 0;                                                                         \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);                                      \
        uint16_t ins_addr;                                                                                        \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*Force the instruction to end after only one iteration when trap flag set*/ \
        while (CNT_REG > 0) {                                                                                     \
            ins_addr = DEST_REG;                                                                                  \
            if (cpu_state.flags & D_FLAG)                                                                         \
                DEST_REG -= 2;                                                                                    \
            else                                                                                                  \
                DEST_REG += 2;                                                                                    \
            CNT_REG--;                                                                                            \
                                                                                                                  \
            SEG_CHECK_WRITE_REP(&cpu_state.seg_es);                                                               \
            CHECK_WRITE_REP(&cpu_state.seg_es, ins_addr, ins_addr + 1UL);                                         \
            writememw(es, ins_addr, AX);                                                                          \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            cycles -= is486 ? 4 : 5;                                                                              \
            writes++;                                                                                             \
            total_cycles += is486 ? 4 : 5;                                                                        \
            if (cycles < cycles_end)                                                                              \
                break;                                                                                            \
        }                                                                                                         \
        PREFETCH_RUN(total_cycles, 1, -1, 0, 0, writes, 0, 0);                                                    \
        if (CNT_REG > 0) {                                                                                        \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt;                                                                                    \
    }                                                                                                             \
                                                                                                                  \
    static int opREP_LODSB_286_##size(UNUSED(uint32_t fetchdat))                                                  \
    {                                                                                                             \
        int reads = 0, total_cycles = 0;                                                                          \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);                                      \
        uint16_t ins_addr;                                                                                        \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*Force the instruction to end after only one iteration when trap flag set*/ \
        while (CNT_REG > 0) {                                                                                     \
            ins_addr = SRC_REG;                                                                                   \
            if (cpu_state.flags & D_FLAG)                                                                         \
                SRC_REG--;                                                                                        \
            else                                                                                                  \
                SRC_REG++;                                                                                        \
            CNT_REG--;                                                                                            \
                                                                                                                  \
            SEG_CHECK_READ_REP(cpu_state.ea_seg);                                                                 \
            CHECK_READ_REP(cpu_state.ea_seg, ins_addr, ins_addr);                                                 \
            uint8_t new_AL = readmemb(cpu_state.ea_seg->base, ins_addr);                                          \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            AL = new_AL;                                                                                          \
                                                                                                                  \
            cycles -= is486 ? 4 : 5;                                                                              \
            reads++;                                                                                              \
            total_cycles += is486 ? 4 : 5;                                                                        \
            if (cycles < cycles_end)                                                                              \
                break;                                                                                            \
        }                                                                                                         \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, 0, 0, 0);                                                     \
        if (CNT_REG > 0) {                                                                                        \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt;                                                                                    \
    }                                                                                                             \
    static int opREP_LODSW_286_##size(UNUSED(uint32_t fetchdat))                                                  \
    {                                                                                                             \
        int reads = 0, total_cycles = 0;                                                                          \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);                                      \
        uint16_t ins_addr;                                                                                        \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*Force the instruction to end after only one iteration when trap flag set*/ \
        while (CNT_REG > 0) {                                                                                     \
            ins_addr = SRC_REG;                                                                                   \
            if (cpu_state.flags & D_FLAG)                                                                         \
                SRC_REG -= 2;                                                                                     \
            else                                                                                                  \
                SRC_REG += 2;                                                                                     \
            CNT_REG--;                                                                                            \
                                                                                                                  \
            SEG_CHECK_READ_REP(cpu_state.ea_seg);                                                                 \
            CHECK_READ_REP(cpu_state.ea_seg, ins_addr, ins_addr + 1UL);                                           \
            uint16_t new_AX = readmemw(cpu_state.ea_seg->base, ins_addr);                                         \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            AX = new_AX;                                                                                          \
                                                                                                                  \
            cycles -= is486 ? 4 : 5;                                                                              \
            reads++;                                                                                              \
            total_cycles += is486 ? 4 : 5;                                                                        \
            if (cycles < cycles_end)                                                                              \
                break;                                                                                            \
        }                                                                                                         \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, 0, 0, 0);                                                     \
        if (CNT_REG > 0) {                                                                                        \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt;                                                                                    \
    }                                                                                                             \

#define REP_OPS_CMPS_SCAS_286(size, CNT_REG, SRC_REG, DEST_REG, FV)                                               \
    static int opREP_CMPSB_286_##size(UNUSED(uint32_t fetchdat))                                                  \
    {                                                                                                             \
        int reads = 0, total_cycles = 0, tempz;                                                                   \
        uint16_t ins_addr, ins_addr_2;                                                                            \
        addr64 = addr64_2 = 0x00000000;                                                                           \
                                                                                                                  \
        tempz = FV;                                                                                               \
        if ((CNT_REG > 0) && (FV == tempz)) {                                                                     \
            uint8_t temp, temp2;                                                                                  \
                                                                                                                  \
            ins_addr = DEST_REG;                                                                                  \
            if (cpu_state.flags & D_FLAG)                                                                         \
                DEST_REG--;                                                                                       \
            else                                                                                                  \
                DEST_REG++;                                                                                       \
                                                                                                                  \
            SEG_CHECK_READ(&cpu_state.seg_es);                                                                    \
            CHECK_READ(&cpu_state.seg_es, ins_addr, ins_addr);                                                    \
            high_page = 0;                                                                                        \
            do_mmut_rb(es, ins_addr, &addr64);                                                                    \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
                                                                                                                  \
            ins_addr_2 = SRC_REG;                                                                                 \
            if (cpu_state.flags & D_FLAG)                                                                         \
                SRC_REG--;                                                                                        \
            else                                                                                                  \
                SRC_REG++;                                                                                        \
            CNT_REG--;                                                                                            \
                                                                                                                  \
            SEG_CHECK_READ(cpu_state.ea_seg);                                                                     \
            CHECK_READ(cpu_state.ea_seg, ins_addr_2, ins_addr_2);                                                 \
            high_page = 0;                                                                                        \
            do_mmut_rb2(cpu_state.ea_seg->base, ins_addr_2, &addr64_2);                                           \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
                                                                                                                  \
            temp = readmemb_n(es, ins_addr, addr64);                                                              \
            is_compare = 1;                                                                                       \
            temp2 = readmemb_n2(cpu_state.ea_seg->base, ins_addr_2, addr64_2);                                    \
            is_compare = 0;                                                                                       \
                                                                                                                  \
            cycles -= is486 ? 7 : 9;                                                                              \
            reads += 2;                                                                                           \
            total_cycles += is486 ? 7 : 9;                                                                        \
            setsub8(temp2, temp);                                                                                 \
            tempz = (ZF_SET()) ? 1 : 0;                                                                           \
        }                                                                                                         \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, 0, 0, 0);                                                     \
        if ((CNT_REG > 0) && (FV == tempz)) {                                                                     \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt;                                                                                    \
    }                                                                                                             \
    static int opREP_CMPSW_286_##size(UNUSED(uint32_t fetchdat))                                                  \
    {                                                                                                             \
        int reads = 0, total_cycles = 0, tempz;                                                                   \
        uint16_t ins_addr, ins_addr_2;                                                                            \
        addr64a[0] = addr64a[1] = 0x00000000;                                                                     \
        addr64a_2[0] = addr64a_2[1] = 0x00000000;                                                                 \
                                                                                                                  \
        tempz = FV;                                                                                               \
        if ((CNT_REG > 0) && (FV == tempz)) {                                                                     \
            uint16_t temp, temp2;                                                                                 \
                                                                                                                  \
            ins_addr = DEST_REG;                                                                                  \
            if (cpu_state.flags & D_FLAG)                                                                         \
                DEST_REG -= 2;                                                                                    \
            else                                                                                                  \
                DEST_REG += 2;                                                                                    \
                                                                                                                  \
            SEG_CHECK_READ(&cpu_state.seg_es);                                                                    \
            CHECK_READ(&cpu_state.seg_es, ins_addr, ins_addr + 1UL);                                              \
            high_page = 0;                                                                                        \
            do_mmut_rw(es, ins_addr, addr64a);                                                                    \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
                                                                                                                  \
            ins_addr_2 = SRC_REG;                                                                                 \
            if (cpu_state.flags & D_FLAG)                                                                         \
                SRC_REG -= 2;                                                                                     \
            else                                                                                                  \
                SRC_REG += 2;                                                                                     \
            CNT_REG--;                                                                                            \
                                                                                                                  \
            SEG_CHECK_READ(cpu_state.ea_seg);                                                                     \
            CHECK_READ(cpu_state.ea_seg, ins_addr_2, ins_addr_2 + 1UL);                                           \
            high_page = 0;                                                                                        \
            do_mmut_rw2(cpu_state.ea_seg->base, ins_addr_2, addr64a_2);                                           \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
                                                                                                                  \
            temp = readmemw_n(es, ins_addr, addr64a);                                                             \
            is_compare = 1;                                                                                       \
            temp2 = readmemw_n2(cpu_state.ea_seg->base, ins_addr_2, addr64a_2);                                   \
            is_compare = 0;                                                                                       \
                                                                                                                  \
            cycles -= is486 ? 7 : 9;                                                                              \
            reads += 2;                                                                                           \
            total_cycles += is486 ? 7 : 9;                                                                        \
            setsub16(temp2, temp);                                                                                \
            tempz = (ZF_SET()) ? 1 : 0;                                                                           \
        }                                                                                                         \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, 0, 0, 0);                                                     \
        if ((CNT_REG > 0) && (FV == tempz)) {                                                                     \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt;                                                                                    \
    }                                                                                                             \
                                                                                                                  \
    static int opREP_SCASB_286_##size(UNUSED(uint32_t fetchdat))                                                  \
    {                                                                                                             \
        int reads = 0, total_cycles = 0, tempz;                                                                   \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);                                      \
        uint16_t ins_addr;                                                                                        \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*Force the instruction to end after only one iteration when trap flag set*/ \
        tempz = FV;                                                                                               \
        while ((CNT_REG > 0) && (FV == tempz)) {                                                                  \
            ins_addr = DEST_REG;                                                                                  \
            if (cpu_state.flags & D_FLAG)                                                                         \
                DEST_REG--;                                                                                       \
            else                                                                                                  \
                DEST_REG++;                                                                                       \
            CNT_REG--;                                                                                            \
                                                                                                                  \
            SEG_CHECK_READ_REP(&cpu_state.seg_es);                                                                \
            CHECK_READ_REP(&cpu_state.seg_es, ins_addr, ins_addr);                                                \
            uint8_t temp = readmemb(es, ins_addr);                                                                \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            setsub8(AL, temp);                                                                                    \
            tempz = (ZF_SET()) ? 1 : 0;                                                                           \
            cycles -= is486 ? 5 : 8;                                                                              \
            reads++;                                                                                              \
            total_cycles += is486 ? 5 : 8;                                                                        \
            if (cycles < cycles_end)                                                                              \
                break;                                                                                            \
        }                                                                                                         \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, 0, 0, 0);                                                     \
        if ((CNT_REG > 0) && (FV == tempz)) {                                                                     \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt;                                                                                    \
    }                                                                                                             \
    static int opREP_SCASW_286_##size(UNUSED(uint32_t fetchdat))                                                  \
    {                                                                                                             \
        int reads = 0, total_cycles = 0, tempz;                                                                   \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);                                      \
        uint16_t ins_addr;                                                                                        \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*Force the instruction to end after only one iteration when trap flag set*/ \
        tempz = FV;                                                                                               \
        while ((CNT_REG > 0) && (FV == tempz)) {                                                                  \
            ins_addr = DEST_REG;                                                                                  \
            if (cpu_state.flags & D_FLAG)                                                                         \
                DEST_REG -= 2;                                                                                    \
            else                                                                                                  \
                DEST_REG += 2;                                                                                    \
            CNT_REG--;                                                                                            \
                                                                                                                  \
            SEG_CHECK_READ_REP(&cpu_state.seg_es);                                                                \
            CHECK_READ_REP(&cpu_state.seg_es, ins_addr, ins_addr + 1UL);                                          \
            uint16_t temp = readmemw(es, ins_addr);                                                               \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            setsub16(AX, temp);                                                                                   \
            tempz = (ZF_SET()) ? 1 : 0;                                                                           \
            cycles -= is486 ? 5 : 8;                                                                              \
            reads++;                                                                                              \
            total_cycles += is486 ? 5 : 8;                                                                        \
            if (cycles < cycles_end)                                                                              \
                break;                                                                                            \
        }                                                                                                         \
        PREFETCH_RUN(total_cycles, 1, -1, reads, 0, 0, 0, 0);                                                     \
        if ((CNT_REG > 0) && (FV == tempz)) {                                                                     \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt;                                                                                    \
    }                                                                                                             \

REP_OPS_286(a16, CX, SI, DI)
REP_OPS_CMPS_SCAS_286(a16_NE, CX, SI, DI, 0)
REP_OPS_CMPS_SCAS_286(a16_E, CX, SI, DI, 1)
