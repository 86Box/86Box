static int opMOV_w_seg_a16(uint32_t fetchdat)
{
        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);

        switch (rmdat & 0x38)
        {
                case 0x00: /*ES*/
                seteaw(ES);
                break;
                case 0x08: /*CS*/
                seteaw(CS);
                break;
                case 0x18: /*DS*/
                seteaw(DS);
                break;
                case 0x10: /*SS*/
                seteaw(SS);
                break;
                case 0x20: /*FS*/
                seteaw(FS);
                break;
                case 0x28: /*GS*/
                seteaw(GS);
                break;
        }
                        
        CLOCK_CYCLES((cpu_mod == 3) ? 2 : 3);
        PREFETCH_RUN((cpu_mod == 3) ? 2 : 3, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,0, 0);
        return cpu_state.abrt;
}
static int opMOV_w_seg_a32(uint32_t fetchdat)
{
        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);

        switch (rmdat & 0x38)
        {
                case 0x00: /*ES*/
                seteaw(ES);
                break;
                case 0x08: /*CS*/
                seteaw(CS);
                break;
                case 0x18: /*DS*/
                seteaw(DS);
                break;
                case 0x10: /*SS*/
                seteaw(SS);
                break;
                case 0x20: /*FS*/
                seteaw(FS);
                break;
                case 0x28: /*GS*/
                seteaw(GS);
                break;
        }
                        
        CLOCK_CYCLES((cpu_mod == 3) ? 2 : 3);
        PREFETCH_RUN((cpu_mod == 3) ? 2 : 3, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,0, 1);
        return cpu_state.abrt;
}

static int opMOV_l_seg_a16(uint32_t fetchdat)
{
        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);

        switch (rmdat & 0x38)
        {
                case 0x00: /*ES*/
                if (cpu_mod == 3) cpu_state.regs[cpu_rm].l = ES;
                else          seteaw(ES);
                break;
                case 0x08: /*CS*/
                if (cpu_mod == 3) cpu_state.regs[cpu_rm].l = CS;
                else          seteaw(CS);
                break;
                case 0x18: /*DS*/
                if (cpu_mod == 3) cpu_state.regs[cpu_rm].l = DS;
                else          seteaw(DS);
                break;
                case 0x10: /*SS*/
                if (cpu_mod == 3) cpu_state.regs[cpu_rm].l = SS;
                else          seteaw(SS);
                break;
                case 0x20: /*FS*/
                if (cpu_mod == 3) cpu_state.regs[cpu_rm].l = FS;
                else          seteaw(FS);
                break;
                case 0x28: /*GS*/
                if (cpu_mod == 3) cpu_state.regs[cpu_rm].l = GS;
                else          seteaw(GS);
                break;
        }
        
        CLOCK_CYCLES((cpu_mod == 3) ? 2 : 3);
        PREFETCH_RUN((cpu_mod == 3) ? 2 : 3, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,0, 0);
        return cpu_state.abrt;
}
static int opMOV_l_seg_a32(uint32_t fetchdat)
{
        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);

        switch (rmdat & 0x38)
        {
                case 0x00: /*ES*/
                if (cpu_mod == 3) cpu_state.regs[cpu_rm].l = ES;
                else          seteaw(ES);
                break;
                case 0x08: /*CS*/
                if (cpu_mod == 3) cpu_state.regs[cpu_rm].l = CS;
                else          seteaw(CS);
                break;
                case 0x18: /*DS*/
                if (cpu_mod == 3) cpu_state.regs[cpu_rm].l = DS;
                else          seteaw(DS);
                break;
                case 0x10: /*SS*/
                if (cpu_mod == 3) cpu_state.regs[cpu_rm].l = SS;
                else          seteaw(SS);
                break;
                case 0x20: /*FS*/
                if (cpu_mod == 3) cpu_state.regs[cpu_rm].l = FS;
                else          seteaw(FS);
                break;
                case 0x28: /*GS*/
                if (cpu_mod == 3) cpu_state.regs[cpu_rm].l = GS;
                else          seteaw(GS);
                break;
        }
        
        CLOCK_CYCLES((cpu_mod == 3) ? 2 : 3);
        PREFETCH_RUN((cpu_mod == 3) ? 2 : 3, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,0, 1);
        return cpu_state.abrt;
}

