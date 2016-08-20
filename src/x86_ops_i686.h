/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
static int internal_illegal(char *s)
{
	cpu_state.pc = cpu_state.oldpc;
	x86gpf(s, 0);
	return abrt;
}

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

	pclog("SYSENTER called\n");

	if (!(cr0 & 1))  return internal_illegal("SYSENTER: CPU not in protected mode");
	if (!(cs_msr & 0xFFFC))  return internal_illegal("SYSENTER: CS MSR is zero");

	pclog("SYSENTER started:\n");
	pclog("CS (%04X): base=%08X, limit=%08X, access=%02X, seg=%04X, limit_low=%08X, limit_high=%08X, checked=%i\n", CS, _cs.base, _cs.limit, _cs.access, _cs.seg, _cs.limit_low, _cs.limit_high, _cs.checked);
	pclog("SS (%04X): base=%08X, limit=%08X, access=%02X, seg=%04X, limit_low=%08X, limit_high=%08X, checked=%i\n", SS, _ss.base, _ss.limit, _ss.access, _ss.seg, _ss.limit_low, _ss.limit_high, _ss.checked);
	pclog("Model specific registers: cs_msr=%04X, esp_msr=%08X, eip_msr=%08X\n", cs_msr, esp_msr, eip_msr);
	pclog("Other information: eip=%08X esp=%08X eflags=%04X flags=%04X use32=%04X stack32=%i\n", cpu_state.pc, ESP, eflags, flags, use32, stack32);

	if (abrt)  return 1;

	ESP = esp_msr;
	cpu_state.pc = eip_msr;

        optype = CALL;                                                          \
        cgate16 = cgate32 = 0;                                                  \

	/* Set VM, RF, and IF to 0. */
	eflags &= ~0x0003;
	flags &= ~0x0200;

	CS = (cs_msr & 0xFFFC);
	make_seg_data(sysenter_cs_seg_data, 0, 0xFFFFF, 11, 1, 0, 1, 1, 1, 0);
	do_seg_load(&_cs, sysenter_cs_seg_data);
	use32 = 0x300;

	SS = ((cs_msr + 8) & 0xFFFC);
	make_seg_data(sysenter_ss_seg_data, 0, 0xFFFFF, 3, 1, 0, 1, 1, 1, 0);
	do_seg_load(&_ss, sysenter_ss_seg_data);
	stack32 = 1;

	cycles -= timing_call_pm;

	optype = 0;

	CPU_BLOCK_END();

	pclog("SYSENTER completed:\n");
	pclog("CS (%04X): base=%08X, limit=%08X, access=%02X, seg=%04X, limit_low=%08X, limit_high=%08X, checked=%i\n", CS, _cs.base, _cs.limit, _cs.access, _cs.seg, _cs.limit_low, _cs.limit_high, _cs.checked);
	pclog("SS (%04X): base=%08X, limit=%08X, access=%02X, seg=%04X, limit_low=%08X, limit_high=%08X, checked=%i\n", SS, _ss.base, _ss.limit, _ss.access, _ss.seg, _ss.limit_low, _ss.limit_high, _ss.checked);
	pclog("Model specific registers: cs_msr=%04X, esp_msr=%08X, eip_msr=%08X\n", cs_msr, esp_msr, eip_msr);
	pclog("Other information: eip=%08X esp=%08X eflags=%04X flags=%04X use32=%04X stack32=%i\n", cpu_state.pc, ESP, eflags, flags, use32, stack32);

	return 0;
}

