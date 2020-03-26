/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		x86 i686 (Pentium Pro/Pentium II) CPU Instructions.
 *
 *
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2016-2020 Miran Grca.
 */

/*	0 = Limit 0-15
	1 = Base 0-15
	2 = Base 16-23 (bits 0-7), Access rights
		8-11	Type
		12	S
		13, 14	DPL
		15	P
	3 = Limit 16-19 (bits 0-3), Base 24-31 (bits 8-15), granularity, etc.
		4	A
		6	DB
		7	G	*/

static void make_seg_data(uint16_t *seg_data, uint32_t base, uint32_t limit, uint8_t type, uint8_t s, uint8_t dpl, uint8_t p, uint8_t g, uint8_t db, uint8_t a)
{
	seg_data[0] = limit & 0xFFFF;
	seg_data[1] = base & 0xFFFF;
	seg_data[2] = ((base >> 16) & 0xFF) | (type << 8) | (p << 15) | (dpl << 13) | (s << 12);
	seg_data[3] = ((limit >> 16) & 0xF) | (a << 4) | (db << 6) | (g << 7) | ((base >> 16) & 0xFF00);
}

static int opSYSENTER(uint32_t fetchdat)
{
	uint16_t sysenter_cs_seg_data[4];
	uint16_t sysenter_ss_seg_data[4];

#ifdef SYSENTER_LOG
	x386_dynarec_log("SYSENTER called\n");
#endif

	if (!(msw & 1))  return internal_illegal("SYSENTER: CPU not in protected mode");
	if (!(cs_msr & 0xFFFC))  return internal_illegal("SYSENTER: CS MSR is zero");

#ifdef SYSENTER_LOG
	x386_dynarec_log("SYSENTER started:\n");
	x386_dynarec_log("CS (%04X): base=%08X, limit=%08X, access=%02X, seg=%04X, limit_low=%08X, limit_high=%08X, checked=%i\n", CS, cpu_state.seg_cs.base, cpu_state.seg_cs.limit, cpu_state.seg_cs.access, cpu_state.seg_cs.seg, cpu_state.seg_cs.limit_low, cpu_state.seg_cs.limit_high, cpu_state.seg_cs.checked);
	x386_dynarec_log("SS (%04X): base=%08X, limit=%08X, access=%02X, seg=%04X, limit_low=%08X, limit_high=%08X, checked=%i\n", SS, cpu_state.seg_ss.base, cpu_state.seg_ss.limit, cpu_state.seg_ss.access, cpu_state.seg_ss.seg, cpu_state.seg_ss.limit_low, cpu_state.seg_ss.limit_high, cpu_state.seg_ss.checked);
	x386_dynarec_log("Model specific registers: cs_msr=%04X, esp_msr=%08X, eip_msr=%08X\n", cs_msr, esp_msr, eip_msr);
	x386_dynarec_log("Other information: eip=%08X esp=%08X cpu_state.eflags=%04X cpu_state.flags=%04X use32=%04X stack32=%i\n", cpu_state.pc, ESP, cpu_state.eflags, cpu_state.flags, use32, stack32);
#endif

	if (cpu_state.abrt)  return 1;

	ESP = esp_msr;
	cpu_state.pc = eip_msr;

        optype = CALL;                                                          \
        cgate16 = cgate32 = 0;                                                  \

	/* Set VM, RF, and IF to 0. */
	cpu_state.eflags &= ~0x0003;
	cpu_state.flags &= ~0x0200;

	CS = (cs_msr & 0xFFFC);
	make_seg_data(sysenter_cs_seg_data, 0, 0xFFFFF, 11, 1, 0, 1, 1, 1, 0);
	do_seg_load(&cpu_state.seg_cs, sysenter_cs_seg_data);
	use32 = 0x300;

	SS = ((cs_msr + 8) & 0xFFFC);
	make_seg_data(sysenter_ss_seg_data, 0, 0xFFFFF, 3, 1, 0, 1, 1, 1, 0);
	do_seg_load(&cpu_state.seg_ss, sysenter_ss_seg_data);
	stack32 = 1;

	cycles -= timing_call_pm;

	optype = 0;

	CPU_BLOCK_END();

#ifdef SYSENTER_LOG
	x386_dynarec_log("SYSENTER completed:\n");
	x386_dynarec_log("CS (%04X): base=%08X, limit=%08X, access=%02X, seg=%04X, limit_low=%08X, limit_high=%08X, checked=%i\n", CS, cpu_state.seg_cs.base, cpu_state.seg_cs.limit, cpu_state.seg_cs.access, cpu_state.seg_cs.seg, cpu_state.seg_cs.limit_low, cpu_state.seg_cs.limit_high, cpu_state.seg_cs.checked);
	x386_dynarec_log("SS (%04X): base=%08X, limit=%08X, access=%02X, seg=%04X, limit_low=%08X, limit_high=%08X, checked=%i\n", SS, cpu_state.seg_ss.base, cpu_state.seg_ss.limit, cpu_state.seg_ss.access, cpu_state.seg_ss.seg, cpu_state.seg_ss.limit_low, cpu_state.seg_ss.limit_high, cpu_state.seg_ss.checked);
	x386_dynarec_log("Model specific registers: cs_msr=%04X, esp_msr=%08X, eip_msr=%08X\n", cs_msr, esp_msr, eip_msr);
	x386_dynarec_log("Other information: eip=%08X esp=%08X cpu_state.eflags=%04X cpu_state.flags=%04X use32=%04X stack32=%i\n", cpu_state.pc, ESP, cpu_state.eflags, cpu_state.flags, use32, stack32);
#endif

	return 0;
}