static int opMOV_seg_w_a16(uint32_t fetchdat)
{
        uint16_t new_seg;
        
        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        new_seg=geteaw();         if (cpu_state.abrt) return 1;
        
        switch (rmdat & 0x38)
        {
                case 0x00: /*ES*/
                loadseg(new_seg, &cpu_state.seg_es);
                break;
                case 0x18: /*DS*/
                loadseg(new_seg, &cpu_state.seg_ds);
                break;
                case 0x10: /*SS*/
                loadseg(new_seg, &cpu_state.seg_ss);
                if (cpu_state.abrt) return 1;
                cpu_state.oldpc = cpu_state.pc;
                cpu_state.op32 = use32;
                cpu_state.ssegs = 0;
                cpu_state.ea_seg = &cpu_state.seg_ds;
                fetchdat = fastreadl(cs + cpu_state.pc);
                cpu_state.pc++;
                if (cpu_state.abrt) return 1;
                x86_opcodes[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
                return 1;
                case 0x20: /*FS*/
                loadseg(new_seg, &cpu_state.seg_fs);
                break;
                case 0x28: /*GS*/
                loadseg(new_seg, &cpu_state.seg_gs);
                break;
        }
                        
        CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
        PREFETCH_RUN((cpu_mod == 3) ? 2 : 5, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,0, 0);
        return cpu_state.abrt;
}
static int opMOV_seg_w_a32(uint32_t fetchdat)
{
        uint16_t new_seg;
        
        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        new_seg=geteaw();         if (cpu_state.abrt) return 1;
        
        switch (rmdat & 0x38)
        {
                case 0x00: /*ES*/
                loadseg(new_seg, &cpu_state.seg_es);
                break;
                case 0x18: /*DS*/
                loadseg(new_seg, &cpu_state.seg_ds);
                break;
                case 0x10: /*SS*/
                loadseg(new_seg, &cpu_state.seg_ss);
                if (cpu_state.abrt) return 1;
                cpu_state.oldpc = cpu_state.pc;
                cpu_state.op32 = use32;
                cpu_state.ssegs = 0;
                cpu_state.ea_seg = &cpu_state.seg_ds;
                fetchdat = fastreadl(cs + cpu_state.pc);
                cpu_state.pc++;
                if (cpu_state.abrt) return 1;
                x86_opcodes[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
                return 1;
                case 0x20: /*FS*/
                loadseg(new_seg, &cpu_state.seg_fs);
                break;
                case 0x28: /*GS*/
                loadseg(new_seg, &cpu_state.seg_gs);
                break;
        }
                        
        CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
        PREFETCH_RUN((cpu_mod == 3) ? 2 : 5, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,0, 1);
        return cpu_state.abrt;
}


static int opLDS_w_a16(uint32_t fetchdat)
{
        uint16_t addr, seg;

        fetch_ea_16(fetchdat);
        ILLEGAL_ON(cpu_mod == 3);
        SEG_CHECK_READ(cpu_state.ea_seg);
        addr = readmemw(easeg, cpu_state.eaaddr);
        seg = readmemw(easeg, cpu_state.eaaddr + 2);      if (cpu_state.abrt) return 1;
        loadseg(seg, &cpu_state.seg_ds);                  if (cpu_state.abrt) return 1;
        cpu_state.regs[cpu_reg].w = addr;
 
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 2, rmdat, 2,0,0,0, 0);
        return 0;
}
static int opLDS_w_a32(uint32_t fetchdat)
{
        uint16_t addr, seg;

        fetch_ea_32(fetchdat);
        ILLEGAL_ON(cpu_mod == 3);
        SEG_CHECK_READ(cpu_state.ea_seg);
        addr = readmemw(easeg, cpu_state.eaaddr);
        seg = readmemw(easeg, cpu_state.eaaddr + 2);      if (cpu_state.abrt) return 1;
        loadseg(seg, &cpu_state.seg_ds);                  if (cpu_state.abrt) return 1;
        cpu_state.regs[cpu_reg].w = addr;
 
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 2, rmdat, 2,0,0,0, 1);
        return 0;
}
static int opLDS_l_a16(uint32_t fetchdat)
{
        uint32_t addr;
        uint16_t seg;

        fetch_ea_16(fetchdat);
        ILLEGAL_ON(cpu_mod == 3);
        SEG_CHECK_READ(cpu_state.ea_seg);
        addr = readmeml(easeg, cpu_state.eaaddr);
        seg = readmemw(easeg, cpu_state.eaaddr + 4);      if (cpu_state.abrt) return 1;
        loadseg(seg, &cpu_state.seg_ds);                  if (cpu_state.abrt) return 1;
        cpu_state.regs[cpu_reg].l = addr;
 
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 2, rmdat, 1,1,0,0, 0);
        return 0;
}
static int opLDS_l_a32(uint32_t fetchdat)
{
        uint32_t addr;
        uint16_t seg;

        fetch_ea_32(fetchdat);
        ILLEGAL_ON(cpu_mod == 3);
        SEG_CHECK_READ(cpu_state.ea_seg);
        addr = readmeml(easeg, cpu_state.eaaddr);
        seg = readmemw(easeg, cpu_state.eaaddr + 4);      if (cpu_state.abrt) return 1;
        loadseg(seg, &cpu_state.seg_ds);                  if (cpu_state.abrt) return 1;
        cpu_state.regs[cpu_reg].l = addr;
 
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 2, rmdat, 1,1,0,0, 1);
        return 0;
}

