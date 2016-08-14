#define CALL_FAR_w(new_seg, new_pc)                                             \
        old_cs = CS;                                                            \
        old_pc = cpu_state.pc;                                                  \
        oxpc = cpu_state.pc;                                                    \
        cpu_state.pc = new_pc;                                                  \
        optype = CALL;                                                          \
        cgate16 = cgate32 = 0;                                                  \
        if (msw & 1) loadcscall(new_seg);                                       \
        else                                                                    \
        {                                                                       \
                loadcs(new_seg);                                                \
                cycles -= timing_call_rm;                                       \
        }                                                                       \
        optype = 0;                                                             \
        if (abrt) { cgate16 = cgate32 = 0; return 1; }                          \
        oldss = ss;                                                             \
        if (cgate32)                                                            \
        {                                                                       \
                uint32_t old_esp = ESP;                                         \
                PUSH_L(old_cs);                         if (abrt) { cgate16 = cgate32 = 0; return 1; }     \
                PUSH_L(old_pc);                         if (abrt) { ESP = old_esp; return 1; } \
        }                                                                       \
        else                                                                    \
        {                                                                       \
                uint32_t old_esp = ESP;                                         \
                PUSH_W(old_cs);                         if (abrt) { cgate16 = cgate32 = 0; return 1; }     \
                PUSH_W(old_pc);                         if (abrt) { ESP = old_esp; return 1; } \
        }
        
#define CALL_FAR_l(new_seg, new_pc)                                             \
        old_cs = CS;                                                            \
        old_pc = cpu_state.pc;                                                  \
        oxpc = cpu_state.pc;                                                    \
        cpu_state.pc = new_pc;                                                  \
        optype = CALL;                                                          \
        cgate16 = cgate32 = 0;                                                  \
        if (msw & 1) loadcscall(new_seg);                                       \
        else                                                                    \
        {                                                                       \
                loadcs(new_seg);                                                \
                cycles -= timing_call_rm;                                       \
        }                                                                       \
        optype = 0;                                                             \
        if (abrt) { cgate16 = cgate32 = 0; return 1; }                          \
        oldss = ss;                                                             \
        if (cgate16)                                                            \
        {                                                                       \
                uint32_t old_esp = ESP;                                         \
                PUSH_W(old_cs);                         if (abrt) { cgate16 = cgate32 = 0; return 1; }     \
                PUSH_W(old_pc);                         if (abrt) { ESP = old_esp; return 1; } \
        }                                                                       \
        else                                                                    \
        {                                                                       \
                uint32_t old_esp = ESP;                                         \
                PUSH_L(old_cs);                         if (abrt) { cgate16 = cgate32 = 0; return 1; }     \
                PUSH_L(old_pc);                         if (abrt) { ESP = old_esp; return 1; } \
        }
        
       
static int opCALL_far_w(uint32_t fetchdat)
{
        uint32_t old_cs, old_pc;
        uint16_t new_cs, new_pc;
        
        new_pc = getwordf();
        new_cs = getword();                             if (abrt) return 1;
               
        CALL_FAR_w(new_cs, new_pc);
        CPU_BLOCK_END();
        
        return 0;
}
static int opCALL_far_l(uint32_t fetchdat)
{
        uint32_t old_cs, old_pc;
        uint32_t new_cs, new_pc;
        
        new_pc = getlong();
        new_cs = getword();                             if (abrt) return 1;
        
        CALL_FAR_l(new_cs, new_pc);
        CPU_BLOCK_END();
        
        return 0;
}