static int opSYSEXIT(uint32_t fetchdat)
{
	uint16_t sysexit_cs_seg_data[4];
	uint16_t sysexit_ss_seg_data[4];

	pclog("SYSEXIT called\n");

	if (!(cs_msr & 0xFFFC))  return internal_illegal("SYSEXIT: CS MSR is zero");
	if (!(cr0 & 1))  return internal_illegal("SYSEXIT: CPU not in protected mode");
	if (CS & 3)  return internal_illegal("SYSEXIT: CPL not 0");

	pclog("SYSEXIT start:\n");
	pclog("CS (%04X): base=%08X, limit=%08X, access=%02X, seg=%04X, limit_low=%08X, limit_high=%08X, checked=%i\n", CS, _cs.base, _cs.limit, _cs.access, _cs.seg, _cs.limit_low, _cs.limit_high, _cs.checked);
	pclog("SS (%04X): base=%08X, limit=%08X, access=%02X, seg=%04X, limit_low=%08X, limit_high=%08X, checked=%i\n", SS, _ss.base, _ss.limit, _ss.access, _ss.seg, _ss.limit_low, _ss.limit_high, _ss.checked);
	pclog("Model specific registers: cs_msr=%04X, esp_msr=%08X, eip_msr=%08X\n", cs_msr, esp_msr, eip_msr);
	pclog("Other information: eip=%08X esp=%08X eflags=%04X flags=%04X use32=%04X stack32=%i ECX=%08X EDX=%08X\n", cpu_state.pc, ESP, eflags, flags, use32, stack32, ECX, EDX);

	if (abrt)  return 1;

	ESP = ECX;
	cpu_state.pc = EDX;

        optype = CALL;                                                          \
        cgate16 = cgate32 = 0;                                                  \

	CS = ((cs_msr + 16) & 0xFFFC) | 3;
	make_seg_data(sysexit_cs_seg_data, 0, 0xFFFFF, 11, 1, 3, 1, 1, 1, 0);
	do_seg_load(&_cs, sysexit_cs_seg_data);
	use32 = 0x300;

	SS = CS + 8;
	make_seg_data(sysexit_ss_seg_data, 0, 0xFFFFF, 3, 1, 3, 1, 1, 1, 0);
	do_seg_load(&_ss, sysexit_ss_seg_data);
	stack32 = 1;

	flushmmucache_cr3();

	cycles -= timing_call_pm;

	optype = 0;

	CPU_BLOCK_END();

	pclog("SYSEXIT completed:\n");
	pclog("CS (%04X): base=%08X, limit=%08X, access=%02X, seg=%04X, limit_low=%08X, limit_high=%08X, checked=%i\n", CS, _cs.base, _cs.limit, _cs.access, _cs.seg, _cs.limit_low, _cs.limit_high, _cs.checked);
	pclog("SS (%04X): base=%08X, limit=%08X, access=%02X, seg=%04X, limit_low=%08X, limit_high=%08X, checked=%i\n", SS, _ss.base, _ss.limit, _ss.access, _ss.seg, _ss.limit_low, _ss.limit_high, _ss.checked);
	pclog("Model specific registers: cs_msr=%04X, esp_msr=%08X, eip_msr=%08X\n", cs_msr, esp_msr, eip_msr);
	pclog("Other information: eip=%08X esp=%08X eflags=%04X flags=%04X use32=%04X stack32=%i ECX=%08X EDX=%08X\n", cpu_state.pc, ESP, eflags, flags, use32, stack32, ECX, EDX);

	return 0;
}