static int opLSS_w_a16(uint32_t fetchdat)
{
        uint16_t addr, seg;

        fetch_ea_16(fetchdat);
        ILLEGAL_ON(cpu_mod == 3);
        SEG_CHECK_READ(cpu_state.ea_seg);
        addr = readmemw(easeg, cpu_state.eaaddr);
        seg = readmemw(easeg, cpu_state.eaaddr + 2);      if (cpu_state.abrt) return 1;
        loadseg(seg, &cpu_state.seg_ss);                  if (cpu_state.abrt) return 1;
        cpu_state.regs[cpu_reg].w = addr;
 
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 2, rmdat, 2,0,0,0, 0);
        return 1;
}
static int opLSS_w_a32(uint32_t fetchdat)
{
        uint16_t addr, seg;

        fetch_ea_32(fetchdat);
        ILLEGAL_ON(cpu_mod == 3);
        SEG_CHECK_READ(cpu_state.ea_seg);
        addr = readmemw(easeg, cpu_state.eaaddr);
        seg = readmemw(easeg, cpu_state.eaaddr + 2);      if (cpu_state.abrt) return 1;
        loadseg(seg, &cpu_state.seg_ss);                  if (cpu_state.abrt) return 1;
        cpu_state.regs[cpu_reg].w = addr;
 
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 2, rmdat, 2,0,0,0, 1);
        return 1;
}
static int opLSS_l_a16(uint32_t fetchdat)
{
        uint32_t addr;
        uint16_t seg;

        fetch_ea_16(fetchdat);
        ILLEGAL_ON(cpu_mod == 3);
        SEG_CHECK_READ(cpu_state.ea_seg);
        addr = readmeml(easeg, cpu_state.eaaddr);
        seg = readmemw(easeg, cpu_state.eaaddr + 4);      if (cpu_state.abrt) return 1;
        loadseg(seg, &cpu_state.seg_ss);                  if (cpu_state.abrt) return 1;
        cpu_state.regs[cpu_reg].l = addr;
 
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 2, rmdat, 2,0,0,0, 0);
        return 1;
}
static int opLSS_l_a32(uint32_t fetchdat)
{
        uint32_t addr;
        uint16_t seg;

        fetch_ea_32(fetchdat);
        ILLEGAL_ON(cpu_mod == 3);
        SEG_CHECK_READ(cpu_state.ea_seg);
        addr = readmeml(easeg, cpu_state.eaaddr);
        seg = readmemw(easeg, cpu_state.eaaddr + 4);      if (cpu_state.abrt) return 1;
        loadseg(seg, &cpu_state.seg_ss);                  if (cpu_state.abrt) return 1;
        cpu_state.regs[cpu_reg].l = addr;
 
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 2, rmdat, 2,0,0,0, 1);
        return 1;
}

