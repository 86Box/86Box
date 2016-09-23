//#if 0
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "ibm.h"
#include "mem.h"
#include "x86.h"
#include "386.h"
#include "cpu.h"

/*Controls whether the accessed bit in a descriptor is set when CS is loaded.*/
#define CS_ACCESSED

/*Controls whether the accessed bit in a descriptor is set when a data or stack
  selector is loaded.*/
#define SEL_ACCESSED
int stimes = 0;
int dtimes = 0;
int btimes = 0;
int is486=1;

uint32_t abrt_error;
int cgate16,cgate32;

#define breaknullsegs 0

int intgatesize;

void taskswitch286(uint16_t seg, uint16_t *segdat, int is32);
void taskswitch386(uint16_t seg, uint16_t *segdat);

int output;
void pmodeint(int num, int soft);
/*NOT PRESENT is INT 0B
  GPF is INT 0D*/

FILE *pclogf;
void x86abort(const char *format, ...)
{
   va_list ap;
   va_start(ap, format);
   vprintf(format, ap);
   va_end(ap);
   fflush(stdout);
   savenvr();
   dumpregs();
   fflush(stdout);
   exit(-1);
}

uint8_t opcode2;

static void seg_reset(x86seg *s)
{
        s->access = (0 << 5) | 2 | 0x80;
        s->limit = 0xFFFF;
        s->limit_low = 0;
        s->limit_high = 0xffff;
}

void x86seg_reset()
{
        seg_reset(&_cs);
        seg_reset(&_ds);
        seg_reset(&_es);
        seg_reset(&_fs);
        seg_reset(&_gs);
        seg_reset(&_ss);
}

void x86_doabrt(int x86_abrt)
{
//        ingpf = 1;
        CS = oldcs;
        cpu_state.pc = cpu_state.oldpc;
        _cs.access = (oldcpl << 5) | 0x80;
//        pclog("x86_doabrt - %02X %08X  %04X:%08X  %i\n", x86_abrt, abrt_error, CS, pc, ins);
        
/*        if (CS == 0x3433 && cpu_state.pc == 0x000006B0)
        {
                // pclog("Quit it\n");
                dumpregs();
                exit(-1);
        }*/
//        pclog("GPF! - error %04X  %04X(%08X):%08X %02X %02X  %i  %04X %i %i\n",error,CS,cs,cpu_state.pc,opcode,opcode2,ins,flags&I_FLAG,IOPL, dtimes);

        if (msw & 1)
                pmodeint(x86_abrt, 0);
        else
        {
                uint32_t addr = (x86_abrt << 2) + idt.base;
                if (stack32)
                {
                        writememw(ss,ESP-2,flags);
                        writememw(ss,ESP-4,CS);
                        writememw(ss,ESP-6,cpu_state.pc);
                        ESP-=6;
                }
                else
                {
                        writememw(ss,((SP-2)&0xFFFF),flags);
                        writememw(ss,((SP-4)&0xFFFF),CS);
                        writememw(ss,((SP-6)&0xFFFF),cpu_state.pc);
                        SP-=6;
                }

                flags&=~I_FLAG;
                flags&=~T_FLAG;
                oxpc=cpu_state.pc;
                cpu_state.pc=readmemw(0,addr);
                loadcs(readmemw(0,addr+2));
                return;
        }
        
        if (cpu_state.abrt) return;
        
        if (intgatesize == 16)
        {
                if (stack32)
                {
                        writememw(ss, ESP-2, abrt_error);
                        ESP-=2;
                }
                else
                {
                        writememw(ss, ((SP-2)&0xFFFF), abrt_error);
                        SP-=2;
                }
        }
        else
        {
                if (stack32)
                {
                        writememl(ss, ESP-4, abrt_error);
                        ESP-=4;
                }
                else
                {
                        writememl(ss, ((SP-4)&0xFFFF), abrt_error);
                        SP-=4;
                }
        }
//        ingpf = 0;
//        cpu_state.abrt = gpf = 1;
}
void x86gpf(char *s, uint16_t error)
{
        // pclog("GPF %04X : %s\n", error, s);
        cpu_state.abrt = ABRT_GPF;
        abrt_error = error;
}
void x86ss(char *s, uint16_t error)
{
        // pclog("SS %04X\n", error);
        cpu_state.abrt = ABRT_SS;
        abrt_error = error;
}
void x86ts(char *s, uint16_t error)
{
        // pclog("TS %04X\n", error);
        cpu_state.abrt = ABRT_TS;
        abrt_error = error;
}
void x86np(char *s, uint16_t error)
{
        // pclog("NP %04X : %s\n", error, s);
        cpu_state.abrt = ABRT_NP;
        abrt_error = error;
}


void do_seg_load(x86seg *s, uint16_t *segdat)
{
        s->limit = segdat[0] | ((segdat[3] & 0xF) << 16);
        if (segdat[3] & 0x80)
                s->limit = (s->limit << 12) | 0xFFF;
        s->base = segdat[1] | ((segdat[2] & 0xFF) << 16);
        if (is386)
                s->base |= ((segdat[3] >> 8) << 24);
        s->access = segdat[2] >> 8;
                        
        if ((segdat[2] & 0x1800) != 0x1000 || !(segdat[2] & (1 << 10))) /*expand-down*/
        {
                s->limit_high = s->limit;
                s->limit_low = 0;
        }
        else
        {
                s->limit_high = (segdat[3] & 0x40) ? 0xffffffff : 0xffff;
                s->limit_low = s->limit + 1;
        }
//        if (output) pclog("SEG : base=%08x limit=%08x low=%08x high=%08x\n", s->base, s->limit, s->limit_low, s->limit_high);
}

static void do_seg_v86_init(x86seg *s)
{
        s->access = (3 << 5) | 2 | 0x80;
        s->limit = 0xffff;
        s->limit_low = 0;
        s->limit_high = 0xffff;
}

static void check_seg_valid(x86seg *s)
{
        int dpl = (s->access >> 5) & 3;
        int valid = 1;

        if (s->seg & 4)
        {
                if ((s->seg & ~7) >= ldt.limit)
                {
//                        pclog("Bigger than LDT limit %04X %04X %02X %02X %02X\n", s->seg, ldt.limit, opcode, opcode2, rmdat);
                        valid = 0;
                }
        }
        else
        {
                if ((s->seg & ~7) >= gdt.limit)
                {
//                        pclog("Bigger than GDT limit %04X %04X\n", s->seg, gdt.limit);
                        valid = 0;
                }
        }

        switch (s->access & 0x1f)
        {
                case 0x10: case 0x11: case 0x12: case 0x13: /*Data segments*/
                case 0x14: case 0x15: case 0x16: case 0x17:
                case 0x1A: case 0x1B: /*Readable non-conforming code*/
                if ((s->seg & 3) > dpl || (CPL) > dpl)
                {
//                        pclog("Data seg fail - %04X:%08X %04X %i\n", CS, cpu_state.pc, s->seg, dpl);
                        valid = 0;
                        break;
                }
                break;
                
                case 0x1E: case 0x1F: /*Readable conforming code*/
                break;
                
                default:
                valid = 0;
                break;
        }
        
        if (!valid)
                loadseg(0, s);
}

void loadseg(uint16_t seg, x86seg *s)
{
        uint16_t segdat[4];
        uint32_t addr;
        int dpl;

        if (msw&1 && !(eflags&VM_FLAG))
        {
//                intcount++;
                if (!(seg&~3))
                {
                        if (s==&_ss)
                        {
                                // pclog("SS selector = NULL!\n");
                                x86ss(NULL,0);
                                return;
//                                dumpregs();
//                                exit(-1);
                        }
//                        if (s->base!=-1) pclog("NEW! ");
                        s->seg=0;
                        // s->access = 0;
                        s->access = 0x80;
                        s->base=-1;
//                        pclog("NULL selector %s%s%s%s %04X(%06X):%06X\n",(s==&_ds)?"DS":"",(s==&_es)?"ES":"",(s==&_fs)?"FS":"",(s==&_gs)?"GS":"",CS,cs,cpu_state.pc);
                        return;
                }
//                if (s==&_ss) pclog("Load SS %04X\n",seg);
//                pclog("Protected mode seg load!\n");
                addr=seg&~7;
                if (seg&4)
                {
                        if (addr>=ldt.limit)
                        {
                                // pclog("Bigger than LDT limit %04X %04X %02X %02X %02X\n",seg,ldt.limit, opcode, opcode2, rmdat);
//                                dumppic();
//                                dumpregs();
//                                exit(-1);
                                x86gpf("loadseg(): Bigger than LDT limit",seg&~3);
                                return;
                        }
                        addr+=ldt.base;
                }
                else
                {
                        if (addr>=gdt.limit)
                        {
                                // pclog("Bigger than GDT limit %04X %04X 1\n",seg,gdt.limit);
//                                dumpregs();
//                                exit(-1);
                                x86gpf("loadseg(): Bigger than GDT limit",seg&~3);
                                return;
                        }
                        addr+=gdt.base;
                }
                cpl_override=1;
                segdat[0]=readmemw(0,addr);
                segdat[1]=readmemw(0,addr+2);
                segdat[2]=readmemw(0,addr+4);
                segdat[3]=readmemw(0,addr+6); cpl_override=0; if (cpu_state.abrt) return;
                dpl=(segdat[2]>>13)&3;
                if (s==&_ss)
                {
                        if (!(seg&~3))
                        {
                                // pclog("Load SS null selector\n");
                                x86gpf(NULL,seg&~3);
                                return;
                        }
                        if ((seg&3)!=CPL || dpl!=CPL)
                        {
                                // pclog("Invalid SS permiss\n");
                                x86gpf(NULL,seg&~3);
//                                x86abort("Invalid SS permiss for %04X!\n",seg&0xFFFC);
                                return;
                        }
                        switch ((segdat[2]>>8)&0x1F)
                        {
                                case 0x12: case 0x13: case 0x16: case 0x17: /*r/w*/
                                break;
                                default:
                                // pclog("Invalid SS type\n");
                                x86gpf(NULL,seg&~3);
//                                x86abort("Invalid SS segment type for %04X!\n",seg&0xFFFC);
                                return;
                        }
                        if (!(segdat[2]&0x8000))
                        {
                                // pclog("Load SS not present!\n");
                                x86ss(NULL,seg&~3);
                                return;
                        }
                        stack32 = (segdat[3] & 0x40) ? 1 : 0;
//                        pclog("Load SS  %04x %04x %04x %04x\n", segdat[0], segdat[1], segdat[2], segdat[3]);
                }
                else if (s!=&_cs)
                {
                        if (output) pclog("Seg data %04X %04X %04X %04X\n", segdat[0], segdat[1], segdat[2], segdat[3]);
                        if (output) pclog("Seg type %03X\n",segdat[2]&0x1F00);
                        switch ((segdat[2]>>8)&0x1F)
                        {
                                case 0x10: case 0x11: case 0x12: case 0x13: /*Data segments*/
                                case 0x14: case 0x15: case 0x16: case 0x17:
                                case 0x1A: case 0x1B: /*Readable non-conforming code*/
//                                pclog("Load seg %04X %i %i %04X:%08X\n",seg,dpl,CS&3,CS,cpu_state.pc);
                                if ((seg&3)>dpl || (CPL)>dpl)
                                {
                                        // pclog("Data seg fail - %04X:%08X %04X %i %04X\n",CS,cpu_state.pc,seg,dpl,segdat[2]);
                                        x86gpf(NULL,seg&~3);
//                                        x86abort("Data segment load - level too low!\n",seg&0xFFFC);
                                        return;
                                }
                                break;
                                case 0x1E: case 0x1F: /*Readable conforming code*/
                                break;
                                default:
                                // pclog("Invalid segment type for %04X! %04X\n",seg&0xFFFC,segdat[2]);
                                x86gpf(NULL,seg&~3);
                                return;
                        }
                }

                if (!(segdat[2] & 0x8000))
                {
                        x86np("Load data seg not present", seg & 0xfffc);
                        return;
                }
                s->seg = seg;
                do_seg_load(s, segdat);

#ifndef CS_ACCESSED
                if (s != &_cs)
                {
#endif                   
#ifdef SEL_ACCESSED         
                        cpl_override = 1;
                        writememw(0, addr+4, segdat[2] | 0x100); /*Set accessed bit*/
                        cpl_override = 0;
#endif
#ifndef CS_ACCESSED
                }
#endif
                s->checked = 0;
        }
        else
        {
                s->access = (3 << 5) | 2 | 0x80;
                s->base = seg << 4;
                s->seg = seg;
                if (s == &_ss)
                        stack32 = 0;
                s->checked = 1;
        }
}