static int opSYSEXIT(uint32_t fetchdat)
{
	uint16_t sysexit_cs_seg_data[4];
	uint16_t sysexit_ss_seg_data[4];

#ifdef SYSEXIT_LOG
	x386_dynarec_log("SYSEXIT called\n");
#endif

	if (!(cs_msr & 0xFFFC))  return internal_illegal("SYSEXIT: CS MSR is zero");
	if (!(msw & 1))  return internal_illegal("SYSEXIT: CPU not in protected mode");
	if (CPL)  return internal_illegal("SYSEXIT: CPL not 0");

#ifdef SYSEXIT_LOG
	x386_dynarec_log("SYSEXIT start:\n");
	x386_dynarec_log("CS (%04X): base=%08X, limit=%08X, access=%02X, seg=%04X, limit_low=%08X, limit_high=%08X, checked=%i\n", CS, cpu_state.seg_cs.base, cpu_state.seg_cs.limit, cpu_state.seg_cs.access, cpu_state.seg_cs.seg, cpu_state.seg_cs.limit_low, cpu_state.seg_cs.limit_high, cpu_state.seg_cs.checked);
	x386_dynarec_log("SS (%04X): base=%08X, limit=%08X, access=%02X, seg=%04X, limit_low=%08X, limit_high=%08X, checked=%i\n", SS, cpu_state.seg_ss.base, cpu_state.seg_ss.limit, cpu_state.seg_ss.access, cpu_state.seg_ss.seg, cpu_state.seg_ss.limit_low, cpu_state.seg_ss.limit_high, cpu_state.seg_ss.checked);
	x386_dynarec_log("Model specific registers: cs_msr=%04X, esp_msr=%08X, eip_msr=%08X\n", cs_msr, esp_msr, eip_msr);
	x386_dynarec_log("Other information: eip=%08X esp=%08X cpu_state.eflags=%04X cpu_state.flags=%04X use32=%04X stack32=%i ECX=%08X EDX=%08X\n", cpu_state.pc, ESP, cpu_state.eflags, cpu_state.flags, use32, stack32, ECX, EDX);
#endif

	if (cpu_state.abrt)  return 1;

	ESP = ECX;
	cpu_state.pc = EDX;

        optype = CALL;                                                          \
        cgate16 = cgate32 = 0;                                                  \

	CS = ((cs_msr + 16) & 0xFFFC) | 3;
	make_seg_data(sysexit_cs_seg_data, 0, 0xFFFFF, 11, 1, 3, 1, 1, 1, 0);
	do_seg_load(&cpu_state.seg_cs, sysexit_cs_seg_data);
	use32 = 0x300;

	SS = CS + 8;
	make_seg_data(sysexit_ss_seg_data, 0, 0xFFFFF, 3, 1, 3, 1, 1, 1, 0);
	do_seg_load(&cpu_state.seg_ss, sysexit_ss_seg_data);
	stack32 = 1;

	flushmmucache_cr3();

	cycles -= timing_call_pm;

	optype = 0;

	CPU_BLOCK_END();

#ifdef SYSEXIT_LOG
	x386_dynarec_log("SYSEXIT completed:\n");
	x386_dynarec_log("CS (%04X): base=%08X, limit=%08X, access=%02X, seg=%04X, limit_low=%08X, limit_high=%08X, checked=%i\n", CS, cpu_state.seg_cs.base, cpu_state.seg_cs.limit, cpu_state.seg_cs.access, cpu_state.seg_cs.seg, cpu_state.seg_cs.limit_low, cpu_state.seg_cs.limit_high, cpu_state.seg_cs.checked);
	x386_dynarec_log("SS (%04X): base=%08X, limit=%08X, access=%02X, seg=%04X, limit_low=%08X, limit_high=%08X, checked=%i\n", SS, cpu_state.seg_ss.base, cpu_state.seg_ss.limit, cpu_state.seg_ss.access, cpu_state.seg_ss.seg, cpu_state.seg_ss.limit_low, cpu_state.seg_ss.limit_high, cpu_state.seg_ss.checked);
	x386_dynarec_log("Model specific registers: cs_msr=%04X, esp_msr=%08X, eip_msr=%08X\n", cs_msr, esp_msr, eip_msr);
	x386_dynarec_log("Other information: eip=%08X esp=%08X cpu_state.eflags=%04X cpu_state.flags=%04X use32=%04X stack32=%i ECX=%08X EDX=%08X\n", cpu_state.pc, ESP, cpu_state.eflags, cpu_state.flags, use32, stack32, ECX, EDX);
#endif

	return 0;
}