#define opLsel(name, sel)                                                       \
        static int opL ## name ## _w_a16(uint32_t fetchdat)                     \
        {                                                                       \
                uint16_t addr, seg;                                             \
                                                                                \
                fetch_ea_16(fetchdat);                                          \
                SEG_CHECK_READ(cpu_state.ea_seg);                               \
                ILLEGAL_ON(cpu_mod == 3);                                           \
                addr = readmemw(easeg, cpu_state.eaaddr);                                 \
                seg = readmemw(easeg, cpu_state.eaaddr + 2);      if (cpu_state.abrt) return 1;     \
                loadseg(seg, &sel);                     if (cpu_state.abrt) return 1;     \
                cpu_state.regs[cpu_reg].w = addr;                                   \
                                                                                \
                CLOCK_CYCLES(7);                                                \
                PREFETCH_RUN(7, 2, rmdat, 2,0,0,0, 0); \
                return 0;                                                       \
        }                                                                       \
                                                                                \
        static int opL ## name ## _w_a32(uint32_t fetchdat)                     \
        {                                                                       \
                uint16_t addr, seg;                                             \
                                                                                \
                fetch_ea_32(fetchdat);                                          \
                SEG_CHECK_READ(cpu_state.ea_seg);                               \
                ILLEGAL_ON(cpu_mod == 3);                                           \
                addr = readmemw(easeg, cpu_state.eaaddr);                                 \
                seg = readmemw(easeg, cpu_state.eaaddr + 2);      if (cpu_state.abrt) return 1;     \
                loadseg(seg, &sel);                     if (cpu_state.abrt) return 1;     \
                cpu_state.regs[cpu_reg].w = addr;                                   \
                                                                                \
                CLOCK_CYCLES(7);                                                \
                PREFETCH_RUN(7, 2, rmdat, 2,0,0,0, 1); \
                return 0;                                                       \
        }                                                                       \
                                                                                \
        static int opL ## name ## _l_a16(uint32_t fetchdat)                     \
        {                                                                       \
                uint32_t addr;                                                  \
                uint16_t seg;                                                   \
                                                                                \
                fetch_ea_16(fetchdat);                                          \
                SEG_CHECK_READ(cpu_state.ea_seg);                               \
                ILLEGAL_ON(cpu_mod == 3);                                           \
                addr = readmeml(easeg, cpu_state.eaaddr);                                 \
                seg = readmemw(easeg, cpu_state.eaaddr + 4);      if (cpu_state.abrt) return 1;     \
                loadseg(seg, &sel);                     if (cpu_state.abrt) return 1;     \
                cpu_state.regs[cpu_reg].l = addr;                                   \
                                                                                \
                CLOCK_CYCLES(7);                                                \
                PREFETCH_RUN(7, 2, rmdat, 1,1,0,0, 0); \
                return 0;                                                       \
        }                                                                       \
                                                                                \
        static int opL ## name ## _l_a32(uint32_t fetchdat)                     \
        {                                                                       \
                uint32_t addr;                                                  \
                uint16_t seg;                                                   \
                                                                                \
                fetch_ea_32(fetchdat);                                          \
                SEG_CHECK_READ(cpu_state.ea_seg);                               \
                ILLEGAL_ON(cpu_mod == 3);                                           \
                addr = readmeml(easeg, cpu_state.eaaddr);                                 \
                seg = readmemw(easeg, cpu_state.eaaddr + 4);      if (cpu_state.abrt) return 1;     \
                loadseg(seg, &sel);                     if (cpu_state.abrt) return 1;     \
                cpu_state.regs[cpu_reg].l = addr;                                   \
                                                                                \
                CLOCK_CYCLES(7);                                                \
                PREFETCH_RUN(7, 2, rmdat, 1,1,0,0, 1); \
                return 0;                                                       \
        }
        
opLsel(ES, cpu_state.seg_es)
opLsel(FS, cpu_state.seg_fs)
opLsel(GS, cpu_state.seg_gs)