#define DPL ((segdat[2]>>13)&3)
#define DPL2 ((segdat2[2]>>13)&3)
#define DPL3 ((segdat3[2]>>13)&3)

void loadcs(uint16_t seg)
{
        uint16_t segdat[4];
        uint32_t addr;
        if (output) pclog("Load CS %04X\n",seg);
        if (msw&1 && !(eflags&VM_FLAG))
        {
//                intcount++;
//                flushmmucache();
//                pclog("Load CS %04X\n",seg);
                if (!(seg&~3))
                {
                        // pclog("Trying to load CS with NULL selector! lcs\n");
//                        dumpregs();
//                        exit(-1);
                        x86gpf(NULL,0);
                        return;
                }
//                pclog("Protected mode CS load! %04X\n",seg);
                addr=seg&~7;
                if (seg&4)
                {
                        if (addr>=ldt.limit)
                        {
                                // pclog("Bigger than LDT limit %04X %04X CS\n",seg,ldt.limit);
                                x86gpf(NULL,seg&~3);
                                return;
                        }
                        addr+=ldt.base;
                }
                else
                {
                        if (addr>=gdt.limit)
                        {
                                // pclog("Bigger than GDT limit %04X %04X CS\n",seg,gdt.limit);
                                x86gpf(NULL,seg&~3);
                                return;
                        }
                        addr+=gdt.base;
                }
                cpl_override=1;
                segdat[0]=readmemw(0,addr);
                segdat[1]=readmemw(0,addr+2);
                segdat[2]=readmemw(0,addr+4);
                segdat[3]=readmemw(0,addr+6); cpl_override=0; if (cpu_state.abrt) return;
                // if (optype==JMP) pclog("Code seg - %04X - %04X %04X %04X %04X\n",seg,segdat[0],segdat[1],segdat[2],segdat[3]);
//                if (!(segdat[2]&0x8000)) x86abort("Code segment not present!\n");
//                if (output) pclog("Segdat2 %04X\n",segdat[2]);
                if (segdat[2]&0x1000) /*Normal code segment*/
                {
                        if (!(segdat[2]&0x400)) /*Not conforming*/
                        {
                                if ((seg&3)>CPL)
                                {
                                        x86gpf(NULL,seg&~3);
                                        // pclog("loadcs RPL > CPL %04X %04X %i %02X\n",segdat[2],seg,CPL,opcode);
                                        return;
                                }
                                if (CPL != DPL)
                                {
                                        x86gpf("loadcs(): CPL != DPL",seg&~3);
                                        return;
                                }
                        }
                        if (CPL < DPL)
                        {
                                x86gpf("loadcs(): CPL < DPL",seg&~3);
                                return;
                        }
                        if (!(segdat[2]&0x8000))
                        {
                                x86np("Load CS not present", seg & 0xfffc);
                                return;
                        }
                        if (segdat[3]&0x40) use32=0x300;
                        else                use32=0;
                        CS=(seg&~3)|CPL;
                        do_seg_load(&_cs, segdat);
                        use32=(segdat[3]&0x40)?0x300:0;
                        if (CPL==3 && oldcpl!=3) flushmmucache_cr3();

#ifdef CS_ACCESSED                        
                        cpl_override = 1;
                        writememw(0, addr+4, segdat[2] | 0x100); /*Set accessed bit*/
                        cpl_override = 0;
#endif
//                        if (output) pclog("Load CS %08X\n",_cs.base);
//                        CS=(CS&0xFFFC)|((_cs.access>>5)&3);
                }
                else /*System segment*/
                {
                        if (!(segdat[2]&0x8000))
                        {
                                x86np("Load CS system seg not present\n", seg & 0xfffc);
                                return;
                        }
                        switch (segdat[2]&0xF00)
                        {
                                default:
                                // pclog("Bad CS %02X %02X %i special descriptor %03X %04X\n",opcode,rmdat,optype,segdat[2]&0xF00,seg);
                                x86gpf(NULL,seg&~3);
                                return;
                        }
                }
//                pclog("CS = %04X base=%06X limit=%04X access=%02X  %04X\n",CS,cs,_cs.limit,_cs.access,addr);
//                dumpregs();
//                exit(-1);
        }
        else
        {
                _cs.base=seg<<4;
                _cs.limit=0xFFFF;
                _cs.limit_low = 0;
                _cs.limit_high = 0xffff;
                CS=seg;
                if (eflags&VM_FLAG) _cs.access=(3<<5) | 2 | 0x80;
                else                _cs.access=(0<<5) | 2 | 0x80;
                if (CPL==3 && oldcpl!=3) flushmmucache_cr3();
        }
}

void loadcsjmp(uint16_t seg, uint32_t oxpc)
{
        uint16_t segdat[4];
        uint32_t addr;
        int count;
        uint16_t type,seg2;
        uint32_t newpc;
//        pclog("Load CS JMP %04X\n",seg);
        if (msw&1 && !(eflags&VM_FLAG))
        {
                if (!(seg&~3))
                {
                        // pclog("Trying to load CS with NULL selector! lcsjmp\n");
                        x86gpf(NULL,0);
                        return;
//                        dumpregs();
//                        exit(-1);
                }
                addr=seg&~7;
                if (seg&4)
                {
                        if (addr>=ldt.limit)
                        {
                                // pclog("Bigger than LDT limit %04X %04X CS\n",seg,ldt.limit);
                                x86gpf(NULL,seg&~3);
                                return;
                        }
                        addr+=ldt.base;
                }
                else
                {
                        if (addr>=gdt.limit)
                        {
                                // pclog("Bigger than GDT limit %04X %04X CS\n",seg,gdt.limit);
                                x86gpf(NULL,seg&~3);
                                return;
                        }
                        addr+=gdt.base;
                }
                cpl_override=1;
                segdat[0]=readmemw(0,addr);
                segdat[1]=readmemw(0,addr+2);
                segdat[2]=readmemw(0,addr+4);
                segdat[3]=readmemw(0,addr+6); cpl_override=0; if (cpu_state.abrt) return;
                if (output) pclog("%04X %04X %04X %04X\n",segdat[0],segdat[1],segdat[2],segdat[3]);
                if (segdat[2]&0x1000) /*Normal code segment*/
                {
//                        pclog("Normal CS\n");
                        if (!(segdat[2]&0x400)) /*Not conforming*/
                        {
                                if ((seg&3)>CPL)
                                {
                                        x86gpf("loadcsjmp(): segment PL > CPL",seg&~3);
                                        return;
                                }
                                if (CPL != DPL)
                                {
                                        x86gpf("loadcsjmp(): CPL != DPL",seg&~3);
                                        return;
                                }
                        }
                        if (CPL < DPL)
                        {
                                x86gpf("loadcsjmp(): CPL < DPL",seg&~3);
                                return;
                        }
                        if (!(segdat[2]&0x8000))
                        {
                                x86np("Load CS JMP not present\n", seg & 0xfffc);
                                return;
                        }
                        if (segdat[3]&0x40) use32=0x300;
                        else                use32=0;

#ifdef CS_ACCESSED                        
                        cpl_override = 1;
                        writememw(0, addr+4, segdat[2] | 0x100); /*Set accessed bit*/
                        cpl_override = 0;
#endif
                        
                        CS = (seg & ~3) | CPL;
                        segdat[2] = (segdat[2] & ~(3 << (5+8))) | (CPL << (5+8));

                        do_seg_load(&_cs, segdat);
                        if (CPL==3 && oldcpl!=3) flushmmucache_cr3();
                        use32=(segdat[3]&0x40)?0x300:0;
                        cycles -= timing_jmp_pm;
                }
                else /*System segment*/
                {
//                        pclog("System CS\n");
                        if (!(segdat[2]&0x8000))
                        {
                                // x86np("Load CS JMP system selector not present\n", seg & 0xfffc);
                                return;
                        }
                        type=segdat[2]&0xF00;
                        if (type==0x400) newpc=segdat[0];
                        else             newpc=segdat[0]|(segdat[3]<<16);
                        switch (type)
                        {
                                case 0x400: /*Call gate*/
                                case 0xC00:
//                                pclog("Call gate\n");
                                cgate32=(type&0x800);
                                cgate16=!cgate32;
                                oldcs=CS;
                                cpu_state.oldpc=cpu_state.pc;
                                count=segdat[2]&31;
#if 0
                                if ((DPL < CPL) || (DPL < (seg&3)))
                                {
                                        x86gpf(NULL,seg&~3);
                                        return;
                                }
#endif
                                if (DPL < CPL)
                                {
                                        x86gpf("loadcsjmp(): ex DPL < CPL",seg&~3);
                                        return;
                                }
                                if ((DPL < (seg&3)))
                                {
                                        x86gpf("loadcsjmp(): ex (DPL < (seg&3))",seg&~3);
                                        return;
                                }
                                if (!(segdat[2]&0x8000))
                                {
                                        x86np("Load CS JMP call gate not present\n", seg & 0xfffc);
                                        return;
                                }
                                seg2=segdat[1];

                                if (!(seg2&~3))
                                {
                                        // pclog("Trying to load CS with NULL selector! lcsjmpcg\n");
                                        x86gpf(NULL,0);
                                        return;
//                                        dumpregs();
//                                        exit(-1);
                                }
                                addr=seg2&~7;
                                if (seg2&4)
                                {
                                        if (addr>=ldt.limit)
                                        {
                                                // pclog("Bigger than LDT limit %04X %04X CSJ\n",seg2,gdt.limit);
                                                x86gpf(NULL,seg2&~3);
                                                return;
                                        }
                                        addr+=ldt.base;
                                }
                                else
                                {
                                        if (addr>=gdt.limit)
                                        {
                                                // pclog("Bigger than GDT limit %04X %04X CSJ\n",seg2,gdt.limit);
                                                x86gpf(NULL,seg2&~3);
                                                return;
                                        }
                                        addr+=gdt.base;
                                }
                                cpl_override=1;
                                segdat[0]=readmemw(0,addr);
                                segdat[1]=readmemw(0,addr+2);
                                segdat[2]=readmemw(0,addr+4);
                                segdat[3]=readmemw(0,addr+6); cpl_override=0; if (cpu_state.abrt) return;

                                if (DPL > CPL)
                                {
                                        x86gpf("loadcsjmp(): ex DPL > CPL",seg2&~3);
                                        return;
                                }
                                if (!(segdat[2]&0x8000))
                                {
                                        x86np("Load CS JMP from call gate not present\n", seg2 & 0xfffc);
                                        return;
                                }


                                switch (segdat[2]&0x1F00)
                                {
                                        case 0x1800: case 0x1900: case 0x1A00: case 0x1B00: /*Non-conforming code*/
                                        if (DPL > CPL)
                                        {
                                                // pclog("Call gate DPL > CPL");
                                                x86gpf(NULL,seg2&~3);
                                                return;
                                        }
                                        case 0x1C00: case 0x1D00: case 0x1E00: case 0x1F00: /*Conforming*/
                                        CS=seg2;
                                        do_seg_load(&_cs, segdat);
                                        if (CPL==3 && oldcpl!=3) flushmmucache_cr3();
                                        use32=(segdat[3]&0x40)?0x300:0;
                                                cpu_state.pc=newpc;

#ifdef CS_ACCESSED                                                
                                        cpl_override = 1;
                                        writememw(0, addr+4, segdat[2] | 0x100); /*Set accessed bit*/
                                        cpl_override = 0;
#endif
                                        break;

                                        default:
                                        // pclog("JMP Call gate bad segment type\n");
                                        x86gpf(NULL,seg2&~3);
                                        return;
                                }
                                cycles -= timing_jmp_pm_gate;
                                break;

                                
                                case 0x900: /*386 Task gate*/
//                                pclog("Task gate\n");
                                cpu_state.pc=oxpc;
                                cpl_override=1;
                                taskswitch286(seg,segdat,segdat[2]&0x800);
                                flags &= ~NT_FLAG;
                                cpl_override=0;
//                                case 0xB00: /*386 Busy task gate*/
//                                if (optype==JMP) pclog("Task switch!\n");
//                                taskswitch386(seg,segdat);
                                return;

                                default:
                                // pclog("Bad JMP CS %02X %02X %i special descriptor %03X %04X\n",opcode,rmdat,optype,segdat[2]&0xF00,seg);
                                x86gpf(NULL,0);
                                return;
//                                dumpregs();
//                                exit(-1);
                        }
                }
//                pclog("CS = %04X base=%06X limit=%04X access=%02X  %04X\n",CS,cs,_cs.limit,_cs.access,addr);
//                dumpregs();
//                exit(-1);
        }
        else
        {
                _cs.base=seg<<4;
                _cs.limit=0xFFFF;
                _cs.limit_low = 0;
                _cs.limit_high = 0xffff;
                CS=seg;
                if (eflags&VM_FLAG) _cs.access=(3<<5) | 2 | 0x80;
                else                _cs.access=(0<<5) | 2 | 0x80;
                if (CPL==3 && oldcpl!=3) flushmmucache_cr3();
                cycles -= timing_jmp_rm;
        }
}

