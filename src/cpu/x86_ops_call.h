#ifdef USE_NEW_DYNAREC
#define CALL_FAR_w(new_seg, new_pc)                                             \
        old_cs = CS;                                                            \
        old_pc = cpu_state.pc;                                                  \
        cpu_state.pc = new_pc;                                                  \
        optype = CALL;                                                          \
        cgate16 = cgate32 = 0;                                                  \
        if (msw & 1) loadcscall(new_seg, old_pc);                               \
        else                                                                    \
        {                                                                       \
                loadcs(new_seg);                                                \
                cycles -= timing_call_rm;                                       \
        }                                                                       \
        optype = 0;                                                             \
        if (cpu_state.abrt) { cgate16 = cgate32 = 0; return 1; }                          \
        oldss = ss;                                                             \
        if (cgate32)                                                            \
        {                                                                       \
                uint32_t old_esp = ESP;                                         \
                PUSH_L(old_cs);                         if (cpu_state.abrt) { CS = old_cs; cgate16 = cgate32 = 0; return 1; }     \
                PUSH_L(old_pc);                         if (cpu_state.abrt) { CS = old_cs; ESP = old_esp; return 1; } \
        }                                                                       \
        else                                                                    \
        {                                                                       \
                uint32_t old_esp = ESP;                                         \
                PUSH_W(old_cs);                         if (cpu_state.abrt) { CS = old_cs; cgate16 = cgate32 = 0; return 1; }     \
                PUSH_W(old_pc);                         if (cpu_state.abrt) { CS = old_cs; ESP = old_esp; return 1; } \
        }

#define CALL_FAR_l(new_seg, new_pc)                                             \
        old_cs = CS;                                                            \
        old_pc = cpu_state.pc;                                                  \
        cpu_state.pc = new_pc;                                                  \
        optype = CALL;                                                          \
        cgate16 = cgate32 = 0;                                                  \
        if (msw & 1) loadcscall(new_seg, old_pc);                               \
        else                                                                    \
        {                                                                       \
                loadcs(new_seg);                                                \
                cycles -= timing_call_rm;                                       \
        }                                                                       \
        optype = 0;                                                             \
        if (cpu_state.abrt) { cgate16 = cgate32 = 0; return 1; }                          \
        oldss = ss;                                                             \
        if (cgate16)                                                            \
        {                                                                       \
                uint32_t old_esp = ESP;                                         \
                PUSH_W(old_cs);                         if (cpu_state.abrt) { CS = old_cs; cgate16 = cgate32 = 0; return 1; }     \
                PUSH_W(old_pc);                         if (cpu_state.abrt) { CS = old_cs; ESP = old_esp; return 1; } \
        }                                                                       \
        else                                                                    \
        {                                                                       \
                uint32_t old_esp = ESP;                                         \
                PUSH_L(old_cs);                         if (cpu_state.abrt) { CS = old_cs; cgate16 = cgate32 = 0; return 1; }     \
                PUSH_L(old_pc);                         if (cpu_state.abrt) { CS = old_cs; ESP = old_esp; return 1; } \
        }
#else
#define CALL_FAR_w(new_seg, new_pc)                                             \
        old_cs = CS;                                                            \
        old_pc = cpu_state.pc;                                                  \
		oxpc = cpu_state.pc;													\
        cpu_state.pc = new_pc;                                                  \
        optype = CALL;                                                          \
        cgate16 = cgate32 = 0;                                                  \
        if (msw & 1) loadcscall(new_seg);                               		\
        else                                                                    \
        {                                                                       \
                loadcs(new_seg);                                                \
                cycles -= timing_call_rm;                                       \
        }                                                                       \
        optype = 0;                                                             \
        if (cpu_state.abrt) { cgate16 = cgate32 = 0; return 1; }                          \
        oldss = ss;                                                             \
        if (cgate32)                                                            \
        {                                                                       \
                uint32_t old_esp = ESP;                                         \
                PUSH_L(old_cs);                         if (cpu_state.abrt) { cgate16 = cgate32 = 0; return 1; }     \
                PUSH_L(old_pc);                         if (cpu_state.abrt) { ESP = old_esp; return 1; } \
        }                                                                       \
        else                                                                    \
        {                                                                       \
                uint32_t old_esp = ESP;                                         \
                PUSH_W(old_cs);                         if (cpu_state.abrt) { cgate16 = cgate32 = 0; return 1; }     \
                PUSH_W(old_pc);                         if (cpu_state.abrt) { ESP = old_esp; return 1; } \
        }

