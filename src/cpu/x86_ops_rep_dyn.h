/*Between two transfers of a REP INS or OUTS. Each used to be an instruction
  of its own, after which the loop could step in: so the string stops where
  the loop would take an event (exec386_dynarec(): an SMI, an abort, a reset
  or numeric exception asked for, a takeable NMI, an interrupt with IF set),
  with a debug register armed (a breakpoint traps after its own transfer),
  with the trap flag and when the pass's cycles are spent (cycles_end, as REP
  MOVS); and the timers due by then run in between (update_tsc(), as the
  loop runs them after a block), so a device sees its time pass transfer by
  transfer as before. Nonzero: stop here.*/
static __inline int
rep_io_event_due(void)
{
    return smi_line || cpu_state.abrt || cpu_init || new_ne || cpu_dr_armed || NMI_TAKEABLE() ||
           (cpu_events.irq && (cpu_state.flags & I_FLAG) && x86_external_interrupt_pending());
}

static __inline int
rep_io_stop(int cycles_end)
{
    if ((cycles < cycles_end) || rep_io_event_due())
        return 1;
    update_tsc();
    return rep_io_event_due();
}

/*What the loop charged a REP INS or OUTS each time it started again, when
  every transfer was a start of its own: the instruction's timing, charged by
  the block that runs it (or by the interpreter). Each transfer a pass does
  past its first pays it too, so the guest's time per transfer is as it was.
  It is the cycles spent between a pass that left with the count not done
  and the next one, at the same instruction with that count: an interrupt
  taken in between costs far more than REP_IO_DISPATCH_MAX, and is passed
  over.*/
#define REP_IO_DISPATCH_MAX 96
static uint32_t rep_io_left_at = 0xffffffff;
static uint32_t rep_io_left_cnt;
static int32_t  rep_io_left_cycles;

static __inline void
rep_io_enter(int *dispatch, uint32_t cnt)
{
    if (((cs + cpu_state.oldpc) == rep_io_left_at) && (cnt == rep_io_left_cnt)) {
        const int32_t d = rep_io_left_cycles - cycles;

        if ((d >= 0) && (d <= REP_IO_DISPATCH_MAX))
            *dispatch = d;
    }
    rep_io_left_at = 0xffffffff;
}

static __inline void
rep_io_leave(uint32_t cnt)
{
    rep_io_left_at     = cs + cpu_state.oldpc;
    rep_io_left_cnt    = cnt;
    rep_io_left_cycles = cycles;
}

/*How many words a block run of a REP INSW or OUTSW (io_string_get()) may
  move from here: no more than room (the page, the segment, a 16-bit
  register's wrap), than the pass's cycles leave (as cycles_end stops the
  word-by-word string), and none past the word whose end the next timer
  falls due in, after which the word-by-word string runs the timers. Each
  word costs io cycles and the next one dispatch more (rep_io_enter()); the
  TSC is brought up to date first.*/
static __inline uint32_t
rep_io_block_room(uint32_t room, int cycles_end, int32_t io, int32_t dispatch)
{
    const int64_t step = (int64_t) io + dispatch;
    int64_t       left;

    update_tsc();
    left = ((int64_t) cycles - io - cycles_end) / step + 1;
    if (left < 1)
        left = 1;
    if ((uint64_t) left < room)
        room = (uint32_t) left;
    if (TIMER_VAL_LESS_THAN_VAL(tsc, timer_target)) {
        const int64_t to_timer = (int64_t) (timer_target - (uint64_t) tsc);

        left = (to_timer + dispatch) / step + 1;
        if ((uint64_t) left < room)
            room = (uint32_t) left;
    } else
        room = 1;
    return room;
}