void PUSHW(uint16_t v)
{
//        if (output==3) pclog("PUSHW %04X to %08X\n",v,ESP-4);
        if (stack32)
        {
                writememw(ss,ESP-2,v);
                if (cpu_state.abrt) return;
                ESP-=2;
        }
        else
        {
//                pclog("Write %04X to %08X\n", v, ss+((SP-2)&0xFFFF));
                writememw(ss,((SP-2)&0xFFFF),v);
                if (cpu_state.abrt) return;
                SP-=2;
        }
}
void PUSHL(uint32_t v)
{
//        if (output==3) pclog("PUSHL %08X to %08X\n",v,ESP-4);
        if (stack32)
        {
                writememl(ss,ESP-4,v);
                if (cpu_state.abrt) return;
                ESP-=4;
        }
        else
        {
                writememl(ss,((SP-4)&0xFFFF),v);
                if (cpu_state.abrt) return;
                SP-=4;
        }
}
uint16_t POPW()
{
        uint16_t tempw;
        if (stack32)
        {
                tempw=readmemw(ss,ESP);
                if (cpu_state.abrt) return 0;
                ESP+=2;
        }
        else
        {
                tempw=readmemw(ss,SP);
                if (cpu_state.abrt) return 0;
                SP+=2;
        }
        return tempw;
}
uint32_t POPL()
{
        uint32_t templ;
        if (stack32)
        {
                templ=readmeml(ss,ESP);
                if (cpu_state.abrt) return 0;
                ESP+=4;
        }
        else
        {
                templ=readmeml(ss,SP);
                if (cpu_state.abrt) return 0;
                SP+=4;
        }
        return templ;
}