static int opFXSAVESTOR_a16(uint32_t fetchdat)
{
	uint8_t fxinst = 0;
	uint16_t twd = x87_gettag();
	uint16_t old_eaaddr = 0;
	int old_ismmx = ismmx;
	uint8_t ftwb = 0;
	uint16_t rec_ftw = 0;
	uint16_t fpus = 0;

	if (CPUID < 0x650)  return ILLEGAL(fetchdat);

	FP_ENTER();

	fetch_ea_16(fetchdat);

	if (cpu_state.eaaddr & 0xf)
	{
		pclog("Effective address %04X not on 16-byte boundary\n", cpu_state.eaaddr);
		x86gpf(NULL, 0);
		return abrt;
	}

	fxinst = (rmdat >> 3) & 7;

	if ((fxinst > 1) || (cpu_mod == 3))
	{
		// if (fxinst > 1)  pclog("FX instruction is: %02X\n", fxinst);
		// if (cpu_mod == 3)  pclog("MOD is 3\n");

		x86illegal();
		return abrt;
	}

	FP_ENTER();

	old_eaaddr = cpu_state.eaaddr;

	if (fxinst == 1)
	{
		/* FXRSTOR */
		// pclog("FXRSTOR issued\n");

		npxc = readmemw(easeg, cpu_state.eaaddr);
		fpus = readmemw(easeg, cpu_state.eaaddr + 2);
		npxc = (npxc & ~FPU_CW_Reserved_Bits) | 0x0040;
		TOP = (fpus >> 11) & 7;
		npxs &= fpus & ~0x3800;

		/* foo = readmemw(easeg, cpu_state.eaaddr + 6) & 0x7FF; */

               	x87_pc_off = readmeml(easeg, cpu_state.eaaddr+8);
                x87_pc_seg = readmemw(easeg, cpu_state.eaaddr+12);
		/* if (cr0 & 1)
		{
			x87_pc_seg &= 0xFFFC;
			x87_pc_seg |= ((_cs.access >> 5) & 3);
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
		x87_ldmmx(&MM[0]); x87_ld_frstor(0);

		cpu_state.eaaddr = old_eaaddr + 48;
		x87_ldmmx(&MM[1]); x87_ld_frstor(1);

		cpu_state.eaaddr = old_eaaddr + 64;
		x87_ldmmx(&MM[2]); x87_ld_frstor(2);

		cpu_state.eaaddr = old_eaaddr + 80;
		x87_ldmmx(&MM[3]); x87_ld_frstor(3);

		cpu_state.eaaddr = old_eaaddr + 96;
		x87_ldmmx(&MM[4]); x87_ld_frstor(4);

		cpu_state.eaaddr = old_eaaddr + 112;
		x87_ldmmx(&MM[5]); x87_ld_frstor(5);

		cpu_state.eaaddr = old_eaaddr + 128;
		x87_ldmmx(&MM[6]); x87_ld_frstor(6);

		cpu_state.eaaddr = old_eaaddr + 144;
		x87_ldmmx(&MM[7]); x87_ld_frstor(7);

	        ismmx = 0;
	        /*Horrible hack, but as PCem doesn't keep the FPU stack in 80-bit precision at all times
	          something like this is needed*/
	        if (MM[0].w[4] == 0xffff && MM[1].w[4] == 0xffff && MM[2].w[4] == 0xffff && MM[3].w[4] == 0xffff &&
	            MM[4].w[4] == 0xffff && MM[5].w[4] == 0xffff && MM[6].w[4] == 0xffff && MM[7].w[4] == 0xffff &&
       		    !TOP && !(*(uint64_t *)tag))
	        ismmx = old_ismmx;

		x87_settag(rec_ftw);

	        CLOCK_CYCLES((cr0 & 1) ? 34 : 44);

		if(abrt)  pclog("FXRSTOR: abrt != 0\n");
	}
	else
	{
		/* FXSAVE */
		// pclog("FXSAVE issued\n");

		if (twd & 0x0003 == 0x0003)  ftwb |= 0x01;
		if (twd & 0x000C == 0x000C)  ftwb |= 0x02;
		if (twd & 0x0030 == 0x0030)  ftwb |= 0x04;
		if (twd & 0x00C0 == 0x00C0)  ftwb |= 0x08;
		if (twd & 0x0300 == 0x0300)  ftwb |= 0x10;
		if (twd & 0x0C00 == 0x0C00)  ftwb |= 0x20;
		if (twd & 0x3000 == 0x3000)  ftwb |= 0x40;
		if (twd & 0xC000 == 0xC000)  ftwb |= 0x80;

                writememw(easeg,cpu_state.eaaddr,npxc);
                writememw(easeg,cpu_state.eaaddr+2,npxs);
                writememb(easeg,cpu_state.eaaddr+4,ftwb);

                writememw(easeg,cpu_state.eaaddr+6,(x87_op_off>>16)<<12);
               	writememl(easeg,cpu_state.eaaddr+8,x87_pc_off);
                writememw(easeg,cpu_state.eaaddr+12,x87_pc_seg);

                writememl(easeg,cpu_state.eaaddr+16,x87_op_off);
                writememw(easeg,cpu_state.eaaddr+20,x87_op_seg);

		cpu_state.eaaddr = old_eaaddr + 32;
		ismmx ? x87_stmmx(MM[0]) : x87_st_fsave(0);

		cpu_state.eaaddr = old_eaaddr + 48;
		ismmx ? x87_stmmx(MM[1]) : x87_st_fsave(1);

		cpu_state.eaaddr = old_eaaddr + 64;
		ismmx ? x87_stmmx(MM[2]) : x87_st_fsave(2);

		cpu_state.eaaddr = old_eaaddr + 80;
		ismmx ? x87_stmmx(MM[3]) : x87_st_fsave(3);

		cpu_state.eaaddr = old_eaaddr + 96;
		ismmx ? x87_stmmx(MM[4]) : x87_st_fsave(4);

		cpu_state.eaaddr = old_eaaddr + 112;
		ismmx ? x87_stmmx(MM[5]) : x87_st_fsave(5);

		cpu_state.eaaddr = old_eaaddr + 128;
		ismmx ? x87_stmmx(MM[6]) : x87_st_fsave(6);

		cpu_state.eaaddr = old_eaaddr + 144;
		ismmx ? x87_stmmx(MM[7]) : x87_st_fsave(7);

		cpu_state.eaaddr = old_eaaddr;

		CLOCK_CYCLES((cr0 & 1) ? 56 : 67);

		if(abrt)  pclog("FXSAVE: abrt != 0\n");
	}

	return abrt;
}