#define REP_OPS(size, CNT_REG, SRC_REG, DEST_REG)                                                                 \
    static int opREP_INSB_##size(UNUSED(uint32_t fetchdat))                                                       \
    {                                                                                                             \
        static int dispatch = 0;                                                                                  \
        int        cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);                               \
                                                                                                                  \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*One transfer with the trap flag set*/                                      \
        rep_io_enter(&dispatch, CNT_REG);                                                                         \
        addr64 = 0x00000000;                                                                                      \
                                                                                                                  \
        while (CNT_REG > 0) {                                                                                     \
            uint8_t temp;                                                                                         \
                                                                                                                  \
            check_io_perm(DX, 1);                                                                                 \
                                                                                                                  \
            SEG_CHECK_WRITE(&cpu_state.seg_es);                                                                   \
            CHECK_WRITE(&cpu_state.seg_es, DEST_REG, DEST_REG);                                                   \
            high_page = 0;                                                                                        \
            do_mmut_wb(es, DEST_REG, &addr64);                                                                    \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
                                                                                                                  \
            const uint32_t dr6 = dr[6];                                                                           \
            const int      tr  = trap;                                                                            \
            temp = inb(DX);                                                                                       \
            writememb_n(es, DEST_REG, addr64, temp);                                                              \
            if (cpu_state.abrt) {                                                                                 \
                /* The store faulted, so the instruction did not execute and its port                             \
                   read takes no I/O breakpoint trap (Intel 1999 vol. 3 p. 15-9). */                              \
                dr[6] = dr6;                                                                                      \
                trap  = tr;                                                                                       \
                return 1;                                                                                         \
            }                                                                                                     \
                                                                                                                  \
            if (cpu_state.flags & D_FLAG)                                                                         \
                DEST_REG--;                                                                                       \
            else                                                                                                  \
                DEST_REG++;                                                                                       \
            CNT_REG--;                                                                                            \
            cycles -= 15;                                                                                         \
            if (!CNT_REG || rep_io_stop(cycles_end))                                                              \
                break;                                                                                            \
            cycles -= dispatch;                                                                                   \
        }                                                                                                         \
        if (CNT_REG > 0) {                                                                                        \
            rep_io_leave(CNT_REG);                                                                                \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt || smi_line || io_op_trap();                                                        \
    }                                                                                                             \
    static int opREP_INSW_##size(UNUSED(uint32_t fetchdat))                                                       \
    {                                                                                                             \
        static int dispatch = 0;                                                                                  \
        void      *spriv    = NULL;                                                                               \
        io_insw_t  insw  = NULL;                                                                                  \
        int        cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);                               \
                                                                                                                  \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*One transfer with the trap flag set*/                                      \
        rep_io_enter(&dispatch, CNT_REG);                                                                         \
        /*Not while an event stops it at every word*/                                                             \
        if ((CNT_REG > 1) && !trap && !cpu_dr_armed && !(cpu_state.flags & D_FLAG))                               \
            insw = (io_insw_t) io_string_get(DX, 0, &spriv);                                                      \
        addr64a[0] = addr64a[1] = 0x00000000;                                                                     \
                                                                                                                  \
        while (CNT_REG > 0) {                                                                                     \
            if (insw != NULL) {                                                                                   \
                const uint32_t  addr = es + DEST_REG;                                                             \
                const uintptr_t wl   = writelookup2[addr >> 12];                                                  \
                                                                                                                  \
                if ((wl != (uintptr_t) LOOKUP_INV) && !(addr & 1) && (CNT_REG > 1) && (es != 0xffffffff)) {       \
                    const int32_t io   = IO_STR_CYCLES(DX, 15);                                                   \
                    uint32_t      room = (0x1000 - (addr & 0xfff)) >> 1;                                          \
                    uint32_t      n;                                                                              \
                                                                                                                  \
                    check_io_perm(DX, 2);                                                                         \
                    SEG_CHECK_WRITE(&cpu_state.seg_es);                                                           \
                    CHECK_WRITE(&cpu_state.seg_es, DEST_REG, DEST_REG + 1UL);                                     \
                    if (((uint64_t) cpu_state.seg_es.limit_high - DEST_REG + 1) / 2 < room)                       \
                        room = (uint32_t) (((uint64_t) cpu_state.seg_es.limit_high - DEST_REG + 1) / 2);          \
                    if ((sizeof(DEST_REG) == 2) && (((0x10000 - (uint32_t) DEST_REG) >> 1) < room))               \
                        room = (0x10000 - (uint32_t) DEST_REG) >> 1;                                              \
                    if (CNT_REG < room)                                                                           \
                        room = CNT_REG;                                                                           \
                    if (room > 1) {                                                                               \
                        room = rep_io_block_room(room, cycles_end, io, dispatch);                                 \
                        io_cycle = 1;                                                                             \
                        n    = insw(DX, (uint16_t *) (wl + (uintptr_t) addr), room, spriv);                       \
                        io_cycle = 0;                                                                             \
                        DEST_REG += n * 2;                                                                        \
                        CNT_REG -= n;                                                                             \
                        cycles -= (int) (n * io + (n - 1) * dispatch);                                            \
                        if (!CNT_REG || rep_io_stop(cycles_end))                                                  \
                            break;                                                                                \
                        cycles -= dispatch;                                                                       \
                        continue;                                                                                 \
                    }                                                                                             \
                }                                                                                                 \
            }                                                                                                     \
                                                                                                                  \
            uint16_t temp;                                                                                        \
                                                                                                                  \
            check_io_perm(DX, 2);                                                                                 \
                                                                                                                  \
            SEG_CHECK_WRITE(&cpu_state.seg_es);                                                                   \
            CHECK_WRITE(&cpu_state.seg_es, DEST_REG, DEST_REG + 1UL);                                             \
            high_page = 0;                                                                                        \
            do_mmut_ww(es, DEST_REG, addr64a);                                                                    \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
                                                                                                                  \
            const uint32_t dr6 = dr[6];                                                                           \
            const int      tr  = trap;                                                                            \
            temp = inw(DX);                                                                                       \
            writememw_n(es, DEST_REG, addr64a, temp);                                                             \
            if (cpu_state.abrt) {                                                                                 \
                /* The store faulted, so the instruction did not execute and its port                             \
                   read takes no I/O breakpoint trap (Intel 1999 vol. 3 p. 15-9). */                              \
                dr[6] = dr6;                                                                                      \
                trap  = tr;                                                                                       \
                return 1;                                                                                         \
            }                                                                                                     \
                                                                                                                  \
            if (cpu_state.flags & D_FLAG)                                                                         \
                DEST_REG -= 2;                                                                                    \
            else                                                                                                  \
                DEST_REG += 2;                                                                                    \
            CNT_REG--;                                                                                            \
            cycles -= IO_STR_CYCLES(DX, 15);                                                                      \
            if (!CNT_REG || rep_io_stop(cycles_end))                                                              \
                break;                                                                                            \
            cycles -= dispatch;                                                                                   \
        }                                                                                                         \
        if (CNT_REG > 0) {                                                                                        \
            rep_io_leave(CNT_REG);                                                                                \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt || smi_line || io_op_trap();                                                        \
    }                                                                                                             \
    static int opREP_INSL_##size(UNUSED(uint32_t fetchdat))                                                       \
    {                                                                                                             \
        static int dispatch = 0;                                                                                  \
        int        cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);                               \
                                                                                                                  \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*One transfer with the trap flag set*/                                      \
        rep_io_enter(&dispatch, CNT_REG);                                                                         \
        addr64a[0] = addr64a[1] = addr64a[2] = addr64a[3] = 0x00000000;                                           \
                                                                                                                  \
        while (CNT_REG > 0) {                                                                                     \
            uint32_t temp;                                                                                        \
                                                                                                                  \
            check_io_perm(DX, 4);                                                                                 \
                                                                                                                  \
            SEG_CHECK_WRITE(&cpu_state.seg_es);                                                                   \
            CHECK_WRITE(&cpu_state.seg_es, DEST_REG, DEST_REG + 3UL);                                             \
            high_page = 0;                                                                                        \
            do_mmut_wl(es, DEST_REG, addr64a);                                                                    \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
                                                                                                                  \
            const uint32_t dr6 = dr[6];                                                                           \
            const int      tr  = trap;                                                                            \
            temp = inl(DX);                                                                                       \
            writememl_n(es, DEST_REG, addr64a, temp);                                                             \
            if (cpu_state.abrt) {                                                                                 \
                /* The store faulted, so the instruction did not execute and its port                             \
                   read takes no I/O breakpoint trap (Intel 1999 vol. 3 p. 15-9). */                              \
                dr[6] = dr6;                                                                                      \
                trap  = tr;                                                                                       \
                return 1;                                                                                         \
            }                                                                                                     \
                                                                                                                  \
            if (cpu_state.flags & D_FLAG)                                                                         \
                DEST_REG -= 4;                                                                                    \
            else                                                                                                  \
                DEST_REG += 4;                                                                                    \
            CNT_REG--;                                                                                            \
            cycles -= IO_STR_CYCLES(DX, 15);                                                                      \
            if (!CNT_REG || rep_io_stop(cycles_end))                                                              \
                break;                                                                                            \
            cycles -= dispatch;                                                                                   \
        }                                                                                                         \
        if (CNT_REG > 0) {                                                                                        \
            rep_io_leave(CNT_REG);                                                                                \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt || smi_line || io_op_trap();                                                        \
    }                                                                                                             \
                                                                                                                  \
    static int opREP_OUTSB_##size(UNUSED(uint32_t fetchdat))                                                      \
    {                                                                                                             \
        static int dispatch = 0;                                                                                  \
        int        cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);                               \
                                                                                                                  \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*One transfer with the trap flag set*/                                      \
        rep_io_enter(&dispatch, CNT_REG);                                                                         \
        addr64 = 0x00000000;                                                                                      \
                                                                                                                  \
        while (CNT_REG > 0) {                                                                                     \
            uint8_t temp;                                                                                         \
                                                                                                                  \
            SEG_CHECK_READ(cpu_state.ea_seg);                                                                     \
            CHECK_READ(cpu_state.ea_seg, SRC_REG, SRC_REG);                                                       \
            high_page = 0;                                                                                        \
            do_mmut_rb(cpu_state.ea_seg->base, SRC_REG, &addr64);                                                 \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
            check_io_perm(DX, 1);                                                                                 \
                                                                                                                  \
            temp = readmemb_n(cpu_state.ea_seg->base, SRC_REG, addr64);                                           \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
            outb(DX, temp);                                                                                       \
                                                                                                                  \
            if (cpu_state.flags & D_FLAG)                                                                         \
                SRC_REG--;                                                                                        \
            else                                                                                                  \
                SRC_REG++;                                                                                        \
            CNT_REG--;                                                                                            \
            cycles -= 14;                                                                                         \
            if (!CNT_REG || rep_io_stop(cycles_end))                                                              \
                break;                                                                                            \
            cycles -= dispatch;                                                                                   \
        }                                                                                                         \
        if (CNT_REG > 0) {                                                                                        \
            rep_io_leave(CNT_REG);                                                                                \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt || smi_line || io_op_trap();                                                        \
    }                                                                                                             \
    static int opREP_OUTSW_##size(UNUSED(uint32_t fetchdat))                                                      \
    {                                                                                                             \
        static int dispatch = 0;                                                                                  \
        void      *spriv    = NULL;                                                                               \
        io_outsw_t outsw = NULL;                                                                                  \
        int        cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);                               \
                                                                                                                  \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*One transfer with the trap flag set*/                                      \
        rep_io_enter(&dispatch, CNT_REG);                                                                         \
        /*Not while an event stops it at every word*/                                                             \
        if ((CNT_REG > 1) && !trap && !cpu_dr_armed && !(cpu_state.flags & D_FLAG))                               \
            outsw = (io_outsw_t) io_string_get(DX, 1, &spriv);                                                    \
        addr64a[0] = addr64a[1] = 0x00000000;                                                                     \
                                                                                                                  \
        while (CNT_REG > 0) {                                                                                     \
            if (outsw != NULL) {                                                                                  \
                const uint32_t  addr = cpu_state.ea_seg->base + SRC_REG;                                          \
                const uintptr_t rl   = readlookup2[addr >> 12];                                                   \
                                                                                                                  \
                if ((rl != (uintptr_t) LOOKUP_INV) && !(addr & 1) && (CNT_REG > 1) && (cpu_state.ea_seg->base != 0xffffffff)) { \
                    const int32_t io   = IO_STR_CYCLES(DX, 14);                                                   \
                    uint32_t      room = (0x1000 - (addr & 0xfff)) >> 1;                                          \
                    uint32_t      n;                                                                              \
                                                                                                                  \
                    SEG_CHECK_READ(cpu_state.ea_seg);                                                             \
                    CHECK_READ(cpu_state.ea_seg, SRC_REG, SRC_REG + 1UL);                                         \
                    check_io_perm(DX, 2);                                                                         \
                    if (((uint64_t) cpu_state.ea_seg->limit_high - SRC_REG + 1) / 2 < room)                       \
                        room = (uint32_t) (((uint64_t) cpu_state.ea_seg->limit_high - SRC_REG + 1) / 2);          \
                    if ((sizeof(SRC_REG) == 2) && (((0x10000 - (uint32_t) SRC_REG) >> 1) < room))                 \
                        room = (0x10000 - (uint32_t) SRC_REG) >> 1;                                               \
                    if (CNT_REG < room)                                                                           \
                        room = CNT_REG;                                                                           \
                    if (room > 1) {                                                                               \
                        room = rep_io_block_room(room, cycles_end, io, dispatch);                                 \
                        io_cycle = 1;                                                                             \
                        n    = outsw(DX, (const uint16_t *) (rl + (uintptr_t) addr), room, spriv);                \
                        io_cycle = 0;                                                                             \
                        SRC_REG += n * 2;                                                                         \
                        CNT_REG -= n;                                                                             \
                        cycles -= (int) (n * io + (n - 1) * dispatch);                                            \
                        if (!CNT_REG || rep_io_stop(cycles_end))                                                  \
                            break;                                                                                \
                        cycles -= dispatch;                                                                       \
                        continue;                                                                                 \
                    }                                                                                             \
                }                                                                                                 \
            }                                                                                                     \
                                                                                                                  \
            uint16_t temp;                                                                                        \
                                                                                                                  \
            SEG_CHECK_READ(cpu_state.ea_seg);                                                                     \
            CHECK_READ(cpu_state.ea_seg, SRC_REG, SRC_REG + 1UL);                                                 \
            high_page = 0;                                                                                        \
            do_mmut_rw(cpu_state.ea_seg->base, SRC_REG, addr64a);                                                 \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
            check_io_perm(DX, 2);                                                                                 \
                                                                                                                  \
            temp = readmemw_n(cpu_state.ea_seg->base, SRC_REG, addr64a);                                          \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
            outw(DX, temp);                                                                                       \
                                                                                                                  \
            if (cpu_state.flags & D_FLAG)                                                                         \
                SRC_REG -= 2;                                                                                     \
            else                                                                                                  \
                SRC_REG += 2;                                                                                     \
            CNT_REG--;                                                                                            \
            cycles -= IO_STR_CYCLES(DX, 14);                                                                      \
            if (!CNT_REG || rep_io_stop(cycles_end))                                                              \
                break;                                                                                            \
            cycles -= dispatch;                                                                                   \
        }                                                                                                         \
        if (CNT_REG > 0) {                                                                                        \
            rep_io_leave(CNT_REG);                                                                                \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt || smi_line || io_op_trap();                                                        \
    }                                                                                                             \
    static int opREP_OUTSL_##size(UNUSED(uint32_t fetchdat))                                                      \
    {                                                                                                             \
        static int dispatch = 0;                                                                                  \
        int        cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);                               \
                                                                                                                  \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*One transfer with the trap flag set*/                                      \
        rep_io_enter(&dispatch, CNT_REG);                                                                         \
        addr64a[0] = addr64a[1] = addr64a[2] = addr64a[3] = 0x00000000;                                           \
                                                                                                                  \
        while (CNT_REG > 0) {                                                                                     \
            uint32_t temp;                                                                                        \
                                                                                                                  \
            SEG_CHECK_READ(cpu_state.ea_seg);                                                                     \
            CHECK_READ(cpu_state.ea_seg, SRC_REG, SRC_REG + 3UL);                                                 \
            high_page = 0;                                                                                        \
            do_mmut_rl(cpu_state.ea_seg->base, SRC_REG, addr64a);                                                 \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
            check_io_perm(DX, 4);                                                                                 \
                                                                                                                  \
            temp = readmeml_n(cpu_state.ea_seg->base, SRC_REG, addr64a);                                          \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
            outl(DX, temp);                                                                                       \
                                                                                                                  \
            if (cpu_state.flags & D_FLAG)                                                                         \
                SRC_REG -= 4;                                                                                     \
            else                                                                                                  \
                SRC_REG += 4;                                                                                     \
            CNT_REG--;                                                                                            \
            cycles -= IO_STR_CYCLES(DX, 14);                                                                      \
            if (!CNT_REG || rep_io_stop(cycles_end))                                                              \
                break;                                                                                            \
            cycles -= dispatch;                                                                                   \
        }                                                                                                         \
        if (CNT_REG > 0) {                                                                                        \
            rep_io_leave(CNT_REG);                                                                                \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt || smi_line || io_op_trap();                                                        \
    }                                                                                                             \
                                                                                                                  \
    static int opREP_MOVSB_##size(UNUSED(uint32_t fetchdat))                                                      \
    {                                                                                                             \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);                                      \
        addr64 = addr64_2 = 0x00000000;                                                                           \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*Force the instruction to end after only one iteration when trap flag set*/ \
        while (CNT_REG > 0) {                                                                                     \
            uint8_t temp;                                                                                         \
                                                                                                                  \
            SEG_CHECK_READ_REP(cpu_state.ea_seg);                                                                 \
            CHECK_READ_REP(cpu_state.ea_seg, SRC_REG, SRC_REG);                                                   \
            high_page = 0;                                                                                        \
            do_mmut_rb(cpu_state.ea_seg->base, SRC_REG, &addr64);                                                 \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            SEG_CHECK_WRITE_REP(&cpu_state.seg_es);                                                               \
            CHECK_WRITE_REP(&cpu_state.seg_es, DEST_REG, DEST_REG);                                               \
            high_page = 0;                                                                                        \
            do_mmut_wb(es, DEST_REG, &addr64_2);                                                                  \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            temp = readmemb_n(cpu_state.ea_seg->base, SRC_REG, addr64);                                           \
            writememb_n(es, DEST_REG, addr64_2, temp);                                                            \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            if (cpu_state.flags & D_FLAG) {                                                                       \
                DEST_REG--;                                                                                       \
                SRC_REG--;                                                                                        \
            } else {                                                                                              \
                DEST_REG++;                                                                                       \
                SRC_REG++;                                                                                        \
            }                                                                                                     \
            CNT_REG--;                                                                                            \
            cycles -= is486 ? 3 : 4;                                                                              \
            if ((cycles < cycles_end) || smi_line)                                                                \
                break;                                                                                            \
        }                                                                                                         \
        if (CNT_REG > 0) {                                                                                        \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt || smi_line;                                                                        \
    }                                                                                                             \
    static int opREP_MOVSW_##size(UNUSED(uint32_t fetchdat))                                                      \
    {                                                                                                             \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);                                      \
        addr64a[0] = addr64a[1] = 0x00000000;                                                                     \
        addr64a_2[0] = addr64a_2[1] = 0x00000000;                                                                 \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*Force the instruction to end after only one iteration when trap flag set*/ \
        while (CNT_REG > 0) {                                                                                     \
            uint16_t temp;                                                                                        \
                                                                                                                  \
            SEG_CHECK_READ_REP(cpu_state.ea_seg);                                                                 \
            CHECK_READ_REP(cpu_state.ea_seg, SRC_REG, SRC_REG + 1UL);                                             \
            high_page = 0;                                                                                        \
            do_mmut_rw(cpu_state.ea_seg->base, SRC_REG, addr64a);                                                 \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            SEG_CHECK_WRITE_REP(&cpu_state.seg_es);                                                               \
            CHECK_WRITE_REP(&cpu_state.seg_es, DEST_REG, DEST_REG + 1UL);                                         \
            high_page = 0;                                                                                        \
            do_mmut_ww(es, DEST_REG, addr64a_2);                                                                  \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            temp = readmemw_n(cpu_state.ea_seg->base, SRC_REG, addr64a);                                          \
            writememw_n(es, DEST_REG, addr64a_2, temp);                                                           \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            if (cpu_state.flags & D_FLAG) {                                                                       \
                DEST_REG -= 2;                                                                                    \
                SRC_REG -= 2;                                                                                     \
            } else {                                                                                              \
                DEST_REG += 2;                                                                                    \
                SRC_REG += 2;                                                                                     \
            }                                                                                                     \
            CNT_REG--;                                                                                            \
            cycles -= is486 ? 3 : 4;                                                                              \
            if ((cycles < cycles_end) || smi_line)                                                                \
                break;                                                                                            \
        }                                                                                                         \
        if (CNT_REG > 0) {                                                                                        \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt || smi_line;                                                                        \
    }                                                                                                             \
    static int opREP_MOVSL_##size(UNUSED(uint32_t fetchdat))                                                      \
    {                                                                                                             \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);                                      \
        addr64a[0] = addr64a[1] = addr64a[2] = addr64a[3] = 0x00000000;                                           \
        addr64a_2[0] = addr64a_2[1] = addr64a_2[2] = addr64a_2[3] = 0x00000000;                                   \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*Force the instruction to end after only one iteration when trap flag set*/ \
        while (CNT_REG > 0) {                                                                                     \
            uint32_t temp;                                                                                        \
                                                                                                                  \
            SEG_CHECK_READ_REP(cpu_state.ea_seg);                                                                 \
            CHECK_READ_REP(cpu_state.ea_seg, SRC_REG, SRC_REG + 3UL);                                             \
            high_page = 0;                                                                                        \
            do_mmut_rl(cpu_state.ea_seg->base, SRC_REG, addr64a);                                                 \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            SEG_CHECK_WRITE_REP(&cpu_state.seg_es);                                                               \
            CHECK_WRITE_REP(&cpu_state.seg_es, DEST_REG, DEST_REG + 3UL);                                         \
            high_page = 0;                                                                                        \
            do_mmut_wl(es, DEST_REG, addr64a_2);                                                                  \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            temp = readmeml_n(cpu_state.ea_seg->base, SRC_REG, addr64a);                                          \
            writememl_n(es, DEST_REG, addr64a_2, temp);                                                           \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            if (cpu_state.flags & D_FLAG) {                                                                       \
                DEST_REG -= 4;                                                                                    \
                SRC_REG -= 4;                                                                                     \
            } else {                                                                                              \
                DEST_REG += 4;                                                                                    \
                SRC_REG += 4;                                                                                     \
            }                                                                                                     \
            CNT_REG--;                                                                                            \
            cycles -= is486 ? 3 : 4;                                                                              \
            if ((cycles < cycles_end) || smi_line)                                                                \
                break;                                                                                            \
        }                                                                                                         \
        if (CNT_REG > 0) {                                                                                        \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt || smi_line;                                                                        \
    }                                                                                                             \
                                                                                                                  \
    static int opREP_STOSB_##size(UNUSED(uint32_t fetchdat))                                                      \
    {                                                                                                             \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);                                      \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*Force the instruction to end after only one iteration when trap flag set*/ \
        while (CNT_REG > 0) {                                                                                     \
                                                                                                                  \
            SEG_CHECK_WRITE_REP(&cpu_state.seg_es);                                                               \
            CHECK_WRITE_REP(&cpu_state.seg_es, DEST_REG, DEST_REG);                                               \
            writememb(es, DEST_REG, AL);                                                                          \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            if (cpu_state.flags & D_FLAG)                                                                         \
                DEST_REG--;                                                                                       \
            else                                                                                                  \
                DEST_REG++;                                                                                       \
            CNT_REG--;                                                                                            \
            cycles -= is486 ? 4 : 5;                                                                              \
            if ((cycles < cycles_end) || smi_line)                                                                \
                break;                                                                                            \
        }                                                                                                         \
        if (CNT_REG > 0) {                                                                                        \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt || smi_line;                                                                        \
    }                                                                                                             \
    static int opREP_STOSW_##size(UNUSED(uint32_t fetchdat))                                                      \
    {                                                                                                             \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);                                      \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*Force the instruction to end after only one iteration when trap flag set*/ \
        while (CNT_REG > 0) {                                                                                     \
                                                                                                                  \
            SEG_CHECK_WRITE_REP(&cpu_state.seg_es);                                                               \
            CHECK_WRITE_REP(&cpu_state.seg_es, DEST_REG, DEST_REG + 1UL);                                         \
            writememw(es, DEST_REG, AX);                                                                          \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            if (cpu_state.flags & D_FLAG)                                                                         \
                DEST_REG -= 2;                                                                                    \
            else                                                                                                  \
                DEST_REG += 2;                                                                                    \
            CNT_REG--;                                                                                            \
            cycles -= is486 ? 4 : 5;                                                                              \
            if ((cycles < cycles_end) || smi_line)                                                                \
                break;                                                                                            \
        }                                                                                                         \
        if (CNT_REG > 0) {                                                                                        \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt || smi_line;                                                                        \
    }                                                                                                             \
    static int opREP_STOSL_##size(UNUSED(uint32_t fetchdat))                                                      \
    {                                                                                                             \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);                                      \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*Force the instruction to end after only one iteration when trap flag set*/ \
        while (CNT_REG > 0) {                                                                                     \
                                                                                                                  \
            SEG_CHECK_WRITE_REP(&cpu_state.seg_es);                                                               \
            CHECK_WRITE_REP(&cpu_state.seg_es, DEST_REG, DEST_REG + 3UL);                                         \
            writememl(es, DEST_REG, EAX);                                                                         \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            if (cpu_state.flags & D_FLAG)                                                                         \
                DEST_REG -= 4;                                                                                    \
            else                                                                                                  \
                DEST_REG += 4;                                                                                    \
            CNT_REG--;                                                                                            \
            cycles -= is486 ? 4 : 5;                                                                              \
            if ((cycles < cycles_end) || smi_line)                                                                \
                break;                                                                                            \
        }                                                                                                         \
        if (CNT_REG > 0) {                                                                                        \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt || smi_line;                                                                        \
    }                                                                                                             \
                                                                                                                  \
    static int opREP_LODSB_##size(UNUSED(uint32_t fetchdat))                                                      \
    {                                                                                                             \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);                                      \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*Force the instruction to end after only one iteration when trap flag set*/ \
        while (CNT_REG > 0) {                                                                                     \
            SEG_CHECK_READ_REP(cpu_state.ea_seg);                                                                 \
            CHECK_READ_REP(cpu_state.ea_seg, SRC_REG, SRC_REG);                                                   \
            uint8_t new_AL = readmemb(cpu_state.ea_seg->base, SRC_REG);                                           \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            AL = new_AL;                                                                                          \
                                                                                                                  \
            if (cpu_state.flags & D_FLAG)                                                                         \
                SRC_REG--;                                                                                        \
            else                                                                                                  \
                SRC_REG++;                                                                                        \
            CNT_REG--;                                                                                            \
            cycles -= is486 ? 4 : 5;                                                                              \
            if ((cycles < cycles_end) || smi_line)                                                                \
                break;                                                                                            \
        }                                                                                                         \
        if (CNT_REG > 0) {                                                                                        \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt || smi_line;                                                                        \
    }                                                                                                             \
    static int opREP_LODSW_##size(UNUSED(uint32_t fetchdat))                                                      \
    {                                                                                                             \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);                                      \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*Force the instruction to end after only one iteration when trap flag set*/ \
        while (CNT_REG > 0) {                                                                                     \
            SEG_CHECK_READ_REP(cpu_state.ea_seg);                                                                 \
            CHECK_READ_REP(cpu_state.ea_seg, SRC_REG, SRC_REG + 1UL);                                             \
            uint16_t new_AX = readmemw(cpu_state.ea_seg->base, SRC_REG);                                          \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            AX = new_AX;                                                                                          \
                                                                                                                  \
            if (cpu_state.flags & D_FLAG)                                                                         \
                SRC_REG -= 2;                                                                                     \
            else                                                                                                  \
                SRC_REG += 2;                                                                                     \
            CNT_REG--;                                                                                            \
            cycles -= is486 ? 4 : 5;                                                                              \
            if ((cycles < cycles_end) || smi_line)                                                                \
                break;                                                                                            \
        }                                                                                                         \
        if (CNT_REG > 0) {                                                                                        \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt || smi_line;                                                                        \
    }                                                                                                             \
    static int opREP_LODSL_##size(UNUSED(uint32_t fetchdat))                                                      \
    {                                                                                                             \
        int cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);                                      \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*Force the instruction to end after only one iteration when trap flag set*/ \
        while (CNT_REG > 0) {                                                                                     \
            SEG_CHECK_READ_REP(cpu_state.ea_seg);                                                                 \
            CHECK_READ_REP(cpu_state.ea_seg, SRC_REG, SRC_REG + 3UL);                                             \
            uint32_t new_EAX = readmeml(cpu_state.ea_seg->base, SRC_REG);                                         \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            EAX = new_EAX;                                                                                        \
                                                                                                                  \
            if (cpu_state.flags & D_FLAG)                                                                         \
                SRC_REG -= 4;                                                                                     \
            else                                                                                                  \
                SRC_REG += 4;                                                                                     \
            CNT_REG--;                                                                                            \
            cycles -= is486 ? 4 : 5;                                                                              \
            if ((cycles < cycles_end) || smi_line)                                                                \
                break;                                                                                            \
        }                                                                                                         \
        if (CNT_REG > 0) {                                                                                        \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt || smi_line;                                                                        \
    }