void loadcscall(uint16_t seg)
{
        uint16_t seg2;
        uint16_t segdat[4],segdat2[4],newss;
        uint32_t addr,oldssbase=ss, oaddr;
        uint32_t newpc;
        int count;
        uint16_t oldcs=CPL;
        uint32_t oldss,oldsp,newsp,oldpc, oldsp2;
        int type;
        uint16_t tempw;
        
        int csout = output;
        
        if (msw&1 && !(eflags&VM_FLAG))
        {
                //flushmmucache();
                if (csout) pclog("Protected mode CS load! %04X\n",seg);
                if (!(seg&~3))
                {
                        // pclog("Trying to load CS with NULL selector! lcscall\n");
                        x86gpf(NULL,0);
                        return;
//                        dumpregs();
//                        exit(-1);
                }
                addr=seg&~7;
                if (seg&4)
                {
                        if (addr>=ldt.limit)
                        {
                                // pclog("Bigger than LDT limit %04X %04X CSC\n",seg,gdt.limit);
                                x86gpf(NULL,seg&~3);
                                return;
                        }
                        addr+=ldt.base;
                }
                else
                {
                        if (addr>=gdt.limit)
                        {
                                // pclog("Bigger than GDT limit %04X %04X CSC\n",seg,gdt.limit);
                                x86gpf(NULL,seg&~3);
                                return;
                        }
                        addr+=gdt.base;
                }
                cpl_override=1;
                segdat[0]=readmemw(0,addr);
                segdat[1]=readmemw(0,addr+2);
                segdat[2]=readmemw(0,addr+4);
                segdat[3]=readmemw(0,addr+6); cpl_override=0; if (cpu_state.abrt) return;
                type=segdat[2]&0xF00;
                if (type==0x400) newpc=segdat[0];
                else             newpc=segdat[0]|(segdat[3]<<16);

                if (csout) pclog("Code seg call - %04X - %04X %04X %04X\n",seg,segdat[0],segdat[1],segdat[2]);
                if (segdat[2]&0x1000)
                {
                        if (!(segdat[2]&0x400)) /*Not conforming*/
                        {
                                if ((seg&3)>CPL)
                                {
                                        /* if (csout) */ // pclog("Not conforming, RPL > CPL\n");
                                        x86gpf("loadcscall(): segment > CPL",seg&~3);
                                        return;
                                }
                                if (CPL != DPL)
                                {
                                        /* if (csout) */ // pclog("Not conforming, CPL != DPL (%i %i)\n",CPL,DPL);
                                        x86gpf(NULL,seg&~3);
                                        return;
                                }
                        }
                        if (CPL < DPL)
                        {
                                /* if (csout) */ // pclog("CPL < DPL\n");
                                x86gpf(NULL,seg&~3);
                                return;
                        }
                        if (!(segdat[2]&0x8000))
                        {
                                /* if (csout) */ // pclog("Not present\n");
                                x86np("Load CS call not present", seg & 0xfffc);
                                return;
                        }
                        if (segdat[3]&0x40) use32=0x300;
                        else                use32=0;

#ifdef CS_ACCESSED                        
                        cpl_override = 1;
                        writememw(0, addr+4, segdat[2] | 0x100); /*Set accessed bit*/
                        cpl_override = 0;
#endif
                        
                        /*Conforming segments don't change CPL, so preserve existing CPL*/
                        if (segdat[2]&0x400)
                        {
                                seg = (seg & ~3) | CPL;
                                segdat[2] = (segdat[2] & ~(3 << (5+8))) | (CPL << (5+8));
                        }
                        else /*On non-conforming segments, set RPL = CPL*/
                                seg = (seg & ~3) | CPL;
                        CS=seg;
                        do_seg_load(&_cs, segdat);
                        if (CPL==3 && oldcpl!=3) flushmmucache_cr3();
                        use32=(segdat[3]&0x40)?0x300:0;
                        if (csout) pclog("Complete\n");
                        cycles -= timing_call_pm;
                }
                else
                {
                        type=segdat[2]&0xF00;
                        if (csout) pclog("Type %03X\n",type);
                        switch (type)
                        {
                                case 0x400: /*Call gate*/
                                case 0xC00: /*386 Call gate*/
                                if (output) pclog("Callgate %08X\n", cpu_state.pc);
                                cgate32=(type&0x800);
                                cgate16=!cgate32;
                                oldcs=CS;
                                oldpc=cpu_state.pc;
                                count=segdat[2]&31;
#if 0
                                if ((DPL < CPL) || (DPL < (seg&3)))
                                {
                                        x86gpf("",seg&~3);
                                        return;
                                }
#endif
                                if ((DPL < CPL))
                                {
                                        x86gpf("loadcscall(): ex DPL < CPL",seg&~3);
                                        return;
                                }
                                if ((DPL < (seg&3)))
                                {
                                        x86gpf("loadcscall(): ex (DPL < (seg&3))",seg&~3);
                                        return;
                                }
                                if (!(segdat[2]&0x8000))
                                {
                                        if (output) pclog("Call gate not present %04X\n",seg);
                                        x86np("Call gate not present\n", seg & 0xfffc);
                                        return;
                                }
                                seg2=segdat[1];
                                
                                if (output) pclog("New address : %04X:%08X\n", seg2, newpc);
                                
                                if (!(seg2&~3))
                                {
                                        // pclog("Trying to load CS with NULL selector! lcscallcg\n");
                                        x86gpf(NULL,0);
                                        return;
//                                        dumpregs();
//                                       exit(-1);
                                }
                                addr=seg2&~7;
                                if (seg2&4)
                                {
                                        if (addr>=ldt.limit)
                                        {
                                                // pclog("Bigger than LDT limit %04X %04X CSC\n",seg2,gdt.limit);
                                                x86gpf(NULL,seg2&~3);
                                                return;
                                        }
                                        addr+=ldt.base;
                                }
                                else
                                {
                                        if (addr>=gdt.limit)
                                        {
                                                // pclog("Bigger than GDT limit %04X %04X CSC\n",seg2,gdt.limit);
                                                x86gpf(NULL,seg2&~3);
                                                return;
                                        }
                                        addr+=gdt.base;
                                }
                                cpl_override=1;
                                segdat[0]=readmemw(0,addr);
                                segdat[1]=readmemw(0,addr+2);
                                segdat[2]=readmemw(0,addr+4);
                                segdat[3]=readmemw(0,addr+6); cpl_override=0; if (cpu_state.abrt) return;
                                
                                if (output) pclog("Code seg2 call - %04X - %04X %04X %04X\n",seg2,segdat[0],segdat[1],segdat[2]);
                                
                                if (DPL > CPL)
                                {
                                        x86gpf("loadcscall(): ex DPL > CPL",seg2&~3);
                                        return;
                                }
                                if (!(segdat[2]&0x8000))
                                {
                                        if (output) pclog("Call gate CS not present %04X\n",seg2);
                                        x86np("Call gate CS not present", seg2 & 0xfffc);
                                        return;
                                }

                                
                                switch (segdat[2]&0x1F00)
                                {
                                        case 0x1800: case 0x1900: case 0x1A00: case 0x1B00: /*Non-conforming code*/
                                        if (DPL < CPL)
                                        {
                                                oaddr = addr;
                                                /*Load new stack*/
                                                oldss=SS;
                                                oldsp=oldsp2=ESP;
                                                cpl_override=1;
                                                if (tr.access&8)
                                                {
                                                        addr = 4 + tr.base + (DPL * 8);
                                                        newss=readmemw(0,addr+4);
                                                        newsp=readmeml(0,addr);
                                                }
                                                else
                                                {
                                                        addr = 2 + tr.base + (DPL * 4);
                                                        newss=readmemw(0,addr+2);
                                                        newsp=readmemw(0,addr);
                                                }
                                                cpl_override=0;
                                                if (cpu_state.abrt) return;
                                                if (output) pclog("New stack %04X:%08X\n",newss,newsp);
                                                if (!(newss&~3))
                                                {
                                                        // pclog("Call gate loading null SS\n");
                                                        x86ts(NULL,newss&~3);
                                                        return;
                                                }
                                                addr=newss&~7;
                                                if (newss&4)
                                                {
                                                        if (addr>=ldt.limit)
                                                        {
                                                                x86abort("Bigger than LDT limit %04X %08X %04X CSC SS\n",newss,addr,ldt.limit);
                                                                x86ts(NULL,newss&~3);
                                                                return;
                                                        }
                                                        addr+=ldt.base;
                                                }
                                                else
                                                {
                                                        if (addr>=gdt.limit)
                                                        {
                                                                x86abort("Bigger than GDT limit %04X %04X CSC\n",newss,gdt.limit);
                                                                x86ts(NULL,newss&~3);
                                                                return;
                                                        }
                                                        addr+=gdt.base;
                                                }
                                                cpl_override=1;
                                                if (output) pclog("Read stack seg\n");
                                                segdat2[0]=readmemw(0,addr);
                                                segdat2[1]=readmemw(0,addr+2);
                                                segdat2[2]=readmemw(0,addr+4);
                                                segdat2[3]=readmemw(0,addr+6); cpl_override=0; if (cpu_state.abrt) return;
                                                if (output) pclog("Read stack seg done!\n");
                                                if (((newss & 3) != DPL) || (DPL2 != DPL))
                                                {
                                                        // pclog("Call gate loading SS with wrong permissions  %04X %04X  %i %i   %04X %04X\n", newss, seg2, DPL, DPL2, segdat[2], segdat2[2]);
//                                                        dumpregs();
//                                                        exit(-1);
                                                        x86ts(NULL,newss&~3);
                                                        return;
                                                }
                                                if ((segdat2[2]&0x1A00)!=0x1200)
                                                {
                                                        // pclog("Call gate loading SS wrong type\n");
                                                        x86ts(NULL,newss&~3);
                                                        return;
                                                }
                                                if (!(segdat2[2]&0x8000))
                                                {
                                                        // pclog("Call gate loading SS not present\n");
                                                        x86np("Call gate loading SS not present\n", newss & 0xfffc);
                                                        return;
                                                }
                                                if (!stack32) oldsp &= 0xFFFF;
                                                SS=newss;
                                                stack32 = (segdat2[3] & 0x40) ? 1 : 0;
                                                if (stack32) ESP=newsp;
                                                else         SP=newsp;
                                                
                                                do_seg_load(&_ss, segdat2);

                                                if (output) pclog("Set access 1\n");

#ifdef SEL_ACCESSED                                                
                                                cpl_override = 1;
                                                writememw(0, addr+4, segdat2[2] | 0x100); /*Set accessed bit*/
                                                cpl_override = 0;
#endif
                                                
                                                CS=seg2;
                                                do_seg_load(&_cs, segdat);
                                                if (CPL==3 && oldcpl!=3) flushmmucache_cr3();
                                                use32=(segdat[3]&0x40)?0x300:0;
                                                cpu_state.pc=newpc;
                                                
                                                if (output) pclog("Set access 2\n");
                                                
#ifdef CS_ACCESSED
                                                cpl_override = 1;
                                                writememw(0, oaddr+4, segdat[2] | 0x100); /*Set accessed bit*/
                                                cpl_override = 0;
#endif
                        
                                                if (output) pclog("Type %04X\n",type);
                                                if (type==0xC00)
                                                {
                                                        PUSHL(oldss);
                                                        PUSHL(oldsp2);
                                                        if (cpu_state.abrt)
                                                        {
                                                                // pclog("ABRT PUSHL\n");
                                                                SS = oldss;
                                                                ESP = oldsp2;
                                                                return;
                                                        }
//                                                        if (output) pclog("Stack now %04X:%08X\n",SS,ESP);
                                                        if (count)
                                                        {
                                                                while (count)
                                                                {
                                                                        count--;
                                                                        PUSHL(readmeml(oldssbase,oldsp+(count*4)));
                                                                        if (cpu_state.abrt)
                                                                        {
                                                                                // pclog("ABRT COPYL\n");
                                                                                SS = oldss;
                                                                                ESP = oldsp2;
                                                                                return;
                                                                        }
                                                                }
                                                        }
//                                                                x86abort("Call gate with count %i\n",count);
//                                                        PUSHL(oldcs);
//                                                        PUSHL(oldpc); if (cpu_state.abrt) return;
                                                }
                                                else
                                                {
                                                        if (output) pclog("Stack %04X\n",SP);
                                                        PUSHW(oldss);
                                                        if (output) pclog("Write SS to %04X:%04X\n",SS,SP);
                                                        PUSHW(oldsp2);
                                                        if (cpu_state.abrt)
                                                        {
                                                                // pclog("ABRT PUSHW\n");
                                                                SS = oldss;
                                                                ESP = oldsp2;
                                                                return;
                                                        }
                                                        if (output) pclog("Write SP to %04X:%04X\n",SS,SP);
//                                                        if (output) pclog("Stack %04X %i %04X:%04X\n",SP,count,oldssbase,oldsp);
//                                                        if (output) pclog("PUSH %04X %04X %i %i now %04X:%08X\n",oldss,oldsp,count,stack32,SS,ESP);
                                                        if (count)
                                                        {
                                                                while (count)
                                                                {
                                                                        count--;
                                                                        tempw=readmemw(oldssbase,(oldsp&0xFFFF)+(count*2));
                                                                        if (output) pclog("PUSH %04X\n",tempw);
                                                                        PUSHW(tempw);
                                                                        if (cpu_state.abrt)
                                                                        {
                                                                                // pclog("ABRT COPYW\n");
                                                                                SS = oldss;
                                                                                ESP = oldsp2;
                                                                                return;
                                                                        }
                                                                }
                                                        }
//                                                        if (output) pclog("Stack %04X\n",SP);
//                                                        if (count) x86abort("Call gate with count\n");
//                                                        PUSHW(oldcs);
//                                                       PUSHW(oldpc); if (cpu_state.abrt) return;
                                                }
                                                cycles -= timing_call_pm_gate_inner;
                                                break;
                                        }
                                        else if (DPL > CPL)
                                        {
                                                // pclog("Call gate DPL > CPL");
                                                x86gpf(NULL,seg2&~3);
                                                return;
                                        }
                                        case 0x1C00: case 0x1D00: case 0x1E00: case 0x1F00: /*Conforming*/
/*                                        if (type==0xC00)
                                        {
                                                PUSHL(oldcs);
                                                PUSHL(oldpc); if (cpu_state.abrt) return;
                                        }
                                        else
                                        {
                                                PUSHW(oldcs);
                                                PUSHW(oldpc); if (cpu_state.abrt) return;
                                        }*/
                                        CS=seg2;
                                        do_seg_load(&_cs, segdat);
                                        if (CPL==3 && oldcpl!=3) flushmmucache_cr3();
                                        use32=(segdat[3]&0x40)?0x300:0;
                                                cpu_state.pc=newpc;

#ifdef CS_ACCESSED                                                
                                        cpl_override = 1;
                                        writememw(0, addr+4, segdat[2] | 0x100); /*Set accessed bit*/
                                        cpl_override = 0;
#endif
                                        cycles -= timing_call_pm_gate;
                                        break;
                                        
                                        default:
                                        // pclog("Call gate bad segment type\n");
                                        x86gpf(NULL,seg2&~3);
                                        return;
                                }
                                break;

//                                case 0x900: /*386 Task gate*/
//                                case 0xB00: /*386 Busy task gate*/
//                                if (optype==JMP) pclog("Task switch!\n");
//                                taskswitch386(seg,segdat);
//                                return;


                                default:
                                // pclog("Bad CALL special descriptor %03X\n",segdat[2]&0xF00);
                                x86gpf(NULL,seg&~3);
                                return;
//                                dumpregs();
//                                exit(-1);
                        }
                }
//                pclog("CS = %04X base=%06X limit=%04X access=%02X  %04X\n",CS,cs,_cs.limit,_cs.access,addr);
//                dumpregs();
//                exit(-1);
        }
        else
        {
                _cs.base=seg<<4;
                _cs.limit=0xFFFF;
                _cs.limit_low = 0;
                _cs.limit_high = 0xffff;
                CS=seg;
                if (eflags&VM_FLAG) _cs.access=(3<<5) | 2 | 0x80;
                else                _cs.access=(0<<5) | 2 | 0x80;
                if (CPL==3 && oldcpl!=3) flushmmucache_cr3();
        }
}