static int opFF_w_a16(uint32_t fetchdat)
{
        uint16_t old_cs, new_cs;
        uint32_t old_pc, new_pc;
        
        uint16_t temp;
        
        fetch_ea_16(fetchdat);
        
        switch (rmdat & 0x38)
        {
                case 0x00: /*INC w*/
                temp = geteaw();                        if (abrt) return 1;
                seteaw(temp + 1);                       if (abrt) return 1;
                setadd16nc(temp, 1);
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
                break;
                case 0x08: /*DEC w*/
                temp = geteaw();                        if (abrt) return 1;
                seteaw(temp - 1);                       if (abrt) return 1;
                setsub16nc(temp, 1);
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
                break;
                case 0x10: /*CALL*/
                new_pc = geteaw();                      if (abrt) return 1;
                PUSH_W(cpu_state.pc);
                cpu_state.pc = new_pc;
                CPU_BLOCK_END();
                if (is486) CLOCK_CYCLES(5);
                else       CLOCK_CYCLES((cpu_mod == 3) ? 7 : 10);
                break;
                case 0x18: /*CALL far*/
                new_pc = readmemw(easeg, eaaddr);
                new_cs = readmemw(easeg, (eaaddr + 2)); if (abrt) return 1;
                
                CALL_FAR_w(new_cs, new_pc);
                CPU_BLOCK_END();
                break;
                case 0x20: /*JMP*/
                new_pc = geteaw();                      if (abrt) return 1;
                cpu_state.pc = new_pc;
                CPU_BLOCK_END();
                if (is486) CLOCK_CYCLES(5);
                else       CLOCK_CYCLES((cpu_mod == 3) ? 7 : 10);
                break;
                case 0x28: /*JMP far*/
                oxpc = cpu_state.pc;
                new_pc = readmemw(easeg, eaaddr);
                new_cs = readmemw(easeg, eaaddr + 2);  if (abrt) return 1;
                cpu_state.pc = new_pc;
                loadcsjmp(new_cs, oxpc);               if (abrt) return 1;
                CPU_BLOCK_END();
                break;
                case 0x30: /*PUSH w*/
                temp = geteaw();                        if (abrt) return 1;
                PUSH_W(temp);
                CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
                break;

                default:
//                fatal("Bad FF opcode %02X\n",rmdat&0x38);
                x86illegal();
        }
        return abrt;
}
static int opFF_w_a32(uint32_t fetchdat)
{
        uint16_t old_cs, new_cs;
        uint32_t old_pc, new_pc;
        
        uint16_t temp;
        
        fetch_ea_32(fetchdat);
        
        switch (rmdat & 0x38)
        {
                case 0x00: /*INC w*/
                temp = geteaw();                        if (abrt) return 1;
                seteaw(temp + 1);                       if (abrt) return 1;
                setadd16nc(temp, 1);
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
                break;
                case 0x08: /*DEC w*/
                temp = geteaw();                        if (abrt) return 1;
                seteaw(temp - 1);                       if (abrt) return 1;
                setsub16nc(temp, 1);
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
                break;
                case 0x10: /*CALL*/
                new_pc = geteaw();                      if (abrt) return 1;
                PUSH_W(cpu_state.pc);
                cpu_state.pc = new_pc;
                CPU_BLOCK_END();
                if (is486) CLOCK_CYCLES(5);
                else       CLOCK_CYCLES((cpu_mod == 3) ? 7 : 10);
                break;
                case 0x18: /*CALL far*/
                new_pc = readmemw(easeg, eaaddr);
                new_cs = readmemw(easeg, (eaaddr + 2)); if (abrt) return 1;
                
                CALL_FAR_w(new_cs, new_pc);
                CPU_BLOCK_END();
                break;
                case 0x20: /*JMP*/
                new_pc = geteaw();                      if (abrt) return 1;
                cpu_state.pc = new_pc;
                CPU_BLOCK_END();
                if (is486) CLOCK_CYCLES(5);
                else       CLOCK_CYCLES((cpu_mod == 3) ? 7 : 10);
                break;
                case 0x28: /*JMP far*/
                oxpc = cpu_state.pc;
                new_pc = readmemw(easeg, eaaddr);
                new_cs = readmemw(easeg, eaaddr + 2);  if (abrt) return 1;
                cpu_state.pc = new_pc;
                loadcsjmp(new_cs, oxpc);               if (abrt) return 1;
                CPU_BLOCK_END();
                break;
                case 0x30: /*PUSH w*/
                temp = geteaw();                        if (abrt) return 1;
                PUSH_W(temp);
                CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
                break;

                default:
//                fatal("Bad FF opcode %02X\n",rmdat&0x38);
                x86illegal();
        }
        return abrt;
}