#define CALL_FAR_l(new_seg, new_pc)                                             \
        old_cs = CS;                                                            \
        old_pc = cpu_state.pc;                                                  \
		oxpc = cpu_state.pc;													\
        cpu_state.pc = new_pc;                                                  \
        optype = CALL;                                                          \
        cgate16 = cgate32 = 0;                                                  \
        if (msw & 1) loadcscall(new_seg);                               		\
        else                                                                    \
        {                                                                       \
                loadcs(new_seg);                                                \
                cycles -= timing_call_rm;                                       \
        }                                                                       \
        optype = 0;                                                             \
        if (cpu_state.abrt) { cgate16 = cgate32 = 0; return 1; }                          \
        oldss = ss;                                                             \
        if (cgate16)                                                            \
        {                                                                       \
                uint32_t old_esp = ESP;                                         \
                PUSH_W(old_cs);                         if (cpu_state.abrt) { cgate16 = cgate32 = 0; return 1; }     \
                PUSH_W(old_pc);                         if (cpu_state.abrt) { ESP = old_esp; return 1; } \
        }                                                                       \
        else                                                                    \
        {                                                                       \
                uint32_t old_esp = ESP;                                         \
                PUSH_L(old_cs);                         if (cpu_state.abrt) { cgate16 = cgate32 = 0; return 1; }     \
                PUSH_L(old_pc);                         if (cpu_state.abrt) { ESP = old_esp; return 1; } \
        }
#endif


static int opCALL_far_w(uint32_t fetchdat)
{
        uint32_t old_cs, old_pc;
        uint16_t new_cs, new_pc;
        int cycles_old = cycles; UN_USED(cycles_old);

        new_pc = getwordf();
        new_cs = getword();                             if (cpu_state.abrt) return 1;

        CALL_FAR_w(new_cs, new_pc);
        CPU_BLOCK_END();
        PREFETCH_RUN(cycles_old-cycles, 5, -1, 0,0,cgate16 ? 2:0,cgate16 ? 0:2, 0);
        PREFETCH_FLUSH();

        return 0;
}
static int opCALL_far_l(uint32_t fetchdat)
{
        uint32_t old_cs, old_pc;
        uint32_t new_cs, new_pc;
        int cycles_old = cycles; UN_USED(cycles_old);

        new_pc = getlong();
        new_cs = getword();                             if (cpu_state.abrt) return 1;

        CALL_FAR_l(new_cs, new_pc);
        CPU_BLOCK_END();
        PREFETCH_RUN(cycles_old-cycles, 7, -1, 0,0,cgate16 ? 2:0,cgate16 ? 0:2, 0);
        PREFETCH_FLUSH();

        return 0;
}


