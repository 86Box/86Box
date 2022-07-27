/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Functions common to all emulated x86 CPU's.
 *
 * Authors:	Andrew Jenner, <https://www.reenigne.org>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2015-2020 Andrew Jenner.
 *		Copyright 2016-2020 Miran Grca.
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include "x86.h"
#include <86box/machine.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/pci.h>
#include <86box/ppi.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>

/* The opcode of the instruction currently being executed. */
uint8_t opcode;

/* The tables to speed up the setting of the Z, N, and P cpu_state.flags. */
uint8_t znptable8[256];
uint16_t znptable16[65536];

/* A 16-bit zero, needed because some speed-up arrays contain pointers to it. */
uint16_t zero = 0;

/* MOD and R/M stuff. */
uint16_t *mod1add[2][8];
uint32_t *mod1seg[8];
uint32_t rmdat;

/* XT CPU multiplier. */
uint64_t xt_cpu_multi;

/* Variables for handling the non-maskable interrupts. */
int nmi = 0, nmi_auto_clear = 0;

/* Was the CPU ever reset? */
int x86_was_reset = 0, soft_reset_pci = 0;

/* Is the TRAP flag on? */
int trap = 0;

/* The current effective address's segment. */
uint32_t easeg;

/* This is for the OPTI 283 special reset handling mode. */
int reset_on_hlt, hlt_reset_pending;


#ifdef ENABLE_X86_LOG
void	dumpregs(int);

int x86_do_log = ENABLE_X86_LOG;
int indump = 0;