static int opFF_l_a16(uint32_t fetchdat)
{
        uint16_t old_cs, new_cs;
        uint32_t old_pc, new_pc;
        
        uint32_t temp;
        
        fetch_ea_16(fetchdat);
        
        switch (rmdat & 0x38)
        {
                case 0x00: /*INC l*/
                temp = geteal();                        if (abrt) return 1;
                seteal(temp + 1);                       if (abrt) return 1;
                setadd32nc(temp, 1);
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
                break;
                case 0x08: /*DEC l*/
                temp = geteal();                        if (abrt) return 1;
                seteal(temp - 1);                       if (abrt) return 1;
                setsub32nc(temp, 1);
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
                break;
                case 0x10: /*CALL*/
                new_pc = geteal();                      if (abrt) return 1;
                PUSH_L(cpu_state.pc);
                cpu_state.pc = new_pc;
                CPU_BLOCK_END();
                if (is486) CLOCK_CYCLES(5);
                else       CLOCK_CYCLES((cpu_mod == 3) ? 7 : 10);
                break;
                case 0x18: /*CALL far*/
                new_pc = readmeml(easeg, eaaddr);
                new_cs = readmemw(easeg, (eaaddr + 4)); if (abrt) return 1;
                
                CALL_FAR_l(new_cs, new_pc);
                CPU_BLOCK_END();
                break;
                case 0x20: /*JMP*/
                new_pc = geteal();                      if (abrt) return 1;
                cpu_state.pc = new_pc;
                CPU_BLOCK_END();
                if (is486) CLOCK_CYCLES(5);
                else       CLOCK_CYCLES((cpu_mod == 3) ? 7 : 10);
                break;
                case 0x28: /*JMP far*/
                oxpc = cpu_state.pc;
                new_pc = readmeml(easeg, eaaddr);
                new_cs = readmemw(easeg, eaaddr + 4);   if (abrt) return 1;
                cpu_state.pc = new_pc;
                loadcsjmp(new_cs, oxpc);                if (abrt) return 1;
                CPU_BLOCK_END();
                break;
                case 0x30: /*PUSH l*/
                temp = geteal();                        if (abrt) return 1;
                PUSH_L(temp);
                CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
                break;

                default:
//                fatal("Bad FF opcode %02X\n",rmdat&0x38);
                x86illegal();
        }
        return abrt;
}
static int opFF_l_a32(uint32_t fetchdat)
{
        uint16_t old_cs, new_cs;
        uint32_t old_pc, new_pc;
        
        uint32_t temp;
        
        fetch_ea_32(fetchdat);
        
        switch (rmdat & 0x38)
        {
                case 0x00: /*INC l*/
                temp = geteal();                        if (abrt) return 1;
                seteal(temp + 1);                       if (abrt) return 1;
                setadd32nc(temp, 1);
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
                break;
                case 0x08: /*DEC l*/
                temp = geteal();                        if (abrt) return 1;
                seteal(temp - 1);                       if (abrt) return 1;
                setsub32nc(temp, 1);
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
                break;
                case 0x10: /*CALL*/
                new_pc = geteal();                      if (abrt) return 1;
                PUSH_L(cpu_state.pc);                             if (abrt) return 1;
                cpu_state.pc = new_pc;
                CPU_BLOCK_END();
                if (is486) CLOCK_CYCLES(5);
                else       CLOCK_CYCLES((cpu_mod == 3) ? 7 : 10);
                break;
                case 0x18: /*CALL far*/
                new_pc = readmeml(easeg, eaaddr);
                new_cs = readmemw(easeg, (eaaddr + 4)); if (abrt) return 1;
                
                CALL_FAR_l(new_cs, new_pc);
                CPU_BLOCK_END();
                break;
                case 0x20: /*JMP*/
                new_pc = geteal();                      if (abrt) return 1;
                cpu_state.pc = new_pc;
                CPU_BLOCK_END();
                if (is486) CLOCK_CYCLES(5);
                else       CLOCK_CYCLES((cpu_mod == 3) ? 7 : 10);
                break;
                case 0x28: /*JMP far*/
                oxpc = cpu_state.pc;
                new_pc = readmeml(easeg, eaaddr);
                new_cs = readmemw(easeg, eaaddr + 4);   if (abrt) return 1;
                cpu_state.pc = new_pc;
                loadcsjmp(new_cs, oxpc);                if (abrt) return 1;
                CPU_BLOCK_END();
                break;
                case 0x30: /*PUSH l*/
                temp = geteal();                        if (abrt) return 1;
                PUSH_L(temp);
                CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
                break;

                default:
//                fatal("Bad FF opcode %02X\n",rmdat&0x38);
                x86illegal();
        }
        return abrt;
}