void pmoderetf(int is32, uint16_t off)
{
        uint32_t newpc;
        uint32_t newsp;
        uint32_t addr, oaddr;
        uint16_t segdat[4],segdat2[4],seg,newss;
        uint32_t oldsp=ESP;
        if (output) pclog("RETF %i %04X:%04X  %08X %04X\n",is32,CS,cpu_state.pc,cr0,eflags);
        if (is32)
        {
                newpc=POPL();
                seg=POPL(); if (cpu_state.abrt) return;
        }
        else
        {
                if (output) pclog("PC read from %04X:%04X\n",SS,SP);
                newpc=POPW();
                if (output) pclog("CS read from %04X:%04X\n",SS,SP);
                seg=POPW(); if (cpu_state.abrt) return;
        }
        if (output) pclog("Return to %04X:%08X\n",seg,newpc);
        if ((seg&3)<CPL)
        {
                // pclog("RETF RPL<CPL %04X %i %i %04X:%08X\n",seg,CPL,ins,CS,cpu_state.pc);
//                output=3;
//                timetolive=100;
//                dumpregs();
//                exit(-1);
                ESP=oldsp;
                x86gpf("pmoderetf(): seg < CPL",seg&~3);
                return;
        }
        if (!(seg&~3))
        {
                // pclog("Trying to load CS with NULL selector! retf\n");
//                dumpregs();
//                exit(-1);
                x86gpf(NULL,0);
                return;
        }
        addr=seg&~7;
        if (seg&4)
        {
                if (addr>=ldt.limit)
                {
                        // pclog("Bigger than LDT limit %04X %04X RETF\n",seg,ldt.limit);
                        x86gpf(NULL,seg&~3);
                        return;
                }
                addr+=ldt.base;
        }
        else
        {
                if (addr>=gdt.limit)
                {
                        // pclog("Bigger than GDT limit %04X %04X RETF\n",seg,gdt.limit);
                        x86gpf(NULL,seg&~3);
//                        dumpregs();
//                        exit(-1);
                        return;
                }
                addr+=gdt.base;
        }
        cpl_override=1;
        segdat[0]=readmemw(0,addr);
        segdat[1]=readmemw(0,addr+2);
        segdat[2]=readmemw(0,addr+4);
        segdat[3]=readmemw(0,addr+6); cpl_override=0; if (cpu_state.abrt) { ESP=oldsp; return; }
        oaddr = addr;
        
        if (output) pclog("CPL %i RPL %i %i\n",CPL,seg&3,is32);

        if (stack32) ESP+=off;
        else         SP+=off;

        if (CPL==(seg&3))
        {
                if (output) pclog("RETF CPL = RPL  %04X\n", segdat[2]);
                switch (segdat[2]&0x1F00)
                {
                        case 0x1800: case 0x1900: case 0x1A00: case 0x1B00: /*Non-conforming*/
                        if (CPL != DPL)
                        {
                                // pclog("RETF non-conforming CPL != DPL\n");
                                ESP=oldsp;
                                x86gpf(NULL,seg&~3);
                                return;
                        }
                        break;
                        case 0x1C00: case 0x1D00: case 0x1E00: case 0x1F00: /*Conforming*/
                        if (CPL < DPL)
                        {
                                // pclog("RETF non-conforming CPL < DPL\n");
                                ESP=oldsp;
                                x86gpf(NULL,seg&~3);
                                return;
                        }
                        break;
                        default:
                        // pclog("RETF CS not code segment\n");
                        x86gpf(NULL,seg&~3);
                        return;
                }
                if (!(segdat[2]&0x8000))
                {
                        // pclog("RETF CS not present %i  %04X %04X %04X\n",ins, segdat[0], segdat[1], segdat[2]);
                        ESP=oldsp;
                        x86np("RETF CS not present\n", seg & 0xfffc);
                        return;
                }
                
#ifdef CS_ACCESSED
                cpl_override = 1;
                writememw(0, addr+4, segdat[2] | 0x100); /*Set accessed bit*/
                cpl_override = 0;
#endif
                                
                cpu_state.pc=newpc;
                if (segdat[2] & 0x400)
                   segdat[2] = (segdat[2] & ~(3 << (5+8))) | ((seg & 3) << (5+8));
                CS = seg;
                do_seg_load(&_cs, segdat);
                _cs.access = (_cs.access & ~(3 << 5)) | ((CS & 3) << 5);
                if (CPL==3 && oldcpl!=3) flushmmucache_cr3();
                use32=(segdat[3]&0x40)?0x300:0;
                
//                pclog("CPL=RPL return to %04X:%08X\n",CS,cpu_state.pc);
                cycles -= timing_retf_pm;
        }
        else
        {
                switch (segdat[2]&0x1F00)
                {
                        case 0x1800: case 0x1900: case 0x1A00: case 0x1B00: /*Non-conforming*/
                        if ((seg&3) != DPL)
                        {
                                // pclog("RETF non-conforming RPL != DPL\n");
                                ESP=oldsp;
                                x86gpf(NULL,seg&~3);
                                return;
                        }
                        if (output) pclog("RETF non-conforming, %i %i\n",seg&3, DPL);
                        break;
                        case 0x1C00: case 0x1D00: case 0x1E00: case 0x1F00: /*Conforming*/
                        if ((seg&3) < DPL)
                        {
                                // pclog("RETF non-conforming RPL < DPL\n");
                                ESP=oldsp;
                                x86gpf(NULL,seg&~3);
                                return;
                        }
                        if (output) pclog("RETF conforming, %i %i\n",seg&3, DPL);
                        break;
                        default:
                        // pclog("RETF CS not code segment\n");
                        ESP=oldsp;
                        x86gpf(NULL,seg&~3);
                        return;
                }
                if (!(segdat[2]&0x8000))
                {
                        // pclog("RETF CS not present! %i  %04X %04X %04X\n",ins, segdat[0], segdat[1], segdat[2]);

                        ESP=oldsp;
                        x86np("RETF CS not present\n", seg & 0xfffc);
                        return;
                }
                if (is32)
                {
                        newsp=POPL();
                        newss=POPL(); if (cpu_state.abrt) return;
//                        pclog("is32 new stack %04X:%04X\n",newss,newsp);
                }
                else
                {
                        if (output) pclog("SP read from %04X:%04X\n",SS,SP);
                        newsp=POPW();
                        if (output) pclog("SS read from %04X:%04X\n",SS,SP);
                        newss=POPW(); if (cpu_state.abrt) return;
//                        pclog("!is32 new stack %04X:%04X\n",newss,newsp);
                }
                if (output) pclog("Read new stack : %04X:%04X (%08X)\n", newss, newsp, ldt.base);
                if (!(newss&~3))
                {
                        // pclog("RETF loading null SS\n");
                        ESP=oldsp;
                        x86gpf(NULL,newss&~3);
                        return;
                }
                addr=newss&~7;
                if (newss&4)
                {
                        if (addr>=ldt.limit)
                        {
                                // pclog("Bigger than LDT limit %04X %04X RETF SS\n",newss,gdt.limit);
                                ESP=oldsp;
                                x86gpf(NULL,newss&~3);
                                return;
                        }
                        addr+=ldt.base;
                }
                else
                {
                        if (addr>=gdt.limit)
                        {
                                // pclog("Bigger than GDT limit %04X %04X RETF SS\n",newss,gdt.limit);
                                ESP=oldsp;
                                x86gpf(NULL,newss&~3);
                                return;
                        }
                        addr+=gdt.base;
                }
                cpl_override=1;
                segdat2[0]=readmemw(0,addr);
                segdat2[1]=readmemw(0,addr+2);
                segdat2[2]=readmemw(0,addr+4);
                segdat2[3]=readmemw(0,addr+6); cpl_override=0; if (cpu_state.abrt) { ESP=oldsp; return; }
                if (output) pclog("Segment data %04X %04X %04X %04X\n", segdat2[0], segdat2[1], segdat2[2], segdat2[3]);
//                if (((newss & 3) != DPL) || (DPL2 != DPL))
                if ((newss & 3) != (seg & 3))
                {
                        // pclog("RETF loading SS with wrong permissions %i %i  %04X %04X\n", newss & 3, seg & 3, newss, seg);
                        ESP=oldsp;
//                        output = 3;
//                        dumpregs();
//                        exit(-1);
                        x86gpf(NULL,newss&~3);
                        return;
                }
                if ((segdat2[2]&0x1A00)!=0x1200)
                {
                        // pclog("RETF loading SS wrong type\n");
                        ESP=oldsp;
//                        dumpregs();
//                        exit(-1);
                        x86gpf(NULL,newss&~3);
                        return;
                }
                if (!(segdat2[2]&0x8000))
                {
                        // pclog("RETF loading SS not present\n");
                        ESP=oldsp;
                        x86np("RETF loading SS not present\n", newss & 0xfffc);
                        return;
                }
                if (DPL2 != (seg & 3))
                {
                        // pclog("RETF loading SS with wrong permissions2 %i %i  %04X %04X\n", DPL2, seg & 3, newss, seg);
                        ESP=oldsp;
                        x86gpf(NULL,newss&~3);
                        return;
                }
                SS=newss;
                stack32 = (segdat2[3] & 0x40) ? 1 : 0;
                if (stack32) ESP=newsp;
                else         SP=newsp;
                do_seg_load(&_ss, segdat2);

#ifdef SEL_ACCESSED
                cpl_override = 1;
                writememw(0, addr+4, segdat2[2] | 0x100); /*Set accessed bit*/

#ifdef CS_ACCESSED
                writememw(0, oaddr+4, segdat[2] | 0x100); /*Set accessed bit*/
#endif
                cpl_override = 0;
#endif                
                        /*Conforming segments don't change CPL, so CPL = RPL*/
                        if (segdat[2]&0x400)
                           segdat[2] = (segdat[2] & ~(3 << (5+8))) | ((seg & 3) << (5+8));

                cpu_state.pc=newpc;
                CS=seg;
                do_seg_load(&_cs, segdat);
                if (CPL==3 && oldcpl!=3) flushmmucache_cr3();
                use32=(segdat[3]&0x40)?0x300:0;
                
                if (stack32) ESP+=off;
                else         SP+=off;
                
                check_seg_valid(&_ds);
                check_seg_valid(&_es);
                check_seg_valid(&_fs);
                check_seg_valid(&_gs);
//                pclog("CPL<RPL return to %04X:%08X %04X:%08X\n",CS,cpu_state.pc,SS,ESP);
                cycles -= timing_retf_pm_outer;
        }
}

void restore_stack()
{
        ss=oldss; _ss.limit=oldsslimit;
}