static int opFXSAVESTOR_a16(uint32_t fetchdat)
{
	uint8_t fxinst = 0;
	uint16_t twd = x87_gettag();
	uint16_t old_eaaddr = 0;
	uint8_t ftwb = 0;
	uint16_t rec_ftw = 0;
	uint16_t fpus = 0;
	uint64_t *p;

	if (CPUID < 0x650)  return ILLEGAL(fetchdat);

	FP_ENTER();

	fetch_ea_16(fetchdat);

	if (cpu_state.eaaddr & 0xf)
	{
		x386_dynarec_log("Effective address %04X not on 16-byte boundary\n", cpu_state.eaaddr);
		x86gpf(NULL, 0);
		return cpu_state.abrt;
	}

	fxinst = (rmdat >> 3) & 7;

	if ((fxinst > 1) || (cpu_mod == 3))
	{
		x86illegal();
		return cpu_state.abrt;
	}

	FP_ENTER();

	old_eaaddr = cpu_state.eaaddr;

	if (fxinst == 1)
	{
		/* FXRSTOR */
		cpu_state.npxc = readmemw(easeg, cpu_state.eaaddr);
#ifdef USE_NEW_DYNAREC
                codegen_set_rounding_mode((cpu_state.npxc >> 10) & 3);
#endif
		fpus = readmemw(easeg, cpu_state.eaaddr + 2);
		cpu_state.npxc = (cpu_state.npxc & ~FPU_CW_Reserved_Bits) | 0x0040;
#ifdef USE_NEW_DYNAREC
                codegen_set_rounding_mode((cpu_state.npxc >> 10) & 3);
#endif
		cpu_state.TOP = (fpus >> 11) & 7;
		cpu_state.npxs &= fpus & ~0x3800;

		/* foo = readmemw(easeg, cpu_state.eaaddr + 6) & 0x7FF; */

               	x87_pc_off = readmeml(easeg, cpu_state.eaaddr+8);
                x87_pc_seg = readmemw(easeg, cpu_state.eaaddr+12);
		/* if (cr0 & 1)
		{
			x87_pc_seg &= 0xFFFC;
			x87_pc_seg |= ((cpu_state.seg_cs.access >> 5) & 3);
		} */

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
		/* if (cr0 & 1)
		{
			x87_op_seg &= 0xFFFC;
			x87_op_seg |= ((_ds.access >> 5) & 3);
		} */

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
	        /*Horrible hack, but as PCem doesn't keep the FPU stack in 80-bit precision at all times
	          something like this is needed*/
		p = (uint64_t *)cpu_state.tag;
		if (cpu_state.MM_w4[0] == 0xffff && cpu_state.MM_w4[1] == 0xffff && cpu_state.MM_w4[2] == 0xffff && cpu_state.MM_w4[3] == 0xffff &&
			cpu_state.MM_w4[4] == 0xffff && cpu_state.MM_w4[5] == 0xffff && cpu_state.MM_w4[6] == 0xffff && cpu_state.MM_w4[7] == 0xffff &&
			!cpu_state.TOP && !(*p))
	        cpu_state.ismmx = 1;

		x87_settag(rec_ftw);

	        CLOCK_CYCLES((cr0 & 1) ? 34 : 44);

		if(cpu_state.abrt)  x386_dynarec_log("FXRSTOR: abrt != 0\n");
	}
	else
	{
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
#ifdef USE_NEW_DYNAREC
                codegen_set_rounding_mode((cpu_state.npxc >> 10) & 3);
#endif
	        cpu_state.new_npxc = (cpu_state.old_npxc & ~0xc00);
        	cpu_state.npxs = 0;
		p = (uint64_t *)cpu_state.tag;
	        *p = 0x0303030303030303ll;
        	cpu_state.TOP = 0;
	        cpu_state.ismmx = 0;

		CLOCK_CYCLES((cr0 & 1) ? 56 : 67);

		if(cpu_state.abrt)  x386_dynarec_log("FXSAVE: abrt != 0\n");
	}

	return cpu_state.abrt;
}