static void
x808x_log(const char *fmt, ...)
{
    va_list ap;

    if (x808x_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}


void
dumpregs(int force)
{
    int c;
    char *seg_names[4] = { "ES", "CS", "SS", "DS" };

    /* Only dump when needed, and only once.. */
    if (indump || (!force && !dump_on_exit))
	return;

    x808x_log("EIP=%08X CS=%04X DS=%04X ES=%04X SS=%04X FLAGS=%04X\n",
	      cpu_state.pc, CS, DS, ES, SS, cpu_state.flags);
    x808x_log("Old CS:EIP: %04X:%08X; %i ins\n", oldcs, cpu_state.oldpc, ins);
    for (c = 0; c < 4; c++) {
	x808x_log("%s : base=%06X limit=%08X access=%02X  limit_low=%08X limit_high=%08X\n",
		  seg_names[c], _opseg[c]->base, _opseg[c]->limit,
		  _opseg[c]->access, _opseg[c]->limit_low, _opseg[c]->limit_high);
    }
    if (is386) {
	x808x_log("FS : base=%06X limit=%08X access=%02X  limit_low=%08X limit_high=%08X\n",
		  seg_fs, cpu_state.seg_fs.limit, cpu_state.seg_fs.access, cpu_state.seg_fs.limit_low, cpu_state.seg_fs.limit_high);
	x808x_log("GS : base=%06X limit=%08X access=%02X  limit_low=%08X limit_high=%08X\n",
		  gs, cpu_state.seg_gs.limit, cpu_state.seg_gs.access, cpu_state.seg_gs.limit_low, cpu_state.seg_gs.limit_high);
	x808x_log("GDT : base=%06X limit=%04X\n", gdt.base, gdt.limit);
	x808x_log("LDT : base=%06X limit=%04X\n", ldt.base, ldt.limit);
	x808x_log("IDT : base=%06X limit=%04X\n", idt.base, idt.limit);
	x808x_log("TR  : base=%06X limit=%04X\n", tr.base, tr.limit);
	x808x_log("386 in %s mode: %i-bit data, %-i-bit stack\n",
		  (msw & 1) ? ((cpu_state.eflags & VM_FLAG) ? "V86" : "protected") : "real",
		  (use32) ? 32 : 16, (stack32) ? 32 : 16);
	x808x_log("CR0=%08X CR2=%08X CR3=%08X CR4=%08x\n", cr0, cr2, cr3, cr4);
	x808x_log("EAX=%08X EBX=%08X ECX=%08X EDX=%08X\nEDI=%08X ESI=%08X EBP=%08X ESP=%08X\n",
		  EAX, EBX, ECX, EDX, EDI, ESI, EBP, ESP);
    } else {
	x808x_log("808x/286 in %s mode\n", (msw & 1) ? "protected" : "real");
	x808x_log("AX=%04X BX=%04X CX=%04X DX=%04X DI=%04X SI=%04X BP=%04X SP=%04X\n",
		  AX, BX, CX, DX, DI, SI, BP, SP);
    }
    x808x_log("Entries in readlookup : %i    writelookup : %i\n", readlnum, writelnum);
    x87_dumpregs();
    indump = 0;
}
#else
#define x808x_log(fmt, ...)
#endif


/* Preparation of the various arrays needed to speed up the MOD and R/M work. */
static void
makemod1table(void)
{
    mod1add[0][0] = &BX;
    mod1add[0][1] = &BX;
    mod1add[0][2] = &BP;
    mod1add[0][3] = &BP;
    mod1add[0][4] = &SI;
    mod1add[0][5] = &DI;
    mod1add[0][6] = &BP;
    mod1add[0][7] = &BX;
    mod1add[1][0] = &SI;
    mod1add[1][1] = &DI;
    mod1add[1][2] = &SI;
    mod1add[1][3] = &DI;
    mod1add[1][4] = &zero;
    mod1add[1][5] = &zero;
    mod1add[1][6] = &zero;
    mod1add[1][7] = &zero;
    mod1seg[0] = &ds;
    mod1seg[1] = &ds;
    mod1seg[2] = &ss;
    mod1seg[3] = &ss;
    mod1seg[4] = &ds;
    mod1seg[5] = &ds;
    mod1seg[6] = &ss;
    mod1seg[7] = &ds;
}


/* Prepare the ZNP table needed to speed up the setting of the Z, N, and P cpu_state.flags. */
static void
makeznptable(void)
{
    int c, d, e;
    for (c = 0; c < 256; c++) {
	d = 0;
	for (e = 0; e < 8; e++) {
		if (c & (1 << e))
			d++;
	}
	if (d & 1)
		znptable8[c] = 0;
	else
		znptable8[c] = P_FLAG;
#ifdef ENABLE_808X_LOG
	if (c == 0xb1)
		x808x_log("znp8 b1 = %i %02X\n", d, znptable8[c]);
#endif
	if (!c)
		znptable8[c] |= Z_FLAG;
	if (c & 0x80)
		znptable8[c] |= N_FLAG;
    }

    for (c = 0; c < 65536; c++) {
	d = 0;
	for (e = 0; e < 8; e++) {
		if (c & (1 << e))
			d++;
	}
	if (d & 1)
		znptable16[c] = 0;
	else
		znptable16[c] = P_FLAG;
#ifdef ENABLE_808X_LOG
	if (c == 0xb1)
		x808x_log("znp16 b1 = %i %02X\n", d, znptable16[c]);
	if (c == 0x65b1)
		x808x_log("znp16 65b1 = %i %02X\n", d, znptable16[c]);
#endif
	if (!c)
		znptable16[c] |= Z_FLAG;
	if (c & 0x8000)
		znptable16[c] |= N_FLAG;
    }
}


/* Common reset function. */
static void
reset_common(int hard)
{
#ifdef ENABLE_808X_LOG
    if (hard)
	x808x_log("x86 reset\n");
#endif

    if (!hard && reset_on_hlt) {
	hlt_reset_pending++;
	pclog("hlt_reset_pending = %i\n", hlt_reset_pending);
	if (hlt_reset_pending == 2)
		hlt_reset_pending = 0;
	else
		return;
    }

    /* Make sure to gracefully leave SMM. */
    if (in_smm)
	leave_smm();

    /* Needed for the ALi M1533. */
    if (is486 && (hard || soft_reset_pci)) {
	pci_reset();
	if (!hard && soft_reset_pci) {
		dma_reset();
		/* TODO: Hack, but will do for time being, because all AT machines currently are 286+,
			 and vice-versa. */
		dma_set_at(is286);
		device_reset_all();
	}
    }

    use32 = 0;
    cpu_cur_status = 0;
    stack32 = 0;
    msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
    msw = 0;
    if (hascache)
	cr0 = 1 << 30;
    else
	cr0 = 0;
    cpu_cache_int_enabled = 0;
    cpu_update_waitstates();
    cr4 = 0;
    cpu_state.eflags = 0;
    cgate32 = 0;
    if (is286) {
	loadcs(0xF000);
	cpu_state.pc = 0xFFF0;
	rammask = cpu_16bitbus ? 0xFFFFFF : 0xFFFFFFFF;
	if (is6117)
		rammask |= 0x03000000;
    }
    idt.base = 0;
    cpu_state.flags = 2;
    trap = 0;

    idt.limit = is386 ? 0x03ff : 0xffff;
    if (is386 || hard)
	EAX = EBX = ECX = EDX = ESI = EDI = EBP = ESP = 0;

    if (hard) {
	makeznptable();
	resetreadlookup();
	makemod1table();
	cpu_set_edx();
	mmu_perm = 4;
    }
    x86seg_reset();
#ifdef USE_DYNAREC
    if (hard)
	codegen_reset();
#endif
    if (!hard)
	flushmmucache();
    x86_was_reset = 1;
    cpu_alt_reset = 0;

    cpu_ven_reset();

    in_smm = smi_latched = 0;
    smi_line = smm_in_hlt = 0;
    smi_block = 0;

    if (hard) {
	if (is486)
		smbase = is_am486dxl ? 0x00060000 : 0x00030000;
	ppi_reset();
    }
    in_sys = 0;

    shadowbios = shadowbios_write = 0;
    alt_access = cpu_end_block_after_ins = 0;

    if (hard) {
    	reset_on_hlt = hlt_reset_pending = 0;
	cache_index = 0;
	memset(_tr, 0x00, sizeof(_tr));
	memset(_cache, 0x00, sizeof(_cache));
    }

    if (!is286)
	reset_808x(hard);
}


/* Hard reset. */
void
resetx86(void)
{
    reset_common(1);

    soft_reset_mask = 0;
}


/* Soft reset. */
void
softresetx86(void)
{
    if (soft_reset_mask)
	return;

    if (ibm8514_enabled || xga_enabled)
        vga_on = 1;

    reset_common(0);
}


/* Actual hard reset. */
void
hardresetx86(void)
{
    dma_reset();
    /* TODO: Hack, but will do for time being, because all AT machines currently are 286+,
       and vice-versa. */
    dma_set_at(is286);
    device_reset_all();

    cpu_alt_reset = 0;

    mem_a20_alt = 0;
    mem_a20_recalc();

    flushmmucache();

    resetx86();
}