void pmodeint(int num, int soft)
{
        uint16_t segdat[4],segdat2[4],segdat3[4];
        uint32_t addr, oaddr;
        uint16_t newss;
        uint32_t oldss,oldsp;
        int type;
        uint32_t newsp;
        uint16_t seg;
        int stack_changed=0;
        int new_cpl;
        
//        if (!num) pclog("Pmode int 0 at %04X(%06X):%08X\n",CS,cs,cpu_state.pc);
//        pclog("Pmode int %02X %i %04X:%08X %04X:%08X %i\n",num,soft,CS,pc, SS, ESP, cpu_state.abrt);
        if (eflags&VM_FLAG && IOPL!=3 && soft)
        {
                if (output) pclog("V86 banned int\n");
                // pclog("V86 banned int!\n");
                x86gpf(NULL,0);
                return;
//                dumpregs();
//                exit(-1);
        }
        addr=(num<<3);
        if (addr>=idt.limit)
        {
                if (num==8)
                {
                        /*Triple fault - reset!*/
                        // pclog("Triple fault!\n");
//                        output=1;
                        softresetx86();
                }
                else if (num==0xD)
                {
                        // pclog("Double fault!\n");
                        pmodeint(8,0);
                }
                else
                {
                        // pclog("INT out of range\n");
                        x86gpf(NULL,(num*8)+2+(soft)?0:1);
                }
                if (output) pclog("addr >= IDT.limit\n");
                return;
        }
        addr+=idt.base;
        cpl_override=1;
        segdat[0]=readmemw(0,addr);
        segdat[1]=readmemw(2,addr);
        segdat[2]=readmemw(4,addr);
        segdat[3]=readmemw(6,addr); cpl_override=0; if (cpu_state.abrt) { /* pclog("Abrt reading from %08X\n",addr); */ return; }
        oaddr = addr;

        if (output) pclog("Addr %08X seg %04X %04X %04X %04X\n",addr,segdat[0],segdat[1],segdat[2],segdat[3]);
        if (!(segdat[2]&0x1F00))
        {
                // pclog("No seg\n");
                x86gpf(NULL,(num*8)+2);
                return;
        }
        if (DPL<CPL && soft)
        {
                // pclog("INT : DPL<CPL  %04X:%08X  %i %i %04X\n",CS,cpu_state.pc,DPL,CPL,segdat[2]);
                x86gpf(NULL,(num*8)+2);
                return;
        }
        type=segdat[2]&0x1F00;
//        if (output) pclog("Gate type %04X\n",type);
        switch (type)
        {
                case 0x600: case 0x700: case 0xE00: case 0xF00: /*Interrupt and trap gates*/
                        intgatesize=(type>=0x800)?32:16;
//                        if (output) pclog("Int gate %04X %i oldpc %04X pc %04X\n",type,intgatesize,oldpc,cpu_state.pc);
                        if (!(segdat[2]&0x8000))
                        {
                                // pclog("Int gate not present\n");
                                x86np("Int gate not present\n", (num << 3) | 2);
                                return;
                        }
                        seg=segdat[1];
                        new_cpl = seg & 3;
//                        pclog("Interrupt gate : %04X:%04X%04X\n",seg,segdat[3],segdat[0]);
                        
                        addr=seg&~7;
                        if (seg&4)
                        {
                                if (addr>=ldt.limit)
                                {
                                        // pclog("Bigger than LDT limit %04X %04X INT\n",seg,gdt.limit);
                                        x86gpf(NULL,seg&~3);
                                        return;
                                }
                                addr+=ldt.base;
                        }
                        else
                        {
                                if (addr>=gdt.limit)
                                {
                                        // pclog("Bigger than GDT limit %04X %04X INT %i\n",seg,gdt.limit,ins);
                                        x86gpf(NULL,seg&~3);
                                        return;
                                }
                                addr+=gdt.base;
                        }
/*                        if ((seg&3) < CPL)
                        {
                                // pclog("INT to higher level\n");
                                x86gpf(NULL,seg&~3);
                                return;
                        }*/
                        cpl_override=1;
                        segdat2[0]=readmemw(0,addr);
                        segdat2[1]=readmemw(0,addr+2);
                        segdat2[2]=readmemw(0,addr+4);
                        segdat2[3]=readmemw(0,addr+6); cpl_override=0; if (cpu_state.abrt) return;
                        oaddr = addr;
                        
                        if (DPL2 > CPL)
                        {
                                // pclog("INT to higher level 2\n");
                                x86gpf(NULL,seg&~3);
                                return;
                        }
                        //pclog("Type %04X\n",segdat2[2]);
                        switch (segdat2[2]&0x1F00)
                        {
                                case 0x1800: case 0x1900: case 0x1A00: case 0x1B00: /*Non-conforming*/
                                if (DPL2<CPL)
                                {
                                        stack_changed=1;
                                        if (!(segdat2[2]&0x8000))
                                        {
                                                // pclog("Int gate CS not present\n");
                                                x86np("Int gate CS not present\n", segdat[1] & 0xfffc);
                                                return;
                                        }
                                        if ((eflags&VM_FLAG) && DPL2)
                                        {
                                                // pclog("V86 calling int gate, DPL != 0\n");
                                                x86gpf(NULL,segdat[1]&0xFFFC);
                                                return;
                                        }
                                        /*Load new stack*/
                                        oldss=SS;
                                        oldsp=ESP;
                                        cpl_override=1;
                                        if (tr.access&8)
                                        {
                                                addr = 4 + tr.base + (DPL2 * 8);
                                                newss=readmemw(0,addr+4);
                                                newsp=readmeml(0,addr);
                                        }
                                        else
                                        {
                                                addr = 2 + tr.base + (DPL2 * 8);
                                                newss=readmemw(0,addr+2);
                                                newsp=readmemw(0,addr);
                                        }
                                        cpl_override=0;
                                        if (!(newss&~3))
                                        {
                                                // pclog("Int gate loading null SS\n");
                                                x86ss(NULL,newss&~3);
                                                return;
                                        }
                                        addr=newss&~7;
                                        if (newss&4)
                                        {
                                                if (addr>=ldt.limit)
                                                {
                                                        // pclog("Bigger than LDT limit %04X %04X PMODEINT SS\n",newss,gdt.limit);
                                                        x86ss(NULL,newss&~3);
                                                        return;
                                                }
                                                addr+=ldt.base;
                                        }
                                        else
                                        {
                                                if (addr>=gdt.limit)
                                                {
                                                        // pclog("Bigger than GDT limit %04X %04X CSC\n",newss,gdt.limit);
                                                        x86ss(NULL,newss&~3);
                                                        return;
                                                }
                                                addr+=gdt.base;
                                        }
                                        cpl_override=1;
                                        segdat3[0]=readmemw(0,addr);
                                        segdat3[1]=readmemw(0,addr+2);
                                        segdat3[2]=readmemw(0,addr+4);
                                        segdat3[3]=readmemw(0,addr+6); cpl_override=0; if (cpu_state.abrt) return;
                                        if (((newss & 3) != DPL2) || (DPL3 != DPL2))
                                        {
                                                // pclog("Int gate loading SS with wrong permissions\n");
                                                x86ss(NULL,newss&~3);
                                                return;
                                        }
                                        if ((segdat3[2]&0x1A00)!=0x1200)
                                        {
                                                // pclog("Int gate loading SS wrong type\n");
                                                x86ss(NULL,newss&~3);
                                                return;
                                        }
                                        if (!(segdat3[2]&0x8000))
                                        {
                                                // pclog("Int gate loading SS not present\n");
                                                x86np("Int gate loading SS not present\n", newss & 0xfffc);
                                                return;
                                        }
                                        SS=newss;
                                        stack32 = (segdat3[3] & 0x40) ? 1 : 0;
                                        if (stack32) ESP=newsp;
                                        else         SP=newsp;
                                        do_seg_load(&_ss, segdat3);

#ifdef CS_ACCESSED                                        
                                        cpl_override = 1;
                                        writememw(0, addr+4, segdat3[2] | 0x100); /*Set accessed bit*/
                                        cpl_override = 0;
#endif
                                        
                                        if (output) pclog("New stack %04X:%08X\n",SS,ESP);
                                        cpl_override=1;
                                        if (type>=0x800)
                                        {
//                                                if (output) pclog("Push 32 %i\n",eflags&VM_FLAG);
                                                if (eflags & VM_FLAG)
                                                {
                                                        PUSHL(GS);
                                                        PUSHL(FS);
                                                        PUSHL(DS);
                                                        PUSHL(ES); if (cpu_state.abrt) return;
                                                        loadseg(0,&_ds);
                                                        loadseg(0,&_es);
                                                        loadseg(0,&_fs);
                                                        loadseg(0,&_gs);
                                                }
                                                PUSHL(oldss);
                                                PUSHL(oldsp);
                                                PUSHL(flags|(eflags<<16));
//                                                if (soft) pclog("Pushl CS %08X\n", CS);
                                                PUSHL(CS);
//                                                if (soft) pclog("Pushl PC %08X\n", cpu_state.pc);                                                
                                                PUSHL(cpu_state.pc); if (cpu_state.abrt) return;
//                                                if (output) pclog("32Stack %04X:%08X\n",SS,ESP);
                                        }
                                        else
                                        {
//                                                if (output) pclog("Push 16\n");
                                                PUSHW(oldss);
                                                PUSHW(oldsp);
                                                PUSHW(flags);
//                                                if (soft) pclog("Pushw CS %04X\n", CS);                                                
                                                PUSHW(CS);
//                                                if (soft) pclog("Pushw pc %04X\n", cpu_state.pc);                                                
                                                PUSHW(cpu_state.pc); if (cpu_state.abrt) return;
//                                                if (output) pclog("16Stack %04X:%08X\n",SS,ESP);
                                        }
                                        cpl_override=0;
                                        _cs.access=0 | 0x80;
                                        cycles -= timing_int_pm_outer - timing_int_pm;
//                                        pclog("Non-confirming int gate, CS = %04X\n");
                                        break;
                                }
                                else if (DPL2!=CPL)
                                {
                                        // pclog("Non-conforming int gate DPL != CPL\n");
                                        x86gpf(NULL,seg&~3);
                                        return;
                                }
                                case 0x1C00: case 0x1D00: case 0x1E00: case 0x1F00: /*Conforming*/
                                if (!(segdat2[2]&0x8000))
                                {
                                        // pclog("Int gate CS not present\n");
                                        x86np("Int gate CS not present\n", segdat[1] & 0xfffc);
                                        return;
                                }
                                if ((eflags & VM_FLAG) && DPL2<CPL)
                                {
                                        // pclog("Int gate V86 mode DPL2<CPL\n");
                                        x86gpf(NULL,seg&~3);
                                        return;
                                }
//                                if (!stack_changed && ssegs) restore_stack();
                                if (type>0x800)
                                {
                                        PUSHL(flags|(eflags<<16));
//                                        if (soft) pclog("Pushlc CS %08X\n", CS);
                                        PUSHL(CS);
//                                        if (soft) pclog("Pushlc PC %08X\n", cpu_state.pc);
                                        PUSHL(cpu_state.pc); if (cpu_state.abrt) return;
                                }
                                else
                                {
                                        PUSHW(flags);
//                                        if (soft) pclog("Pushwc CS %04X\n", CS);
                                        PUSHW(CS);
//                                        if (soft) pclog("Pushwc PC %04X\n", cpu_state.pc);
                                        PUSHW(cpu_state.pc); if (cpu_state.abrt) return;
                                }
                                new_cpl = CS & 3;
                                break;
                                default:
                                // pclog("Int gate CS not code segment - %04X %04X %04X %04X\n",segdat2[0],segdat2[1],segdat2[2],segdat2[3]);
                                x86gpf(NULL,seg&~3);
                                return;
                        }
                do_seg_load(&_cs, segdat2);
                CS = (seg & ~3) | new_cpl;
                _cs.access = (_cs.access & ~(3 << 5)) | (new_cpl << 5);
//                pclog("New CS = %04X\n",CS);
                if (CPL==3 && oldcpl!=3) flushmmucache_cr3();
                if (type>0x800) cpu_state.pc=segdat[0]|(segdat[3]<<16);
                else            cpu_state.pc=segdat[0];
                use32=(segdat2[3]&0x40)?0x300:0;
//                pclog("Int gate done!\n");

#ifdef CS_ACCESSED
                cpl_override = 1;
                writememw(0, oaddr+4, segdat2[2] | 0x100); /*Set accessed bit*/
                cpl_override = 0;
#endif
                        
                eflags&=~VM_FLAG;
                if (!(type&0x100))
                {
                        flags&=~I_FLAG;
//                        pclog("INT %02X disabling interrupts %i\n",num,soft);
                }
                flags&=~(T_FLAG|NT_FLAG);
//                if (output) pclog("Final Stack %04X:%08X\n",SS,ESP);
                cycles -= timing_int_pm;
                break;
                
                case 0x500: /*Task gate*/
//                pclog("Task gate\n");
                seg=segdat[1];
                        addr=seg&~7;
                        if (seg&4)
                        {
                                if (addr>=ldt.limit)
                                {
                                        // pclog("Bigger than LDT limit %04X %04X INT\n",seg,gdt.limit);
                                        x86gpf(NULL,seg&~3);
                                        return;
                                }
                                addr+=ldt.base;
                        }
                        else
                        {
                                if (addr>=gdt.limit)
                                {
                                        // pclog("Bigger than GDT limit %04X %04X INT %i\n",seg,gdt.limit,ins);
                                        x86gpf(NULL,seg&~3);
                                        return;
                                }
                                addr+=gdt.base;
                        }
                        cpl_override=1;
                        segdat2[0]=readmemw(0,addr);
                        segdat2[1]=readmemw(0,addr+2);
                        segdat2[2]=readmemw(0,addr+4);
                        segdat2[3]=readmemw(0,addr+6);
                        cpl_override=0; if (cpu_state.abrt) return;
                                if (!(segdat2[2]&0x8000))
                                {
                                        // pclog("Int task gate not present\n");
                                        x86np("Int task gate not present\n", segdat[1] & 0xfffc);
                                        return;
                                }
                optype=OPTYPE_INT;
                cpl_override=1;
                taskswitch286(seg,segdat2,segdat2[2]&0x800);
                cpl_override=0;
                break;
                
                default:
                // pclog("Bad int gate type %04X   %04X %04X %04X %04X\n",segdat[2]&0x1F00,segdat[0],segdat[1],segdat[2],segdat[3]);
                x86gpf(NULL,seg&~3);
                return;
        }
}