static int opFXSAVESTOR_a32(uint32_t fetchdat)
{
	uint8_t fxinst = 0;
	uint16_t twd = x87_gettag();
	uint32_t old_eaaddr = 0;
	int old_ismmx = ismmx;
	uint8_t ftwb = 0;
	uint16_t rec_ftw = 0;
	uint16_t fpus = 0;

	if (CPUID < 0x650)  return ILLEGAL(fetchdat);

	FP_ENTER();

	fetch_ea_32(fetchdat);

	if (cpu_state.eaaddr & 0xf)
	{
		pclog("Effective address %08X not on 16-byte boundary\n", cpu_state.eaaddr);
		x86gpf(NULL, 0);
		return abrt;
	}

	fxinst = (rmdat >> 3) & 7;

	if ((fxinst > 1) || (cpu_mod == 3))
	{
		// if (fxinst > 1)  pclog("FX instruction is: %02X\n", fxinst);
		// if (cpu_mod == 3)  pclog("MOD is 3\n");

		x86illegal();
		return abrt;
	}

	FP_ENTER();

	old_eaaddr = cpu_state.eaaddr;

	if (fxinst == 1)
	{
		/* FXRSTOR */
		// pclog("FXRSTOR issued\n");

		npxc = readmemw(easeg, cpu_state.eaaddr);
		fpus = readmemw(easeg, cpu_state.eaaddr + 2);
		npxc = (npxc & ~FPU_CW_Reserved_Bits) | 0x0040;
		TOP = (fpus >> 11) & 7;
		npxs &= fpus & ~0x3800;

		/* foo = readmemw(easeg, cpu_state.eaaddr + 6) & 0x7FF; */

               	x87_pc_off = readmeml(easeg, cpu_state.eaaddr+8);
                x87_pc_seg = readmemw(easeg, cpu_state.eaaddr+12);
		/* if (cr0 & 1)
		{
			x87_pc_seg &= 0xFFFC;
			x87_pc_seg |= ((_cs.access >> 5) & 3);
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
		x87_ldmmx(&MM[0]); x87_ld_frstor(0);

		cpu_state.eaaddr = old_eaaddr + 48;
		x87_ldmmx(&MM[1]); x87_ld_frstor(1);

		cpu_state.eaaddr = old_eaaddr + 64;
		x87_ldmmx(&MM[2]); x87_ld_frstor(2);

		cpu_state.eaaddr = old_eaaddr + 80;
		x87_ldmmx(&MM[3]); x87_ld_frstor(3);

		cpu_state.eaaddr = old_eaaddr + 96;
		x87_ldmmx(&MM[4]); x87_ld_frstor(4);

		cpu_state.eaaddr = old_eaaddr + 112;
		x87_ldmmx(&MM[5]); x87_ld_frstor(5);

		cpu_state.eaaddr = old_eaaddr + 128;
		x87_ldmmx(&MM[6]); x87_ld_frstor(6);

		cpu_state.eaaddr = old_eaaddr + 144;
		x87_ldmmx(&MM[7]); x87_ld_frstor(7);

	        ismmx = 0;
	        /*Horrible hack, but as PCem doesn't keep the FPU stack in 80-bit precision at all times
	          something like this is needed*/
	        if (MM[0].w[4] == 0xffff && MM[1].w[4] == 0xffff && MM[2].w[4] == 0xffff && MM[3].w[4] == 0xffff &&
	            MM[4].w[4] == 0xffff && MM[5].w[4] == 0xffff && MM[6].w[4] == 0xffff && MM[7].w[4] == 0xffff &&
       		    !TOP && !(*(uint64_t *)tag))
	        ismmx = old_ismmx;

		x87_settag(rec_ftw);

	        CLOCK_CYCLES((cr0 & 1) ? 34 : 44);

		if(abrt)  pclog("FXRSTOR: abrt != 0\n");
	}
	else
	{
		/* FXSAVE */
		// pclog("FXSAVE issued\n");

		if (twd & 0x0003 == 0x0003)  ftwb |= 0x01;
		if (twd & 0x000C == 0x000C)  ftwb |= 0x02;
		if (twd & 0x0030 == 0x0030)  ftwb |= 0x04;
		if (twd & 0x00C0 == 0x00C0)  ftwb |= 0x08;
		if (twd & 0x0300 == 0x0300)  ftwb |= 0x10;
		if (twd & 0x0C00 == 0x0C00)  ftwb |= 0x20;
		if (twd & 0x3000 == 0x3000)  ftwb |= 0x40;
		if (twd & 0xC000 == 0xC000)  ftwb |= 0x80;

                writememw(easeg,cpu_state.eaaddr,npxc);
                writememw(easeg,cpu_state.eaaddr+2,npxs);
                writememb(easeg,cpu_state.eaaddr+4,ftwb);

                writememw(easeg,cpu_state.eaaddr+6,(x87_op_off>>16)<<12);
               	writememl(easeg,cpu_state.eaaddr+8,x87_pc_off);
                writememw(easeg,cpu_state.eaaddr+12,x87_pc_seg);

                writememl(easeg,cpu_state.eaaddr+16,x87_op_off);
                writememw(easeg,cpu_state.eaaddr+20,x87_op_seg);

		cpu_state.eaaddr = old_eaaddr + 32;
		ismmx ? x87_stmmx(MM[0]) : x87_st_fsave(0);

		cpu_state.eaaddr = old_eaaddr + 48;
		ismmx ? x87_stmmx(MM[1]) : x87_st_fsave(1);

		cpu_state.eaaddr = old_eaaddr + 64;
		ismmx ? x87_stmmx(MM[2]) : x87_st_fsave(2);

		cpu_state.eaaddr = old_eaaddr + 80;
		ismmx ? x87_stmmx(MM[3]) : x87_st_fsave(3);

		cpu_state.eaaddr = old_eaaddr + 96;
		ismmx ? x87_stmmx(MM[4]) : x87_st_fsave(4);

		cpu_state.eaaddr = old_eaaddr + 112;
		ismmx ? x87_stmmx(MM[5]) : x87_st_fsave(5);

		cpu_state.eaaddr = old_eaaddr + 128;
		ismmx ? x87_stmmx(MM[6]) : x87_st_fsave(6);

		cpu_state.eaaddr = old_eaaddr + 144;
		ismmx ? x87_stmmx(MM[7]) : x87_st_fsave(7);

		cpu_state.eaaddr = old_eaaddr;

		CLOCK_CYCLES((cr0 & 1) ? 56 : 67);

		if(abrt)  pclog("FXSAVE: abrt != 0\n");
	}

	return abrt;
}