static int opFXSAVESTOR_a32(uint32_t fetchdat)
{
	uint8_t fxinst = 0;
	uint16_t twd = x87_gettag();
	uint32_t old_eaaddr = 0;
	uint8_t ftwb = 0;
	uint16_t rec_ftw = 0;
	uint16_t fpus = 0;
	uint64_t *p;

	if (CPUID < 0x650)  return ILLEGAL(fetchdat);

	FP_ENTER();

	fetch_ea_32(fetchdat);

	if (cpu_state.eaaddr & 0xf)
	{
		x386_dynarec_log("Effective address %08X not on 16-byte boundary\n", cpu_state.eaaddr);
		x86gpf(NULL, 0);
		return cpu_state.abrt;
	}

	fxinst = (rmdat >> 3) & 7;

	if ((fxinst > 1) || (cpu_mod == 3))
	{
		x86illegal();
		return cpu_state.abrt;
	}

	FP_ENTER();

	old_eaaddr = cpu_state.eaaddr;

	if (fxinst == 1)
	{
		/* FXRSTOR */
		cpu_state.npxc = readmemw(easeg, cpu_state.eaaddr);
#ifdef USE_NEW_DYNAREC
                codegen_set_rounding_mode((cpu_state.npxc >> 10) & 3);
#endif
		fpus = readmemw(easeg, cpu_state.eaaddr + 2);
		cpu_state.npxc = (cpu_state.npxc & ~FPU_CW_Reserved_Bits) | 0x0040;
#ifdef USE_NEW_DYNAREC
                codegen_set_rounding_mode((cpu_state.npxc >> 10) & 3);
#endif
		cpu_state.TOP = (fpus >> 11) & 7;
		cpu_state.npxs &= fpus & ~0x3800;

		/* foo = readmemw(easeg, cpu_state.eaaddr + 6) & 0x7FF; */

               	x87_pc_off = readmeml(easeg, cpu_state.eaaddr+8);
                x87_pc_seg = readmemw(easeg, cpu_state.eaaddr+12);
		/* if (cr0 & 1)
		{
			x87_pc_seg &= 0xFFFC;
			x87_pc_seg |= ((cpu_state.seg_cs.access >> 5) & 3);
		} */

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
		/* if (cr0 & 1)
		{
			x87_op_seg &= 0xFFFC;
			x87_op_seg |= ((_ds.access >> 5) & 3);
		} */

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
	        /*Horrible hack, but as PCem doesn't keep the FPU stack in 80-bit precision at all times
	          something like this is needed*/
		p = (uint64_t *)cpu_state.tag;
		if (cpu_state.MM_w4[0] == 0xffff && cpu_state.MM_w4[1] == 0xffff && cpu_state.MM_w4[2] == 0xffff && cpu_state.MM_w4[3] == 0xffff &&
			cpu_state.MM_w4[4] == 0xffff && cpu_state.MM_w4[5] == 0xffff && cpu_state.MM_w4[6] == 0xffff && cpu_state.MM_w4[7] == 0xffff &&
			!cpu_state.TOP && !(*p))
	        cpu_state.ismmx = 1;

		x87_settag(rec_ftw);

	        CLOCK_CYCLES((cr0 & 1) ? 34 : 44);

		if(cpu_state.abrt)  x386_dynarec_log("FXRSTOR: abrt != 0\n");
	}
	else
	{
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
#ifdef USE_NEW_DYNAREC
                codegen_set_rounding_mode((cpu_state.npxc >> 10) & 3);
#endif
	        cpu_state.new_npxc = (cpu_state.old_npxc & ~0xc00);
        	cpu_state.npxs = 0;
		p = (uint64_t *)cpu_state.tag;
	        *p = 0x0303030303030303ll;
        	cpu_state.TOP = 0;
	        cpu_state.ismmx = 0;

		CLOCK_CYCLES((cr0 & 1) ? 56 : 67);

		if(cpu_state.abrt)  x386_dynarec_log("FXSAVE: abrt != 0\n");
	}

	return cpu_state.abrt;
}