void pmodeiret(int is32)
{
        uint32_t newsp;
        uint16_t newss;
        uint32_t tempflags,flagmask;
        uint32_t newpc;
        uint16_t segdat[4],segdat2[4];
        uint16_t segs[4];
        uint16_t seg;
        uint32_t addr, oaddr;
        uint32_t oldsp=ESP;
        if (is386 && (eflags&VM_FLAG))
        {
//                if (output) pclog("V86 IRET\n");
                if (IOPL!=3)
                {
                        // pclog("V86 IRET! IOPL!=3\n");
                        x86gpf(NULL,0);
                        return;
                }
                oxpc=cpu_state.pc;
                if (is32)
                {
                        newpc=POPL();
                        seg=POPL();
                        tempflags=POPL(); if (cpu_state.abrt) return;
                }
                else
                {
                        newpc=POPW();
                        seg=POPW();
                        tempflags=POPW(); if (cpu_state.abrt) return;
                }
                cpu_state.pc=newpc;
                _cs.base=seg<<4;
                _cs.limit=0xFFFF;
                _cs.limit_low = 0;
                _cs.limit_high = 0xffff;
		_cs.access |= 0x80;
                CS=seg;
                flags=(flags&0x3000)|(tempflags&0xCFD5)|2;
                cycles -= timing_iret_rm;
                return;
        }

//        pclog("IRET %i\n",is32);
        //flushmmucache();
//        if (output) pclog("Pmode IRET %04X:%04X ",CS,cpu_state.pc);

        if (flags&NT_FLAG)
        {
//                pclog("NT IRET\n");
                seg=readmemw(tr.base,0);
                addr=seg&~7;
                if (seg&4)
                {
                        if (addr>=ldt.limit)
                        {
                                // pclog("TS Bigger than LDT limit %04X %04X IRET\n",seg,gdt.limit);
                                x86gpf(NULL,seg&~3);
                                return;
                        }
                        addr+=ldt.base;
                }
                else
                {
                        if (addr>=gdt.limit)
                        {
                                // pclog("TS Bigger than GDT limit %04X %04X IRET\n",seg,gdt.limit);
                                x86gpf(NULL,seg&~3);
                                return;
                        }
                        addr+=gdt.base;
                }
                cpl_override=1;
                segdat[0]=readmemw(0,addr);
                segdat[1]=readmemw(0,addr+2);
                segdat[2]=readmemw(0,addr+4);
                segdat[3]=readmemw(0,addr+6);
                taskswitch286(seg,segdat,0);
                cpl_override=0;
                return;
        }
        oxpc=cpu_state.pc;
        flagmask=0xFFFF;
        if (CPL) flagmask&=~0x3000;
        if (IOPL<CPL) flagmask&=~0x200;
//        if (output) pclog("IRET %i %i %04X %i\n",CPL,IOPL,flagmask,is32);
        if (is32)
        {
//                pclog("POP\n");
                newpc=POPL();
                seg=POPL();
                tempflags=POPL(); if (cpu_state.abrt) { ESP = oldsp; return; }
//                if (output) pclog("IRETD pop %08X %08X %08X\n",newpc,seg,tempflags);
                if (is386 && ((tempflags>>16)&VM_FLAG))
                {
//                        pclog("IRETD to V86\n");

                        newsp=POPL();
                        newss=POPL();
                        segs[0]=POPL();
                        segs[1]=POPL();
                        segs[2]=POPL();
                        segs[3]=POPL(); if (cpu_state.abrt) { ESP = oldsp; return; }
//                        pclog("Pop stack %04X:%04X\n",newss,newsp);
                        eflags=tempflags>>16;
                        loadseg(segs[0],&_es);
                        do_seg_v86_init(&_es);
                        loadseg(segs[1],&_ds);
                        do_seg_v86_init(&_ds);
                        loadseg(segs[2],&_fs);
                        do_seg_v86_init(&_fs);
                        loadseg(segs[3],&_gs);
                        do_seg_v86_init(&_gs);                        
                        
//                        pclog("V86 IRET %04X:%08X\n",SS,ESP);
//                        output=3;
                        
                        cpu_state.pc=newpc;
                        _cs.base=seg<<4;
                        _cs.limit=0xFFFF;
                        _cs.limit_low = 0;
                        _cs.limit_high = 0xffff;
                        CS=seg;
                        _cs.access=(3<<5) | 2 | 0x80;
                        if (CPL==3 && oldcpl!=3) flushmmucache_cr3();

                        ESP=newsp;
                        loadseg(newss,&_ss);
                        do_seg_v86_init(&_ss);
                        use32=0;
                        flags=(tempflags&0xFFD5)|2;
                        cycles -= timing_iret_v86;
//                        pclog("V86 IRET to %04X:%04X  %04X:%04X  %04X %04X %04X %04X %i\n",CS,cpu_state.pc,SS,SP,DS,ES,FS,GS,cpu_state.abrt);
  //                      if (CS==0xFFFF && pc==0xFFFFFFFF) timetolive=12;
/*                        {
                                dumpregs();
                                exit(-1);
                        }*/
                        return;
                }
        }
        else
        {
                newpc=POPW();
                seg=POPW();
                tempflags=POPW(); if (cpu_state.abrt) { ESP = oldsp; return; }
        }
//        if (!is386) tempflags&=0xFFF;
//        pclog("Returned to %04X:%08X %04X %04X %i\n",seg,newpc,flags,tempflags, ins);
        if (!(seg&~3))
        {
                // pclog("IRET CS=0\n");
                ESP = oldsp;
//                dumpregs();
//                exit(-1);
                x86gpf(NULL,0);
                return;
        }

//        if (output) pclog("IRET %04X:%08X\n",seg,newpc);
        addr=seg&~7;
        if (seg&4)
        {
                if (addr>=ldt.limit)
                {
                        // pclog("Bigger than LDT limit %04X %04X IRET\n",seg,gdt.limit);
                        ESP = oldsp;
                        x86gpf(NULL,seg&~3);
                        return;
                }
                addr+=ldt.base;
        }
        else
        {
                if (addr>=gdt.limit)
                {
                        // pclog("Bigger than GDT limit %04X %04X IRET\n",seg,gdt.limit);
                        ESP = oldsp;
                        x86gpf(NULL,seg&~3);
                        return;
                }
                addr+=gdt.base;
        }
        if ((seg&3) < CPL)
        {
                // pclog("IRET to lower level\n");
                ESP = oldsp;
                x86gpf(NULL,seg&~3);
                return;
        }
        cpl_override=1;
        segdat[0]=readmemw(0,addr);
        segdat[1]=readmemw(0,addr+2);
        segdat[2]=readmemw(0,addr+4);
        segdat[3]=readmemw(0,addr+6); cpl_override=0; if (cpu_state.abrt) { ESP = oldsp; return; }
//        pclog("Seg type %04X %04X\n",segdat[2]&0x1F00,segdat[2]);
        
        switch (segdat[2]&0x1F00)
        {
                case 0x1800: case 0x1900: case 0x1A00: case 0x1B00: /*Non-conforming code*/
                if ((seg&3) != DPL)
                {
                        // pclog("IRET NC DPL  %04X   %04X %04X %04X %04X\n", seg, segdat[0], segdat[1], segdat[2], segdat[3]);
                        ESP = oldsp;
//                        dumpregs();
//                        exit(-1);
                        x86gpf(NULL,seg&~3);
                        return;
                }
                break;
                case 0x1C00: case 0x1D00: case 0x1E00: case 0x1F00: /*Conforming code*/
                if ((seg&3) < DPL)
                {
                        // pclog("IRET C DPL\n");
                        ESP = oldsp;
                        x86gpf(NULL,seg&~3);
                        return;
                }
                break;
                default:
                // pclog("IRET CS != code seg\n");
                ESP = oldsp;
                x86gpf(NULL,seg&~3);
//                dumpregs();
//                exit(-1);
                return;
        }
        if (!(segdat[2]&0x8000))
        {
                // pclog("IRET CS not present %i  %04X %04X %04X\n",ins, segdat[0], segdat[1], segdat[2]);
                ESP = oldsp;
                x86np("IRET CS not present\n", seg & 0xfffc);
                return;
        }
//        pclog("Seg %04X CPL %04X\n",seg,CPL);
        if ((seg&3) == CPL)
        {
//                pclog("Same level\n");
                CS=seg;
                do_seg_load(&_cs, segdat);
                _cs.access = (_cs.access & ~(3 << 5)) | ((CS & 3) << 5);
                if (CPL==3 && oldcpl!=3) flushmmucache_cr3();
                use32=(segdat[3]&0x40)?0x300:0;

#ifdef CS_ACCESSED                
                cpl_override = 1;
                writememw(0, addr+4, segdat[2] | 0x100); /*Set accessed bit*/
                cpl_override = 0;
#endif
                cycles -= timing_iret_pm;
        }
        else /*Return to outer level*/
        {
                oaddr = addr;
                if (output) pclog("Outer level\n");
                if (is32)
                {
                        newsp=POPL();
                        newss=POPL(); if (cpu_state.abrt) { ESP = oldsp; return; }
                }
                else
                {
                        newsp=POPW();
                        newss=POPW(); if (cpu_state.abrt) { ESP = oldsp; return; }
                }
                
                if (output) pclog("IRET load stack %04X:%04X\n",newss,newsp);
                
                if (!(newss&~3))
                {
                        // pclog("IRET loading null SS\n");
                        ESP = oldsp;
                        x86gpf(NULL,newss&~3);
                        return;
                }
                addr=newss&~7;
                if (newss&4)
                {
                        if (addr>=ldt.limit)
                        {
                                // pclog("Bigger than LDT limit %04X %04X PMODEIRET SS\n",newss,gdt.limit);
                                ESP = oldsp;
                                x86gpf(NULL,newss&~3);
                                return;
                        }
                        addr+=ldt.base;
                }
                else
                {
                        if (addr>=gdt.limit)
                        {
                                // pclog("Bigger than GDT limit %04X %04X PMODEIRET\n",newss,gdt.limit);
                                ESP = oldsp;
                                x86gpf(NULL,newss&~3);
                                return;
                        }
                        addr+=gdt.base;
                }
                cpl_override=1;
                segdat2[0]=readmemw(0,addr);
                segdat2[1]=readmemw(0,addr+2);
                segdat2[2]=readmemw(0,addr+4);
                segdat2[3]=readmemw(0,addr+6); cpl_override=0; if (cpu_state.abrt) { ESP = oldsp; return; }
//                pclog("IRET SS sd2 %04X\n",segdat2[2]);
//                if (((newss & 3) != DPL) || (DPL2 != DPL))
                if ((newss & 3) != (seg & 3))
                {
                        // pclog("IRET loading SS with wrong permissions  %04X %04X\n", newss, seg);
                        ESP = oldsp;
//                        dumpregs();
//                        exit(-1);
                        x86gpf(NULL,newss&~3);
                        return;
                }
                if ((segdat2[2]&0x1A00)!=0x1200)
                {
                        // pclog("IRET loading SS wrong type\n");
                        ESP = oldsp;
                        x86gpf(NULL,newss&~3);
                        return;
                }
                if (DPL2 != (seg & 3))
                {
                        // pclog("IRET loading SS with wrong permissions2 %i %i  %04X %04X\n", DPL2, seg & 3, newss, seg);
                        ESP = oldsp;
                        x86gpf(NULL,newss&~3);
                        return;
                }
                if (!(segdat2[2]&0x8000))
                {
                        // pclog("IRET loading SS not present\n");
                        ESP = oldsp;
                        x86np("IRET loading SS not present\n", newss & 0xfffc);
                        return;
                }
                SS=newss;
                stack32 = (segdat2[3] & 0x40) ? 1 : 0;
                if (stack32) ESP=newsp;
                else         SP=newsp;
                do_seg_load(&_ss, segdat2);

#ifdef SEL_ACCESSED
                cpl_override = 1;
                writememw(0, addr+4, segdat2[2] | 0x100); /*Set accessed bit*/

#ifdef CS_ACCESSED
                writememw(0, oaddr+4, segdat[2] | 0x100); /*Set accessed bit*/
#endif
                cpl_override = 0;
#endif                
                        /*Conforming segments don't change CPL, so CPL = RPL*/
                        if (segdat[2]&0x400)
                           segdat[2] = (segdat[2] & ~(3 << (5+8))) | ((seg & 3) << (5+8));

                CS=seg;
                do_seg_load(&_cs, segdat);
                _cs.access = (_cs.access & ~(3 << 5)) | ((CS & 3) << 5);
                if (CPL==3 && oldcpl!=3) flushmmucache_cr3();
                use32=(segdat[3]&0x40)?0x300:0;
                        
                check_seg_valid(&_ds);
                check_seg_valid(&_es);
                check_seg_valid(&_fs);
                check_seg_valid(&_gs);
                cycles -= timing_iret_pm_outer;
        }
        cpu_state.pc=newpc;
        flags=(flags&~flagmask)|(tempflags&flagmask&0xFFD5)|2;
        if (is32) eflags=tempflags>>16;
//        pclog("done\n");
}