static int opFF_w_a16(uint32_t fetchdat)
{
        uint16_t old_cs, new_cs;
        uint32_t old_pc, new_pc;
        int cycles_old = cycles; UN_USED(cycles_old);

        uint16_t temp;

        fetch_ea_16(fetchdat);

        switch (rmdat & 0x38)
        {
                case 0x00: /*INC w*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 1UL);
                }
                temp = geteaw();                        if (cpu_state.abrt) return 1;
                if (cpu_mod != 3) {
                        CHECK_WRITE(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 1UL);
                }
                seteaw(temp + 1);                       if (cpu_state.abrt) return 1;
                setadd16nc(temp, 1);
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
                PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mm, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, 0);
                break;
                case 0x08: /*DEC w*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 1UL);
                }
                temp = geteaw();                        if (cpu_state.abrt) return 1;
                if (cpu_mod != 3) {
                        CHECK_WRITE(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 1UL);
                }
                seteaw(temp - 1);                       if (cpu_state.abrt) return 1;
                setsub16nc(temp, 1);
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
                PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mm, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, 0);
                break;
                case 0x10: /*CALL*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 1UL);
                }
                new_pc = geteaw();                      if (cpu_state.abrt) return 1;
                PUSH_W(cpu_state.pc);
                cpu_state.pc = new_pc;
                CPU_BLOCK_END();
                if (is486) { CLOCK_CYCLES(5); }
                else       { CLOCK_CYCLES((cpu_mod == 3) ? 7 : 10); }
                PREFETCH_RUN((cpu_mod == 3) ? 7 : 10, 2, rmdat, (cpu_mod == 3) ? 0:1,0,1,0, 0);
                PREFETCH_FLUSH();
                break;
                case 0x18: /*CALL far*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3UL);
                }
                new_pc = readmemw(easeg, cpu_state.eaaddr);
                new_cs = readmemw(easeg, (cpu_state.eaaddr + 2)); if (cpu_state.abrt) return 1;

                CALL_FAR_w(new_cs, new_pc);
                CPU_BLOCK_END();
                PREFETCH_RUN(cycles_old-cycles, 2, rmdat, 2,0,cgate16 ? 2:0,cgate16 ? 0:2, 0);
                PREFETCH_FLUSH();
                break;
                case 0x20: /*JMP*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 1UL);
                }
                new_pc = geteaw();                      if (cpu_state.abrt) return 1;
                cpu_state.pc = new_pc;
                CPU_BLOCK_END();
                if (is486) { CLOCK_CYCLES(5); }
                else       { CLOCK_CYCLES((cpu_mod == 3) ? 7 : 10); }
                PREFETCH_RUN((cpu_mod == 3) ? 7 : 10, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 0);
                PREFETCH_FLUSH();
                break;
                case 0x28: /*JMP far*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3UL);
                }
#ifdef USE_NEW_DYNAREC
                old_pc = cpu_state.pc;
#else
                oxpc = cpu_state.pc;
#endif
                new_pc = readmemw(easeg, cpu_state.eaaddr);
                new_cs = readmemw(easeg, cpu_state.eaaddr + 2);  if (cpu_state.abrt) return 1;
                cpu_state.pc = new_pc;
#ifdef USE_NEW_DYNAREC
                loadcsjmp(new_cs, old_pc);               if (cpu_state.abrt) return 1;
#else
                loadcsjmp(new_cs, oxpc);               if (cpu_state.abrt) return 1;
#endif
                CPU_BLOCK_END();
                PREFETCH_RUN(cycles_old-cycles, 2, rmdat, 2,0,0,0, 0);
                PREFETCH_FLUSH();
                break;
                case 0x30: /*PUSH w*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 1UL);
                }
                temp = geteaw();                        if (cpu_state.abrt) return 1;
                PUSH_W(temp);
                CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
                PREFETCH_RUN((cpu_mod == 3) ? 2 : 5, 2, rmdat, (cpu_mod == 3) ? 0:1,0,1,0, 0);
                break;

                default:
//                fatal("Bad FF opcode %02X\n",rmdat&0x38);
                x86illegal();
        }
        return cpu_state.abrt;
}
static int opFF_w_a32(uint32_t fetchdat)
{
        uint16_t old_cs, new_cs;
        uint32_t old_pc, new_pc;
        int cycles_old = cycles; UN_USED(cycles_old);

        uint16_t temp;

        fetch_ea_32(fetchdat);

        switch (rmdat & 0x38)
        {
                case 0x00: /*INC w*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 1UL);
                }
                temp = geteaw();                        if (cpu_state.abrt) return 1;
                if (cpu_mod != 3) {
                        CHECK_WRITE(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 1UL);
                }
                seteaw(temp + 1);                       if (cpu_state.abrt) return 1;
                setadd16nc(temp, 1);
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
                PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mm, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, 1);
                break;
                case 0x08: /*DEC w*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 1UL);
                }
                temp = geteaw();                        if (cpu_state.abrt) return 1;
                if (cpu_mod != 3) {
                        CHECK_WRITE(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 1UL);
                }
                seteaw(temp - 1);                       if (cpu_state.abrt) return 1;
                setsub16nc(temp, 1);
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
                PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mm, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, 1);
                break;
                case 0x10: /*CALL*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 1UL);
                }
                new_pc = geteaw();                      if (cpu_state.abrt) return 1;
                PUSH_W(cpu_state.pc);
                cpu_state.pc = new_pc;
                CPU_BLOCK_END();
                if (is486) { CLOCK_CYCLES(5); }
                else       { CLOCK_CYCLES((cpu_mod == 3) ? 7 : 10); }
                PREFETCH_RUN((cpu_mod == 3) ? 7 : 10, 2, rmdat, (cpu_mod == 3) ? 0:1,0,1,0, 1);
                PREFETCH_FLUSH();
                break;
                case 0x18: /*CALL far*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3UL);
                }
                new_pc = readmemw(easeg, cpu_state.eaaddr);
                new_cs = readmemw(easeg, (cpu_state.eaaddr + 2)); if (cpu_state.abrt) return 1;

                CALL_FAR_w(new_cs, new_pc);
                CPU_BLOCK_END();
                PREFETCH_RUN(cycles_old-cycles, 2, rmdat, 2,0,cgate16 ? 2:0,cgate16 ? 0:2, 1);
                PREFETCH_FLUSH();
                break;
                case 0x20: /*JMP*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 1UL);
                }
                new_pc = geteaw();                      if (cpu_state.abrt) return 1;
                cpu_state.pc = new_pc;
                CPU_BLOCK_END();
                if (is486) { CLOCK_CYCLES(5); }
                else       { CLOCK_CYCLES((cpu_mod == 3) ? 7 : 10); }
                PREFETCH_RUN(cycles_old-cycles, 2, rmdat, 1,0,0,0, 1);
                PREFETCH_FLUSH();
                break;
                case 0x28: /*JMP far*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3UL);
                }
#ifdef USE_NEW_DYNAREC
                old_pc = cpu_state.pc;
#else
                oxpc = cpu_state.pc;
#endif
                new_pc = readmemw(easeg, cpu_state.eaaddr);
                new_cs = readmemw(easeg, cpu_state.eaaddr + 2);  if (cpu_state.abrt) return 1;
                cpu_state.pc = new_pc;
#ifdef USE_NEW_DYNAREC
                loadcsjmp(new_cs, old_pc);               if (cpu_state.abrt) return 1;
#else
                loadcsjmp(new_cs, oxpc);               if (cpu_state.abrt) return 1;
#endif
                CPU_BLOCK_END();
                PREFETCH_RUN(cycles_old-cycles, 2, rmdat, 2,0,0,0, 1);
                PREFETCH_FLUSH();
                break;
                case 0x30: /*PUSH w*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 1UL);
                }
                temp = geteaw();                        if (cpu_state.abrt) return 1;
                PUSH_W(temp);
                CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
                PREFETCH_RUN((cpu_mod == 3) ? 2 : 5, 2, rmdat, (cpu_mod == 3) ? 0:1,0,1,0, 1);
                break;

                default:
//                fatal("Bad FF opcode %02X\n",rmdat&0x38);
                x86illegal();
        }
        return cpu_state.abrt;
}

static int opFF_l_a16(uint32_t fetchdat)
{
        uint16_t old_cs, new_cs;
        uint32_t old_pc, new_pc;
        int cycles_old = cycles; UN_USED(cycles_old);

        uint32_t temp;

        fetch_ea_16(fetchdat);

        switch (rmdat & 0x38)
        {
                case 0x00: /*INC l*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3UL);
                }
                temp = geteal();                        if (cpu_state.abrt) return 1;
                if (cpu_mod != 3) {
                        CHECK_WRITE(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3UL);
                }
                seteal(temp + 1);                       if (cpu_state.abrt) return 1;
                setadd32nc(temp, 1);
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
                PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mm, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, 0);
                break;
                case 0x08: /*DEC l*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3UL);
                }
                temp = geteal();                        if (cpu_state.abrt) return 1;
                if (cpu_mod != 3) {
                        CHECK_WRITE(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3UL);
                }
                seteal(temp - 1);                       if (cpu_state.abrt) return 1;
                setsub32nc(temp, 1);
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
                PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mm, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, 0);
                break;
                case 0x10: /*CALL*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3UL);
                }
                new_pc = geteal();                      if (cpu_state.abrt) return 1;
                PUSH_L(cpu_state.pc);
                cpu_state.pc = new_pc;
                CPU_BLOCK_END();
                if (is486) { CLOCK_CYCLES(5); }
                else       { CLOCK_CYCLES((cpu_mod == 3) ? 7 : 10); }
                PREFETCH_RUN((cpu_mod == 3) ? 7 : 10, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,1, 0);
                PREFETCH_FLUSH();
                break;
                case 0x18: /*CALL far*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 5UL);
                }
                new_pc = readmeml(easeg, cpu_state.eaaddr);
                new_cs = readmemw(easeg, (cpu_state.eaaddr + 4)); if (cpu_state.abrt) return 1;

                CALL_FAR_l(new_cs, new_pc);
                CPU_BLOCK_END();
                PREFETCH_RUN(cycles_old-cycles, 2, rmdat, 1,1,cgate16 ? 2:0,cgate16 ? 0:2, 0);
                PREFETCH_FLUSH();
                break;
                case 0x20: /*JMP*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3UL);
                }
                new_pc = geteal();                      if (cpu_state.abrt) return 1;
                cpu_state.pc = new_pc;
                CPU_BLOCK_END();
                if (is486) { CLOCK_CYCLES(5); }
                else       { CLOCK_CYCLES((cpu_mod == 3) ? 7 : 10); }
                PREFETCH_RUN(cycles_old-cycles, 2, rmdat, 0,1,0,0, 0);
                PREFETCH_FLUSH();
                break;
                case 0x28: /*JMP far*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 5UL);
                }
#ifdef USE_NEW_DYNAREC
                old_pc = cpu_state.pc;
#else
                oxpc = cpu_state.pc;
#endif
                new_pc = readmeml(easeg, cpu_state.eaaddr);
                new_cs = readmemw(easeg, cpu_state.eaaddr + 4);   if (cpu_state.abrt) return 1;
                cpu_state.pc = new_pc;
#ifdef USE_NEW_DYNAREC
                loadcsjmp(new_cs, old_pc);               if (cpu_state.abrt) return 1;
#else
                loadcsjmp(new_cs, oxpc);               if (cpu_state.abrt) return 1;
#endif
                CPU_BLOCK_END();
                PREFETCH_RUN(cycles_old-cycles, 2, rmdat, 1,1,0,0, 0);
                PREFETCH_FLUSH();
                break;
                case 0x30: /*PUSH l*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3UL);
                }
                temp = geteal();                        if (cpu_state.abrt) return 1;
                PUSH_L(temp);
                CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
                PREFETCH_RUN((cpu_mod == 3) ? 2 : 5, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,1, 0);
                break;

                default:
//                fatal("Bad FF opcode %02X\n",rmdat&0x38);
                x86illegal();
        }
        return cpu_state.abrt;
}
static int opFF_l_a32(uint32_t fetchdat)
{
        uint16_t old_cs, new_cs;
        uint32_t old_pc, new_pc;
        int cycles_old = cycles; UN_USED(cycles_old);

        uint32_t temp;

        fetch_ea_32(fetchdat);

        switch (rmdat & 0x38)
        {
                case 0x00: /*INC l*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3UL);
                }
                temp = geteal();                        if (cpu_state.abrt) return 1;
                if (cpu_mod != 3) {
                        CHECK_WRITE(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3UL);
                }
                seteal(temp + 1);                       if (cpu_state.abrt) return 1;
                setadd32nc(temp, 1);
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
                PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mm, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, 1);
                break;
                case 0x08: /*DEC l*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3UL);
                }
                temp = geteal();                        if (cpu_state.abrt) return 1;
                if (cpu_mod != 3) {
                        CHECK_WRITE(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3UL);
                }
                seteal(temp - 1);                       if (cpu_state.abrt) return 1;
                setsub32nc(temp, 1);
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
                PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mm, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, 1);
                break;
                case 0x10: /*CALL*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3UL);
                }
                new_pc = geteal();                      if (cpu_state.abrt) return 1;
                PUSH_L(cpu_state.pc);                             if (cpu_state.abrt) return 1;
                cpu_state.pc = new_pc;
                CPU_BLOCK_END();
                if (is486) { CLOCK_CYCLES(5); }
                else       { CLOCK_CYCLES((cpu_mod == 3) ? 7 : 10); }
                PREFETCH_RUN((cpu_mod == 3) ? 7 : 10, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,1, 1);
                PREFETCH_FLUSH();
                break;
                case 0x18: /*CALL far*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 5UL);
                }
                new_pc = readmeml(easeg, cpu_state.eaaddr);
                new_cs = readmemw(easeg, (cpu_state.eaaddr + 4)); if (cpu_state.abrt) return 1;

                CALL_FAR_l(new_cs, new_pc);
                CPU_BLOCK_END();
                PREFETCH_RUN(cycles_old-cycles, 2, rmdat, 1,1,cgate16 ? 2:0,cgate16 ? 0:2, 1);
                PREFETCH_FLUSH();
                break;
                case 0x20: /*JMP*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3UL);
                }
                new_pc = geteal();                      if (cpu_state.abrt) return 1;
                cpu_state.pc = new_pc;
                CPU_BLOCK_END();
                if (is486) { CLOCK_CYCLES(5); }
                else       { CLOCK_CYCLES((cpu_mod == 3) ? 7 : 10); }
                PREFETCH_RUN(cycles_old-cycles, 2, rmdat, 1,1,0,0, 1);
                PREFETCH_FLUSH();
                break;
                case 0x28: /*JMP far*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 5UL);
                }
#ifdef USE_NEW_DYNAREC
                old_pc = cpu_state.pc;
#else
                oxpc = cpu_state.pc;
#endif
                new_pc = readmeml(easeg, cpu_state.eaaddr);
                new_cs = readmemw(easeg, cpu_state.eaaddr + 4);   if (cpu_state.abrt) return 1;
                cpu_state.pc = new_pc;
#ifdef USE_NEW_DYNAREC
                loadcsjmp(new_cs, old_pc);               if (cpu_state.abrt) return 1;
#else
                loadcsjmp(new_cs, oxpc);               if (cpu_state.abrt) return 1;
#endif
                CPU_BLOCK_END();
                PREFETCH_RUN(cycles_old-cycles, 2, rmdat, 1,1,0,0, 1);
                PREFETCH_FLUSH();
                break;
                case 0x30: /*PUSH l*/
                if (cpu_mod != 3) {
                        SEG_CHECK_READ(cpu_state.ea_seg); if (cpu_state.abrt) return 1;
                        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3UL);
                }
                temp = geteal();                        if (cpu_state.abrt) return 1;
                PUSH_L(temp);
                PREFETCH_RUN((cpu_mod == 3) ? 2 : 5, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,1, 1);
                break;

                default:
//                fatal("Bad FF opcode %02X\n",rmdat&0x38);
                x86illegal();
        }
        return cpu_state.abrt;
}