#define AMD_SYSCALL_EIP	(star & 0xFFFFFFFF)
#define AMD_SYSCALL_SB	((star >> 32) & 0xFFFF)
#define AMD_SYSRET_SB	((star >> 48) & 0xFFFF)

/* 0F 05 */
static int opSYSCALL(uint32_t fetchdat)
{
	uint16_t syscall_cs_seg_data[4] = {0, 0, 0, 0};
	uint16_t syscall_ss_seg_data[4] = {0, 0, 0, 0};

	if (!(cr0 & 1))  return internal_illegal("SYSCALL: CPU not in protected mode");
	if (!AMD_SYSCALL_SB)  return internal_illegal("SYSCALL: AMD SYSCALL SB MSR is zero");

	/* Set VM, IF, RF to 0. */
	/* eflags &= ~0x00030200;
	flags &= ~0x0200; */

	/* Let's do this by the AMD spec. */
	ECX = cpu_state.pc;
	cpu_state.pc = AMD_SYSCALL_EIP;

	eflags &= ~0x0002;
	flags &= ~0x0200;

	/* CS */
	_cs.seg = AMD_SYSCALL_SB & ~7;
	if (cs_msr & 4)
	{
		if (_cs.seg >= ldt.limit)
		{
			pclog("Bigger than LDT limit %04X %04X CS\n",cs_msr,ldt.limit);
			x86gpf(NULL, cs_msr & ~3);
			return 1;
		}
		_cs.seg +=ldt.base;
	}
	else
	{
		if (_cs.seg >= gdt.limit)
		{
			pclog("Bigger than GDT limit %04X %04X CS\n",cs_msr,gdt.limit);
			x86gpf(NULL, cs_msr & ~3);
			return 1;
		}
		_cs.seg += gdt.base;
	}
	cpl_override = 1;

	syscall_cs_seg_data[0] = 0xFFFF;
	syscall_cs_seg_data[1] = 0;
	syscall_cs_seg_data[2] = 0x9B00;
	syscall_cs_seg_data[3] = 0xC0;

	cpl_override = 0;

	use32 = 0x300;
	CS = (AMD_SYSCALL_SB & ~3) | 0;

	do_seg_load(&_cs, syscall_cs_seg_data);
	use32 = 0x300;

	CS = (CS & 0xFFFC) | 0;

	_cs.limit = 0xFFFFFFFF;
	_cs.limit_high = 0xFFFFFFFF;

	/* SS */
	syscall_ss_seg_data[0] = 0xFFFF;
	syscall_ss_seg_data[1] = 0;
	syscall_ss_seg_data[2] = 0x9300;
	syscall_ss_seg_data[3] = 0xC0;
	do_seg_load(&_ss, syscall_ss_seg_data);
	_ss.seg = (AMD_SYSCALL_SB + 8) & 0xFFFC;
	stack32 = 1;

	_ss.limit = 0xFFFFFFFF;
	_ss.limit_high = 0xFFFFFFFF;

	_ss.checked = 0;

	cpu_state.pc = eip_msr;

	CLOCK_CYCLES(20);

	CPU_BLOCK_END();

	/* pclog("SYSCALL completed:\n");
	pclog("CS (%04X): base=%08X, limit=%08X, access=%02X, seg=%04X, limit_low=%08X, limit_high=%08X, checked=%i\n", CS, _cs.base, _cs.limit, _cs.access, _cs.seg, _cs.limit_low, _cs.limit_high, _cs.checked);
	pclog("SS (%04X): base=%08X, limit=%08X, access=%02X, seg=%04X, limit_low=%08X, limit_high=%08X, checked=%i\n", SS, _ss.base, _ss.limit, _ss.access, _ss.seg, _ss.limit_low, _ss.limit_high, _ss.checked);
	pclog("Model specific registers: cs_msr=%04X, esp_msr=%08X, eip_msr=%08X\n", cs_msr, esp_msr, eip_msr);
	pclog("Other information: eflags=%04X flags=%04X use32=%04X stack32=%i\n", eflags, flags, use32, stack32); */

	return 0;
}