void taskswitch286(uint16_t seg, uint16_t *segdat, int is32)
{
        uint32_t base;
        uint32_t limit;
        uint32_t templ;
        uint16_t tempw;

	uint32_t new_cr3=0;
	uint16_t new_es,new_cs,new_ss,new_ds,new_fs,new_gs;
	uint16_t new_ldt;

	uint32_t new_eax,new_ebx,new_ecx,new_edx,new_esp,new_ebp,new_esi,new_edi,new_pc,new_flags;

        uint32_t addr;
        
	uint16_t segdat2[4];

//output=3;
        base=segdat[1]|((segdat[2]&0xFF)<<16)|((segdat[3]>>8)<<24);
        limit=segdat[0]|((segdat[3]&0xF)<<16);
//        pclog("286 Task switch! %04X:%04X\n",CS,cpu_state.pc);
///        pclog("TSS %04X base %08X limit %04X old TSS %04X %08X %i\n",seg,base,limit,tr.seg,tr.base,ins);
// /       pclog("%04X %04X %04X %04X\n",segdat[0],segdat[1],segdat[2],segdat[3]);

        if (is386)
        {
//                if (output) pclog("32-bit TSS\n");
                
                new_cr3=readmeml(base,0x1C);
                new_pc=readmeml(base,0x20);
                new_flags=readmeml(base,0x24);
                
                new_eax=readmeml(base,0x28);
                new_ecx=readmeml(base,0x2C);
                new_edx=readmeml(base,0x30);
                new_ebx=readmeml(base,0x34);
                new_esp=readmeml(base,0x38);
                new_ebp=readmeml(base,0x3C);
                new_esi=readmeml(base,0x40);
                new_edi=readmeml(base,0x44);

                new_es=readmemw(base,0x48);
//                if (output) pclog("Read CS from %08X\n",base+0x4C);
                new_cs=readmemw(base,0x4C);
                new_ss=readmemw(base,0x50);
                new_ds=readmemw(base,0x54);
                new_fs=readmemw(base,0x58);
                new_gs=readmemw(base,0x5C);
                new_ldt=readmemw(base,0x60);
                
                if (cpu_state.abrt) return;
                if (optype==JMP || optype==OPTYPE_INT)
                {
                        if (tr.seg&4) tempw=readmemw(ldt.base,(tr.seg&~7)+4);
                        else          tempw=readmemw(gdt.base,(tr.seg&~7)+4);
                        if (cpu_state.abrt) return;
                        tempw&=~0x200;
                        if (tr.seg&4) writememw(ldt.base,(tr.seg&~7)+4,tempw);
                        else          writememw(gdt.base,(tr.seg&~7)+4,tempw);
                }
                
                if (optype==IRET) flags&=~NT_FLAG;
                
//                if (output) pclog("Write PC %08X %08X\n",tr.base,cpu_state.pc);
                cpu_386_flags_rebuild();
                writememl(tr.base,0x1C,cr3);
                writememl(tr.base,0x20,cpu_state.pc);
                writememl(tr.base,0x24,flags|(eflags<<16));
                
                writememl(tr.base,0x28,EAX);
                writememl(tr.base,0x2C,ECX);
                writememl(tr.base,0x30,EDX);
                writememl(tr.base,0x34,EBX);
                writememl(tr.base,0x38,ESP);
                writememl(tr.base,0x3C,EBP);
                writememl(tr.base,0x40,ESI);
                writememl(tr.base,0x44,EDI);
                
                writememl(tr.base,0x48,ES);
//                if (output) pclog("Write CS %04X to %08X\n",CS,tr.base+0x4C);
                writememl(tr.base,0x4C,CS);
                writememl(tr.base,0x50,SS);
                writememl(tr.base,0x54,DS);
                writememl(tr.base,0x58,FS);
                writememl(tr.base,0x5C,GS);
                writememl(tr.base,0x60,ldt.seg);
                
                if (optype==OPTYPE_INT)
                {
                        writememl(base,0,tr.seg);
                        new_flags|=NT_FLAG;
                }
                if (cpu_state.abrt) return;
                if (optype==JMP || optype==OPTYPE_INT)
                {
                        if (tr.seg&4) tempw=readmemw(ldt.base,(seg&~7)+4);
                        else          tempw=readmemw(gdt.base,(seg&~7)+4);
                        if (cpu_state.abrt) return;
                        tempw|=0x200;
                        if (tr.seg&4) writememw(ldt.base,(seg&~7)+4,tempw);
                        else          writememw(gdt.base,(seg&~7)+4,tempw);
                }
                
                
                
                cr3=new_cr3;
//                pclog("TS New CR3 %08X\n",cr3);
                flushmmucache();
                
                
                
                cpu_state.pc=new_pc;
//                if (output) pclog("New pc %08X\n",new_pc);
                flags=new_flags;
                eflags=new_flags>>16;
                cpu_386_flags_extract();

//                if (output) pclog("Load LDT %04X\n",new_ldt);
                ldt.seg=new_ldt;
                templ=(ldt.seg&~7)+gdt.base;
//                if (output) pclog("Load from %08X %08X\n",templ,gdt.base);
                ldt.limit=readmemw(0,templ);
                if (readmemb(templ+6)&0x80)
                {
                        ldt.limit<<=12;
                        ldt.limit|=0xFFF;
                }
                ldt.base=(readmemw(0,templ+2))|(readmemb(templ+4)<<16)|(readmemb(templ+7)<<24);
//                if (output) pclog("Limit %04X Base %08X\n",ldt.limit,ldt.base);


                if (eflags&VM_FLAG)
                {
                        // pclog("Task switch V86!\n");
                        x86gpf(NULL,0);
                        return;
                }

                if (!(new_cs&~3))
                {
                        // pclog("TS loading null CS\n");
                        x86gpf(NULL,0);
                        return;
                }
                addr=new_cs&~7;
                if (new_cs&4)
                {
                        if (addr>=ldt.limit)
                        {
                                // pclog("Bigger than LDT limit %04X %04X %04X TS\n",new_cs,ldt.limit,addr);
                                x86gpf(NULL,0);
                                return;
                        }
                        addr+=ldt.base;
                }
                else
                {
                        if (addr>=gdt.limit)
                        {
                                // pclog("Bigger than GDT limit %04X %04X TS\n",new_cs,gdt.limit);
                                x86gpf(NULL,0);
                                return;
                        }
                        addr+=gdt.base;
                }
                segdat2[0]=readmemw(0,addr);
                segdat2[1]=readmemw(0,addr+2);
                segdat2[2]=readmemw(0,addr+4);
                segdat2[3]=readmemw(0,addr+6);
                if (!(segdat2[2]&0x8000))
                {
                        // pclog("TS loading CS not present\n");
                        x86np("TS loading CS not present\n", new_cs & 0xfffc);
                        return;
                }
                switch (segdat2[2]&0x1F00)
                {
                        case 0x1800: case 0x1900: case 0x1A00: case 0x1B00: /*Non-conforming*/
                        if ((new_cs&3) != DPL2)
                        {
                                // pclog("TS load CS non-conforming RPL != DPL");
                                x86gpf(NULL,new_cs&~3);
                                return;
                        }
                        break;
                        case 0x1C00: case 0x1D00: case 0x1E00: case 0x1F00: /*Conforming*/
                        if ((new_cs&3) < DPL2)
                        {
                                // pclog("TS load CS non-conforming RPL < DPL");
                                x86gpf(NULL,new_cs&~3);
                                return;
                        }
                        break;
                        default:
                        // pclog("TS load CS not code segment\n");
                        x86gpf(NULL,new_cs&~3);
                        return;
                }

//                if (output) pclog("new_cs %04X\n",new_cs);
                CS=new_cs;
                do_seg_load(&_cs, segdat2);
                if (CPL==3 && oldcpl!=3) flushmmucache_cr3();
                use32=(segdat2[3]&0x40)?0x300:0;

                EAX=new_eax;
                ECX=new_ecx;
                EDX=new_edx;
                EBX=new_ebx;
                ESP=new_esp;
                EBP=new_ebp;
                ESI=new_esi;
                EDI=new_edi;

                if (output) pclog("Load ES %04X\n",new_es);
                loadseg(new_es,&_es);
                if (output) pclog("Load SS %04X\n",new_ss);
                loadseg(new_ss,&_ss);
                if (output) pclog("Load DS %04X\n",new_ds);
                loadseg(new_ds,&_ds);
                if (output) pclog("Load FS %04X\n",new_fs);
                loadseg(new_fs,&_fs);
                if (output) pclog("Load GS %04X\n",new_gs);
                loadseg(new_gs,&_gs);

                if (output) pclog("Resuming at %04X:%08X\n",CS,cpu_state.pc);
        }
        else
        {
                // pclog("16-bit TSS\n");
                resetx86();
                //exit(-1);
        }


        tr.seg=seg;
        tr.base=base;
        tr.limit=limit;
        tr.access=segdat[2]>>8;
}