#define REP_OPS_CMPS_SCAS(size, CNT_REG, SRC_REG, DEST_REG, FV)                                                   \
    static int opREP_CMPSB_##size(UNUSED(uint32_t fetchdat))                                                      \
    {                                                                                                             \
        int tempz;                                                                                                \
        addr64 = addr64_2 = 0x00000000;                                                                           \
                                                                                                                  \
        tempz = FV;                                                                                               \
        if ((CNT_REG > 0) && (FV == tempz)) {                                                                     \
            uint8_t temp, temp2;                                                                                  \
                                                                                                                  \
            SEG_CHECK_READ(&cpu_state.seg_es);                                                                    \
            CHECK_READ(&cpu_state.seg_es, DEST_REG, DEST_REG);                                                    \
            high_page = 0;                                                                                        \
            do_mmut_rb(es, DEST_REG, &addr64);                                                                    \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
                                                                                                                  \
            SEG_CHECK_READ(cpu_state.ea_seg);                                                                     \
            CHECK_READ(cpu_state.ea_seg, SRC_REG, SRC_REG);                                                       \
            high_page = 0;                                                                                        \
            do_mmut_rb2(cpu_state.ea_seg->base, SRC_REG, &addr64_2);                                              \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
                                                                                                                  \
            temp = readmemb_n(es, DEST_REG, addr64);                                                              \
            is_compare = 1;                                                                                       \
            temp2 = readmemb_n2(cpu_state.ea_seg->base, SRC_REG, addr64_2);                                       \
            is_compare = 0;                                                                                       \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
                                                                                                                  \
            if (cpu_state.flags & D_FLAG) {                                                                       \
                DEST_REG--;                                                                                       \
                SRC_REG--;                                                                                        \
            } else {                                                                                              \
                DEST_REG++;                                                                                       \
                SRC_REG++;                                                                                        \
            }                                                                                                     \
            CNT_REG--;                                                                                            \
            cycles -= is486 ? 7 : 9;                                                                              \
            setsub8(temp2, temp);                                                                                 \
            tempz = (ZF_SET()) ? 1 : 0;                                                                           \
        }                                                                                                         \
        if ((CNT_REG > 0) && (FV == tempz)) {                                                                     \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt || smi_line;                                                                        \
    }                                                                                                             \
    static int opREP_CMPSW_##size(UNUSED(uint32_t fetchdat))                                                      \
    {                                                                                                             \
        int tempz;                                                                                                \
        addr64a[0] = addr64a[1] = 0x00000000;                                                                     \
        addr64a_2[0] = addr64a_2[1] = 0x00000000;                                                                 \
                                                                                                                  \
        tempz = FV;                                                                                               \
        if ((CNT_REG > 0) && (FV == tempz)) {                                                                     \
            uint16_t temp, temp2;                                                                                 \
                                                                                                                  \
            SEG_CHECK_READ(&cpu_state.seg_es);                                                                    \
            CHECK_READ(&cpu_state.seg_es, DEST_REG, DEST_REG + 1UL);                                              \
            high_page = 0;                                                                                        \
            do_mmut_rw(es, DEST_REG, addr64a);                                                                    \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
                                                                                                                  \
            SEG_CHECK_READ(cpu_state.ea_seg);                                                                     \
            CHECK_READ(cpu_state.ea_seg, SRC_REG, SRC_REG + 1UL);                                                 \
            high_page = 0;                                                                                        \
            do_mmut_rw2(cpu_state.ea_seg->base, SRC_REG, addr64a_2);                                              \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
                                                                                                                  \
            temp = readmemw_n(es, DEST_REG, addr64a);                                                             \
            is_compare = 1;                                                                                       \
            temp2 = readmemw_n2(cpu_state.ea_seg->base, SRC_REG, addr64a_2);                                      \
            is_compare = 0;                                                                                       \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
                                                                                                                  \
            if (cpu_state.flags & D_FLAG) {                                                                       \
                DEST_REG -= 2;                                                                                    \
                SRC_REG -= 2;                                                                                     \
            } else {                                                                                              \
                DEST_REG += 2;                                                                                    \
                SRC_REG += 2;                                                                                     \
            }                                                                                                     \
            CNT_REG--;                                                                                            \
            cycles -= is486 ? 7 : 9;                                                                              \
            setsub16(temp2, temp);                                                                                \
            tempz = (ZF_SET()) ? 1 : 0;                                                                           \
        }                                                                                                         \
        if ((CNT_REG > 0) && (FV == tempz)) {                                                                     \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt || smi_line;                                                                        \
    }                                                                                                             \
    static int opREP_CMPSL_##size(UNUSED(uint32_t fetchdat))                                                      \
    {                                                                                                             \
        int tempz;                                                                                                \
        addr64a[0] = addr64a[1] = addr64a[2] = addr64a[3] = 0x00000000;                                           \
        addr64a_2[0] = addr64a_2[1] = addr64a_2[2] = addr64a_2[3] = 0x00000000;                                   \
                                                                                                                  \
        tempz = FV;                                                                                               \
        if ((CNT_REG > 0) && (FV == tempz)) {                                                                     \
            uint32_t temp, temp2;                                                                                 \
                                                                                                                  \
            SEG_CHECK_READ(&cpu_state.seg_es);                                                                    \
            CHECK_READ(&cpu_state.seg_es, DEST_REG, DEST_REG + 3UL);                                              \
            high_page = 0;                                                                                        \
            do_mmut_rl(es, DEST_REG, addr64a);                                                                    \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
                                                                                                                  \
            SEG_CHECK_READ(cpu_state.ea_seg);                                                                     \
            CHECK_READ(cpu_state.ea_seg, SRC_REG, SRC_REG + 3UL);                                                 \
            high_page = 0;                                                                                        \
            do_mmut_rl2(cpu_state.ea_seg->base, SRC_REG, addr64a_2);                                              \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
                                                                                                                  \
            temp = readmeml_n(es, DEST_REG, addr64a);                                                             \
            is_compare = 1;                                                                                       \
            temp2 = readmeml_n2(cpu_state.ea_seg->base, SRC_REG, addr64a_2);                                      \
            is_compare = 0;                                                                                       \
            if (cpu_state.abrt)                                                                                   \
                return 1;                                                                                         \
                                                                                                                  \
            if (cpu_state.flags & D_FLAG) {                                                                       \
                DEST_REG -= 4;                                                                                    \
                SRC_REG -= 4;                                                                                     \
            } else {                                                                                              \
                DEST_REG += 4;                                                                                    \
                SRC_REG += 4;                                                                                     \
            }                                                                                                     \
            CNT_REG--;                                                                                            \
            cycles -= is486 ? 7 : 9;                                                                              \
            setsub32(temp2, temp);                                                                                \
            tempz = (ZF_SET()) ? 1 : 0;                                                                           \
        }                                                                                                         \
        if ((CNT_REG > 0) && (FV == tempz)) {                                                                     \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt || smi_line;                                                                        \
    }                                                                                                             \
                                                                                                                  \
    static int opREP_SCASB_##size(UNUSED(uint32_t fetchdat))                                                      \
    {                                                                                                             \
        int tempz;                                                                                                \
        int cycles_end = cycles - 1000;                                                                           \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*Force the instruction to end after only one iteration when trap flag set*/ \
        tempz = FV;                                                                                               \
        while ((CNT_REG > 0) && (FV == tempz)) {                                                                  \
            SEG_CHECK_READ_REP(&cpu_state.seg_es);                                                                \
            CHECK_READ_REP(&cpu_state.seg_es, DEST_REG, DEST_REG);                                                \
            uint8_t temp = readmemb(es, DEST_REG);                                                                \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            setsub8(AL, temp);                                                                                    \
            tempz = (ZF_SET()) ? 1 : 0;                                                                           \
            if (cpu_state.flags & D_FLAG)                                                                         \
                DEST_REG--;                                                                                       \
            else                                                                                                  \
                DEST_REG++;                                                                                       \
            CNT_REG--;                                                                                            \
            cycles -= is486 ? 5 : 8;                                                                              \
            if ((cycles < cycles_end) || smi_line)                                                                \
                break;                                                                                            \
        }                                                                                                         \
        if ((CNT_REG > 0) && (FV == tempz)) {                                                                     \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt || smi_line;                                                                        \
    }                                                                                                             \
    static int opREP_SCASW_##size(UNUSED(uint32_t fetchdat))                                                      \
    {                                                                                                             \
        int tempz;                                                                                                \
        int cycles_end = cycles - 1000;                                                                           \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*Force the instruction to end after only one iteration when trap flag set*/ \
        tempz = FV;                                                                                               \
        while ((CNT_REG > 0) && (FV == tempz)) {                                                                  \
            SEG_CHECK_READ_REP(&cpu_state.seg_es);                                                                \
            CHECK_READ_REP(&cpu_state.seg_es, DEST_REG, DEST_REG + 1UL);                                          \
            uint16_t temp = readmemw(es, DEST_REG);                                                               \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            setsub16(AX, temp);                                                                                   \
            tempz = (ZF_SET()) ? 1 : 0;                                                                           \
            if (cpu_state.flags & D_FLAG)                                                                         \
                DEST_REG -= 2;                                                                                    \
            else                                                                                                  \
                DEST_REG += 2;                                                                                    \
            CNT_REG--;                                                                                            \
            cycles -= is486 ? 5 : 8;                                                                              \
            if ((cycles < cycles_end) || smi_line)                                                                \
                break;                                                                                            \
        }                                                                                                         \
        if ((CNT_REG > 0) && (FV == tempz)) {                                                                     \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt || smi_line;                                                                        \
    }                                                                                                             \
    static int opREP_SCASL_##size(UNUSED(uint32_t fetchdat))                                                      \
    {                                                                                                             \
        int tempz;                                                                                                \
        int cycles_end = cycles - 1000;                                                                           \
        if (trap)                                                                                                 \
            cycles_end = cycles + 1; /*Force the instruction to end after only one iteration when trap flag set*/ \
        tempz = FV;                                                                                               \
        while ((CNT_REG > 0) && (FV == tempz)) {                                                                  \
            SEG_CHECK_READ_REP(&cpu_state.seg_es);                                                                \
            CHECK_READ_REP(&cpu_state.seg_es, DEST_REG, DEST_REG + 3UL);                                          \
            uint32_t temp = readmeml(es, DEST_REG);                                                               \
            if (cpu_state.abrt)                                                                                   \
                break;                                                                                            \
                                                                                                                  \
            setsub32(EAX, temp);                                                                                  \
            tempz = (ZF_SET()) ? 1 : 0;                                                                           \
            if (cpu_state.flags & D_FLAG)                                                                         \
                DEST_REG -= 4;                                                                                    \
            else                                                                                                  \
                DEST_REG += 4;                                                                                    \
            CNT_REG--;                                                                                            \
            cycles -= is486 ? 5 : 8;                                                                              \
            if ((cycles < cycles_end) || smi_line)                                                                \
                break;                                                                                            \
        }                                                                                                         \
        if ((CNT_REG > 0) && (FV == tempz)) {                                                                     \
            CPU_BLOCK_END();                                                                                      \
            cpu_state.pc = cpu_state.oldpc;                                                                       \
            return 1;                                                                                             \
        }                                                                                                         \
        return cpu_state.abrt || smi_line;                                                                        \
    }

REP_OPS(a16, CX, SI, DI)
REP_OPS(a32, ECX, ESI, EDI)
REP_OPS_CMPS_SCAS(a16_NE, CX, SI, DI, 0)
REP_OPS_CMPS_SCAS(a16_E, CX, SI, DI, 1)
REP_OPS_CMPS_SCAS(a32_NE, ECX, ESI, EDI, 0)
REP_OPS_CMPS_SCAS(a32_E, ECX, ESI, EDI, 1)

static int
opREPNE(uint32_t fetchdat)
{
    fetchdat = fastreadl(cs + cpu_state.pc);
    if (cpu_state.abrt)
        return 1;
    cpu_state.pc++;

    CLOCK_CYCLES(2);
    rep_op = fetchdat & 0xff;
    if (x86_opcodes_REPNE[(fetchdat & 0xff) | cpu_state.op32])
        return x86_opcodes_REPNE[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
    return x86_opcodes[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
}
static int
opREPE(uint32_t fetchdat)
{
    fetchdat = fastreadl(cs + cpu_state.pc);
    if (cpu_state.abrt)
        return 1;
    cpu_state.pc++;

    CLOCK_CYCLES(2);
    rep_op = fetchdat & 0xff;
    if (x86_opcodes_REPE[(fetchdat & 0xff) | cpu_state.op32])
        return x86_opcodes_REPE[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
    return x86_opcodes[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
}
