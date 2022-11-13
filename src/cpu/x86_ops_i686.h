/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          x86 i686 (Pentium Pro/Pentium II) CPU Instructions.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Copyright 2016-2020 Miran Grca.
 */
static int
opSYSENTER(uint32_t fetchdat)
{
    int ret = sysenter(fetchdat);

    if (ret <= 1) {
	CLOCK_CYCLES(20);
	PREFETCH_RUN(20, 7, -1, 0,0,0,0, 0);
	PREFETCH_FLUSH();
	CPU_BLOCK_END();
    }

    return ret;
}


static int
opSYSEXIT(uint32_t fetchdat)
{
    int ret = sysexit(fetchdat);

    if (ret <= 1) {
	CLOCK_CYCLES(20);
	PREFETCH_RUN(20, 7, -1, 0,0,0,0, 0);
	PREFETCH_FLUSH();
	CPU_BLOCK_END();
    }

    return ret;
}


static int
fx_save_stor_common(uint32_t fetchdat, int bits)
{
    uint8_t fxinst = 0;
    uint16_t twd = x87_gettag();
    uint32_t old_eaaddr = 0;
    uint8_t ftwb = 0;
    uint16_t rec_ftw = 0;
    uint16_t fpus = 0;
    uint64_t *p;

    if (CPUID < 0x650)
	return ILLEGAL(fetchdat);

    FP_ENTER();

    if (bits == 32) {
	fetch_ea_32(fetchdat);
    } else {
	fetch_ea_16(fetchdat);
    }

    if (cpu_state.eaaddr & 0xf) {
	x386_dynarec_log("Effective address %08X not on 16-byte boundary\n", cpu_state.eaaddr);
	x86gpf(NULL, 0);
	return cpu_state.abrt;
    }

    fxinst = (rmdat >> 3) & 7;

    if ((fxinst > 1) || (cpu_mod == 3)) {
	x86illegal();
	return cpu_state.abrt;
    }

    FP_ENTER();

    old_eaaddr = cpu_state.eaaddr;

    if (fxinst == 1) {
	/* FXRSTOR */
	cpu_state.npxc = readmemw(easeg, cpu_state.eaaddr);
	fpus = readmemw(easeg, cpu_state.eaaddr + 2);
	cpu_state.npxc = (cpu_state.npxc & ~FPU_CW_Reserved_Bits) | 0x0040;
	codegen_set_rounding_mode((cpu_state.npxc >> 10) & 3);
	cpu_state.TOP = (fpus >> 11) & 7;
	cpu_state.npxs &= fpus & ~0x3800;

	x87_pc_off = readmeml(easeg, cpu_state.eaaddr+8);
	x87_pc_seg = readmemw(easeg, cpu_state.eaaddr+12);

	ftwb = readmemb(easeg, cpu_state.eaaddr + 4);

	if (ftwb & 0x01)  rec_ftw |= 0x0003;
	if (ftwb & 0x02)  rec_ftw |= 0x000C;
	if (ftwb & 0x04)  rec_ftw |= 0x0030;
	if (ftwb & 0x08)  rec_ftw |= 0x00C0;
	if (ftwb & 0x10)  rec_ftw |= 0x0300;
	if (ftwb & 0x20)  rec_ftw |= 0x0C00;
	if (ftwb & 0x40)  rec_ftw |= 0x3000;
	if (ftwb & 0x80)  rec_ftw |= 0xC000;

	x87_op_off = readmeml(easeg, cpu_state.eaaddr+16);
	x87_op_off |= (readmemw(easeg, cpu_state.eaaddr + 6) >> 12) << 16;
	x87_op_seg = readmemw(easeg, cpu_state.eaaddr+20);

	cpu_state.eaaddr = old_eaaddr + 32;
	x87_ldmmx(&(cpu_state.MM[0]), &(cpu_state.MM_w4[0])); x87_ld_frstor(0);

	cpu_state.eaaddr = old_eaaddr + 48;
	x87_ldmmx(&(cpu_state.MM[1]), &(cpu_state.MM_w4[1])); x87_ld_frstor(1);

	cpu_state.eaaddr = old_eaaddr + 64;
	x87_ldmmx(&(cpu_state.MM[2]), &(cpu_state.MM_w4[2])); x87_ld_frstor(2);

	cpu_state.eaaddr = old_eaaddr + 80;
	x87_ldmmx(&(cpu_state.MM[3]), &(cpu_state.MM_w4[3])); x87_ld_frstor(3);

	cpu_state.eaaddr = old_eaaddr + 96;
	x87_ldmmx(&(cpu_state.MM[4]), &(cpu_state.MM_w4[4])); x87_ld_frstor(4);

	cpu_state.eaaddr = old_eaaddr + 112;
	x87_ldmmx(&(cpu_state.MM[5]), &(cpu_state.MM_w4[5])); x87_ld_frstor(5);

	cpu_state.eaaddr = old_eaaddr + 128;
	x87_ldmmx(&(cpu_state.MM[6]), &(cpu_state.MM_w4[6])); x87_ld_frstor(6);

	cpu_state.eaaddr = old_eaaddr + 144;
	x87_ldmmx(&(cpu_state.MM[7]), &(cpu_state.MM_w4[7])); x87_ld_frstor(7);

	cpu_state.ismmx = 0;
	/*Horrible hack, but as 86Box doesn't keep the FPU stack in 80-bit precision at all times
	  something like this is needed*/
	p = (uint64_t *) cpu_state.tag;
#ifdef USE_NEW_DYNAREC
	if (cpu_state.MM_w4[0] == 0xffff && cpu_state.MM_w4[1] == 0xffff && cpu_state.MM_w4[2] == 0xffff && cpu_state.MM_w4[3] == 0xffff &&
	    cpu_state.MM_w4[4] == 0xffff && cpu_state.MM_w4[5] == 0xffff && cpu_state.MM_w4[6] == 0xffff && cpu_state.MM_w4[7] == 0xffff &&
	    !cpu_state.TOP && (*p == 0x0101010101010101ull))
#else
	if (cpu_state.MM_w4[0] == 0xffff && cpu_state.MM_w4[1] == 0xffff && cpu_state.MM_w4[2] == 0xffff && cpu_state.MM_w4[3] == 0xffff &&
	    cpu_state.MM_w4[4] == 0xffff && cpu_state.MM_w4[5] == 0xffff && cpu_state.MM_w4[6] == 0xffff && cpu_state.MM_w4[7] == 0xffff &&
	    !cpu_state.TOP && !(*p))
#endif
		cpu_state.ismmx = 1;

	x87_settag(rec_ftw);

	CLOCK_CYCLES((cr0 & 1) ? 34 : 44);
    } else {
	/* FXSAVE */
	if ((twd & 0x0003) == 0x0003)  ftwb |= 0x01;
	if ((twd & 0x000C) == 0x000C)  ftwb |= 0x02;
	if ((twd & 0x0030) == 0x0030)  ftwb |= 0x04;
	if ((twd & 0x00C0) == 0x00C0)  ftwb |= 0x08;
	if ((twd & 0x0300) == 0x0300)  ftwb |= 0x10;
	if ((twd & 0x0C00) == 0x0C00)  ftwb |= 0x20;
	if ((twd & 0x3000) == 0x3000)  ftwb |= 0x40;
	if ((twd & 0xC000) == 0xC000)  ftwb |= 0x80;

	writememw(easeg,cpu_state.eaaddr,cpu_state.npxc);
	writememw(easeg,cpu_state.eaaddr+2,cpu_state.npxs);
	writememb(easeg,cpu_state.eaaddr+4,ftwb);

	writememw(easeg,cpu_state.eaaddr+6,(x87_op_off>>16)<<12);
	writememl(easeg,cpu_state.eaaddr+8,x87_pc_off);
	writememw(easeg,cpu_state.eaaddr+12,x87_pc_seg);

	writememl(easeg,cpu_state.eaaddr+16,x87_op_off);
	writememw(easeg,cpu_state.eaaddr+20,x87_op_seg);

	cpu_state.eaaddr = old_eaaddr + 32;
	cpu_state.ismmx ? x87_stmmx(cpu_state.MM[0]) : x87_st_fsave(0);

	cpu_state.eaaddr = old_eaaddr + 48;
	cpu_state.ismmx ? x87_stmmx(cpu_state.MM[1]) : x87_st_fsave(1);

	cpu_state.eaaddr = old_eaaddr + 64;
	cpu_state.ismmx ? x87_stmmx(cpu_state.MM[2]) : x87_st_fsave(2);

	cpu_state.eaaddr = old_eaaddr + 80;
	cpu_state.ismmx ? x87_stmmx(cpu_state.MM[3]) : x87_st_fsave(3);

	cpu_state.eaaddr = old_eaaddr + 96;
	cpu_state.ismmx ? x87_stmmx(cpu_state.MM[4]) : x87_st_fsave(4);

	cpu_state.eaaddr = old_eaaddr + 112;
	cpu_state.ismmx ? x87_stmmx(cpu_state.MM[5]) : x87_st_fsave(5);

	cpu_state.eaaddr = old_eaaddr + 128;
	cpu_state.ismmx ? x87_stmmx(cpu_state.MM[6]) : x87_st_fsave(6);

	cpu_state.eaaddr = old_eaaddr + 144;
	cpu_state.ismmx ? x87_stmmx(cpu_state.MM[7]) : x87_st_fsave(7);

	cpu_state.eaaddr = old_eaaddr;

	cpu_state.npxc = 0x37F;
        codegen_set_rounding_mode(X87_ROUNDING_NEAREST);
	cpu_state.npxs = 0;
	p = (uint64_t *)cpu_state.tag;
#ifdef USE_NEW_DYNAREC
        *p = 0;
#else
        *p = 0x0303030303030303ll;
#endif
	cpu_state.TOP = 0;
	cpu_state.ismmx = 0;

	CLOCK_CYCLES((cr0 & 1) ? 56 : 67);
    }

    return cpu_state.abrt;
}


static int
opFXSAVESTOR_a16(uint32_t fetchdat)
{
    return fx_save_stor_common(fetchdat, 16);
}


static int
opFXSAVESTOR_a32(uint32_t fetchdat)
{
    return fx_save_stor_common(fetchdat, 32);
}


static int
opHINT_NOP_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    CLOCK_CYCLES((is486) ? 1 : 3);
    PREFETCH_RUN(3, 1, -1, 0,0,0,0, 0);
    return 0;
}


static int
opHINT_NOP_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    CLOCK_CYCLES((is486) ? 1 : 3);
    PREFETCH_RUN(3, 1, -1, 0,0,0,0, 0);
    return 0;
}