/* 0F 07 */
static int opSYSRET(uint32_t fetchdat)
{
	uint16_t sysret_cs_seg_data[4] = {0, 0, 0, 0};
	uint16_t sysret_ss_seg_data[4] = {0, 0, 0, 0};

	if (!cs_msr)  return internal_illegal("SYSRET: CS MSR is zero");
	if (!(cr0 & 1))  return internal_illegal("SYSRET: CPU not in protected mode");

	cpu_state.pc = ECX;

	eflags |= (1 << 1);

	/* CS */
	_cs.seg = AMD_SYSRET_SB & ~7;
	if (cs_msr & 4)
	{
		if (_cs.seg >= ldt.limit)
		{
			pclog("Bigger than LDT limit %04X %04X CS\n",cs_msr,ldt.limit);
			x86gpf(NULL, cs_msr & ~3);
			return 1;
		}
		_cs.seg +=ldt.base;
	}
	else
	{
		if (_cs.seg >= gdt.limit)
		{
			pclog("Bigger than GDT limit %04X %04X CS\n",cs_msr,gdt.limit);
			x86gpf(NULL, cs_msr & ~3);
			return 1;
		}
		_cs.seg += gdt.base;
	}
	cpl_override = 1;

	sysret_cs_seg_data[0] = 0xFFFF;
	sysret_cs_seg_data[1] = 0;
	sysret_cs_seg_data[2] = 0xFB00;
	sysret_cs_seg_data[3] = 0xC0;

	cpl_override = 0;

	use32 = 0x300;
	CS = (AMD_SYSRET_SB & ~3) | 3;

	do_seg_load(&_cs, sysret_cs_seg_data);
	flushmmucache_cr3();
	use32 = 0x300;

	CS = (CS & 0xFFFC) | 3;

	_cs.limit = 0xFFFFFFFF;
	_cs.limit_high = 0xFFFFFFFF;

	/* SS */
	sysret_ss_seg_data[0] = 0xFFFF;
	sysret_ss_seg_data[1] = 0;
	sysret_ss_seg_data[2] = 0xF300;
	sysret_ss_seg_data[3] = 0xC0;
	do_seg_load(&_ss, sysret_ss_seg_data);
	_ss.seg = ((AMD_SYSRET_SB + 8) & 0xFFFC) | 3;
	stack32 = 1;

	_ss.limit = 0xFFFFFFFF;
	_ss.limit_high = 0xFFFFFFFF;

	_ss.checked = 0;

	CLOCK_CYCLES(20);

	CPU_BLOCK_END();

	/* pclog("SYSRET completed:\n");
	pclog("CS (%04X): base=%08X, limit=%08X, access=%02X, seg=%04X, limit_low=%08X, limit_high=%08X, checked=%i\n", CS, _cs.base, _cs.limit, _cs.access, _cs.seg, _cs.limit_low, _cs.limit_high, _cs.checked);
	pclog("SS (%04X): base=%08X, limit=%08X, access=%02X, seg=%04X, limit_low=%08X, limit_high=%08X, checked=%i\n", SS, _ss.base, _ss.limit, _ss.access, _ss.seg, _ss.limit_low, _ss.limit_high, _ss.checked);
	pclog("Model specific registers: cs_msr=%04X, esp_msr=%08X, eip_msr=%08X\n", cs_msr, esp_msr, eip_msr);
	pclog("Other information: eflags=%04X flags=%04X use32=%04X stack32=%i ECX=%08X EDX=%08X\n", eflags, flags, use32, stack32, ECX, EDX); */

	return 0;
}
