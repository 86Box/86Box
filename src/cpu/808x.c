/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		808x CPU emulation.
 *
 *		SHR AX,1
 *
 *		4 clocks - fetch opcode
 *		4 clocks - fetch mod/rm
 *		2 clocks - execute              2 clocks - fetch opcode 1
 *		                                2 clocks - fetch opcode 2
 *		                                4 clocks - fetch mod/rm
 *		2 clocks - fetch opcode 1       2 clocks - execute
 *		2 clocks - fetch opcode 2  etc
 *
 * Version:	@(#)808x.c	1.0.10	2017/12/03
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "cpu.h"
#include "x86.h"
#include "../machine/machine.h"
#include "../io.h"
#include "../mem.h"
#include "../rom.h"
#include "../nmi.h"
#include "../pic.h"
#include "../timer.h"
#include "../device.h"		/* for scsi.h */
#include "../keyboard.h"	/* its WRONG to have this in here!! --FvK */
#include "../scsi/scsi.h"	/* its WRONG to have this in here!! --FvK */
#include "../plat.h"


int xt_cpu_multi;
int nmi = 0;
int nmi_auto_clear = 0;

int nextcyc=0;
int cycdiff;
int is8086=0;

int memcycs;
int nopageerrors=0;

void FETCHCOMPLETE();

uint8_t readmembl(uint32_t addr);
void writemembl(uint32_t addr, uint8_t val);
uint16_t readmemwl(uint32_t seg, uint32_t addr);
void writememwl(uint32_t seg, uint32_t addr, uint16_t val);
uint32_t readmemll(uint32_t seg, uint32_t addr);
void writememll(uint32_t seg, uint32_t addr, uint32_t val);

#undef readmemb
#undef readmemw
uint8_t readmemb(uint32_t a)
{
        if (a!=(cs+cpu_state.pc)) memcycs+=4;
        if (readlookup2 == NULL)  return readmembl(a);
        if (readlookup2[(a)>>12]==-1) return readmembl(a);
        else return *(uint8_t *)(readlookup2[(a) >> 12] + (a));
}

uint8_t readmembf(uint32_t a)
{
        if (readlookup2 == NULL)  return readmembl(a);
        if (readlookup2[(a)>>12]==-1) return readmembl(a);
        else return *(uint8_t *)(readlookup2[(a) >> 12] + (a));
}

uint16_t readmemw(uint32_t s, uint16_t a)
{
        if (a!=(cs+cpu_state.pc)) memcycs+=(8>>is8086);
        if (readlookup2 == NULL) return readmemwl(s,a);
        if ((readlookup2[((s)+(a))>>12]==-1 || (s)==0xFFFFFFFF)) return readmemwl(s,a);
        else return *(uint16_t *)(readlookup2[(s + a) >> 12] + s + a);
}

void refreshread() { /*pclog("Refreshread\n"); */FETCHCOMPLETE(); memcycs+=4; }

#undef fetchea
#define fetchea()   { rmdat=FETCH();  \
                    cpu_reg=(rmdat>>3)&7;             \
                    cpu_mod=(rmdat>>6)&3;             \
                    cpu_rm=rmdat&7;                   \
                    if (cpu_mod!=3) fetcheal(); }

void writemembl(uint32_t addr, uint8_t val);
void writememb(uint32_t a, uint8_t v)
{
        memcycs+=4;
        if (writelookup2 == NULL)  writemembl(a,v);
        if (writelookup2[(a)>>12]==-1) writemembl(a,v);
        else *(uint8_t *)(writelookup2[a >> 12] + a) = v;
}
void writememwl(uint32_t seg, uint32_t addr, uint16_t val);
void writememw(uint32_t s, uint32_t a, uint16_t v)
{
        memcycs+=(8>>is8086);
        if (writelookup2 == NULL)  writememwl(s,a,v);
        if (writelookup2[((s)+(a))>>12]==-1 || (s)==0xFFFFFFFF) writememwl(s,a,v);
        else *(uint16_t *)(writelookup2[(s + a) >> 12] + s + a) = v;
}
void writememll(uint32_t seg, uint32_t addr, uint32_t val);
void writememl(uint32_t s, uint32_t a, uint32_t v)
{
        if (writelookup2 == NULL)  writememll(s,a,v);
        if (writelookup2[((s)+(a))>>12]==-1 || (s)==0xFFFFFFFF) writememll(s,a,v);
        else *(uint32_t *)(writelookup2[(s + a) >> 12] + s + a) = v;
}


void dumpregs(int);
uint16_t oldcs;
int oldcpl;

int tempc;
uint8_t opcode;
uint16_t pc2,pc3;
int noint=0;

int output=0;

#if 0
/* Also in mem.c */
int shadowbios=0;
#endif

int ins=0;

int fetchcycles=0,memcycs,fetchclocks;

uint8_t prefetchqueue[6];
uint16_t prefetchpc;
int prefetchw=0;
static __inline uint8_t FETCH()
{
        uint8_t temp;
/*        temp=prefetchqueue[0];
        prefetchqueue[0]=prefetchqueue[1];
        prefetchqueue[1]=prefetchqueue[2];
        prefetchqueue[2]=prefetchqueue[3];
        prefetchqueue[3]=prefetchqueue[4];
        prefetchqueue[4]=prefetchqueue[5];
        if (prefetchw<=((is8086)?4:3))
        {
                prefetchqueue[prefetchw++]=readmembf(cs+prefetchpc); prefetchpc++;
                if (is8086 && (prefetchpc&1))
                {
                        prefetchqueue[prefetchw++]=readmembf(cs+prefetchpc); prefetchpc++;
                }
        }*/

        if (prefetchw==0)
        {
                cycles-=(4-(fetchcycles&3));
                fetchclocks+=(4-(fetchcycles&3));
                fetchcycles=4;
                temp=readmembf(cs+cpu_state.pc);
                prefetchpc = cpu_state.pc = cpu_state.pc + 1;
                if (is8086 && (cpu_state.pc&1))
                {
                        prefetchqueue[0]=readmembf(cs+cpu_state.pc);
                        prefetchpc++;
                        prefetchw++;
                }
        }
        else
        {
                temp=prefetchqueue[0];
                prefetchqueue[0]=prefetchqueue[1];
                prefetchqueue[1]=prefetchqueue[2];
                prefetchqueue[2]=prefetchqueue[3];
                prefetchqueue[3]=prefetchqueue[4];
                prefetchqueue[4]=prefetchqueue[5];
                prefetchw--;
                fetchcycles-=4;
                cpu_state.pc++;
        }
        return temp;
}

static __inline void FETCHADD(int c)
{
        int d;
        if (c<0) return;
        if (prefetchw>((is8086)?4:3)) return;
        d=c+(fetchcycles&3);
        while (d>3 && prefetchw<((is8086)?6:4))
        {
                d-=4;
                if (is8086 && !(prefetchpc&1))
                {
                        prefetchqueue[prefetchw]=readmembf(cs+prefetchpc);
                        prefetchpc++;
                        prefetchw++;
                }
                if (prefetchw<6)
                {
                        prefetchqueue[prefetchw]=readmembf(cs+prefetchpc);
                        prefetchpc++;
                        prefetchw++;
                }
        }
        fetchcycles+=c;
        if (fetchcycles>16) fetchcycles=16;
}

void FETCHCOMPLETE()
{
        if (!(fetchcycles&3)) return;
        if (prefetchw>((is8086)?4:3)) return;
        if (!prefetchw) nextcyc=(4-(fetchcycles&3));
        cycles-=(4-(fetchcycles&3));
        fetchclocks+=(4-(fetchcycles&3));
                if (is8086 && !(prefetchpc&1))
                {
                        prefetchqueue[prefetchw]=readmembf(cs+prefetchpc);
                        prefetchpc++;
                        prefetchw++;
                }
                if (prefetchw<6)
                {
                        prefetchqueue[prefetchw]=readmembf(cs+prefetchpc);
                        prefetchpc++;
                        prefetchw++;
                }
                fetchcycles+=(4-(fetchcycles&3));
}

static __inline void FETCHCLEAR()
{
        prefetchpc=cpu_state.pc;
        prefetchw=0;
        memcycs=cycdiff-cycles;
        fetchclocks=0;
}

static uint16_t getword()
{
        uint8_t temp=FETCH();
        return temp|(FETCH()<<8);
}


/*EA calculation*/

/*R/M - bits 0-2 - R/M   bits 3-5 - Reg   bits 6-7 - mod
  From 386 programmers manual :
r8(/r)                     AL    CL    DL    BL    AH    CH    DH    BH
r16(/r)                    AX    CX    DX    BX    SP    BP    SI    DI
r32(/r)                    EAX   ECX   EDX   EBX   ESP   EBP   ESI   EDI
/digit (Opcode)            0     1     2     3     4     5     6     7
REG =                      000   001   010   011   100   101   110   111
  ����Address
disp8 denotes an 8-bit displacement following the ModR/M byte, to be
sign-extended and added to the index. disp16 denotes a 16-bit displacement
following the ModR/M byte, to be added to the index. Default segment
register is SS for the effective addresses containing a BP index, DS for
other effective addresses.
            �Ŀ �Mod R/M� ���������ModR/M Values in Hexadecimal�������Ŀ

[BX + SI]            000   00    08    10    18    20    28    30    38
[BX + DI]            001   01    09    11    19    21    29    31    39
[BP + SI]            010   02    0A    12    1A    22    2A    32    3A
[BP + DI]            011   03    0B    13    1B    23    2B    33    3B
[SI]             00  100   04    0C    14    1C    24    2C    34    3C
[DI]                 101   05    0D    15    1D    25    2D    35    3D
disp16               110   06    0E    16    1E    26    2E    36    3E
[BX]                 111   07    0F    17    1F    27    2F    37    3F

[BX+SI]+disp8        000   40    48    50    58    60    68    70    78
[BX+DI]+disp8        001   41    49    51    59    61    69    71    79
[BP+SI]+disp8        010   42    4A    52    5A    62    6A    72    7A
[BP+DI]+disp8        011   43    4B    53    5B    63    6B    73    7B
[SI]+disp8       01  100   44    4C    54    5C    64    6C    74    7C
[DI]+disp8           101   45    4D    55    5D    65    6D    75    7D
[BP]+disp8           110   46    4E    56    5E    66    6E    76    7E
[BX]+disp8           111   47    4F    57    5F    67    6F    77    7F

[BX+SI]+disp16       000   80    88    90    98    A0    A8    B0    B8
[BX+DI]+disp16       001   81    89    91    99    A1    A9    B1    B9
[BX+SI]+disp16       010   82    8A    92    9A    A2    AA    B2    BA
[BX+DI]+disp16       011   83    8B    93    9B    A3    AB    B3    BB
[SI]+disp16      10  100   84    8C    94    9C    A4    AC    B4    BC
[DI]+disp16          101   85    8D    95    9D    A5    AD    B5    BD
[BP]+disp16          110   86    8E    96    9E    A6    AE    B6    BE
[BX]+disp16          111   87    8F    97    9F    A7    AF    B7    BF

EAX/AX/AL            000   C0    C8    D0    D8    E0    E8    F0    F8
ECX/CX/CL            001   C1    C9    D1    D9    E1    E9    F1    F9
EDX/DX/DL            010   C2    CA    D2    DA    E2    EA    F2    FA
EBX/BX/BL            011   C3    CB    D3    DB    E3    EB    F3    FB
ESP/SP/AH        11  100   C4    CC    D4    DC    E4    EC    F4    FC
EBP/BP/CH            101   C5    CD    D5    DD    E5    ED    F5    FD
ESI/SI/DH            110   C6    CE    D6    DE    E6    EE    F6    FE
EDI/DI/BH            111   C7    CF    D7    DF    E7    EF    F7    FF

mod = 11 - register
      10 - address + 16 bit displacement
      01 - address + 8 bit displacement
      00 - address

reg = If mod=11,  (depending on data size, 16 bits/8 bits, 32 bits=extend 16 bit registers)
      0=AX/AL   1=CX/CL   2=DX/DL   3=BX/BL
      4=SP/AH   5=BP/CH   6=SI/DH   7=DI/BH

      Otherwise, LSB selects SI/DI (0=SI), NMSB selects BX/BP (0=BX), and MSB
      selects whether BX/BP are used at all (0=used).

      mod=00 is an exception though
      6=16 bit displacement only
      7=[BX]

      Usage varies with instructions.

      MOV AL,BL has ModR/M as C3, for example.
      mod=11, reg=0, r/m=3
      MOV uses reg as dest, and r/m as src.
      reg 0 is AL, reg 3 is BL

      If BP or SP are in address calc, seg is SS, else DS
*/

uint32_t easeg;
int rmdat;

uint16_t zero=0;
uint16_t *mod1add[2][8];
uint32_t *mod1seg[8];

int slowrm[8];

void makemod1table()
{
        mod1add[0][0]=&BX; mod1add[0][1]=&BX; mod1add[0][2]=&BP; mod1add[0][3]=&BP;
        mod1add[0][4]=&SI; mod1add[0][5]=&DI; mod1add[0][6]=&BP; mod1add[0][7]=&BX;
        mod1add[1][0]=&SI; mod1add[1][1]=&DI; mod1add[1][2]=&SI; mod1add[1][3]=&DI;
        mod1add[1][4]=&zero; mod1add[1][5]=&zero; mod1add[1][6]=&zero; mod1add[1][7]=&zero;
        slowrm[0]=0; slowrm[1]=1; slowrm[2]=1; slowrm[3]=0;
        mod1seg[0]=&ds; mod1seg[1]=&ds; mod1seg[2]=&ss; mod1seg[3]=&ss;
        mod1seg[4]=&ds; mod1seg[5]=&ds; mod1seg[6]=&ss; mod1seg[7]=&ds;
}

static void fetcheal()
{
        if (!cpu_mod && cpu_rm==6) { cpu_state.eaaddr=getword(); easeg=ds; FETCHADD(6); }
        else
        {
                switch (cpu_mod)
                {
                        case 0:
                        cpu_state.eaaddr=0;
                        if (cpu_rm&4) FETCHADD(5);
                        else      FETCHADD(7+slowrm[cpu_rm]);
                        break;
                        case 1:
                        cpu_state.eaaddr=(uint16_t)(int8_t)FETCH();
                        if (cpu_rm&4) FETCHADD(9);
                        else      FETCHADD(11+slowrm[cpu_rm]);
                        break;
                        case 2:
                        cpu_state.eaaddr=getword();
                        if (cpu_rm&4) FETCHADD(9);
                        else      FETCHADD(11+slowrm[cpu_rm]);
                        break;
                }
                cpu_state.eaaddr+=(*mod1add[0][cpu_rm])+(*mod1add[1][cpu_rm]);
                easeg=*mod1seg[cpu_rm];
                cpu_state.eaaddr&=0xFFFF;
        }

	cpu_state.last_ea = cpu_state.eaaddr;
}

static __inline uint8_t geteab()
{
        if (cpu_mod == 3)
                return (cpu_rm & 4) ? cpu_state.regs[cpu_rm & 3].b.h : cpu_state.regs[cpu_rm & 3].b.l;
        return readmemb(easeg+cpu_state.eaaddr);
}

static __inline uint16_t geteaw()
{
        if (cpu_mod == 3)
                return cpu_state.regs[cpu_rm].w;
        return readmemw(easeg,cpu_state.eaaddr);
}

#if 0
static __inline uint16_t geteaw2()
{
        if (cpu_mod == 3)
                return cpu_state.regs[cpu_rm].w;
        return readmemw(easeg,(cpu_state.eaaddr+2)&0xFFFF);
}
#endif

static __inline void seteab(uint8_t val)
{
        if (cpu_mod == 3)
        {
                if (cpu_rm & 4)
                        cpu_state.regs[cpu_rm & 3].b.h = val;
                else
                        cpu_state.regs[cpu_rm & 3].b.l = val;
        }
        else
        {
                writememb(easeg+cpu_state.eaaddr,val);
        }
}

static __inline void seteaw(uint16_t val)
{
        if (cpu_mod == 3)
                cpu_state.regs[cpu_rm].w = val;
        else
        {
                writememw(easeg,cpu_state.eaaddr,val);
        }
}

#undef getr8
#define getr8(r)   ((r & 4) ? cpu_state.regs[r & 3].b.h : cpu_state.regs[r & 3].b.l)

#undef setr8
#define setr8(r,v) if (r & 4) cpu_state.regs[r & 3].b.h = v; \
                   else       cpu_state.regs[r & 3].b.l = v;


/*Flags*/
uint8_t znptable8[256];
uint16_t znptable16[65536];

void makeznptable()
{
        int c,d;
        for (c=0;c<256;c++)
        {
                d=0;
                if (c&1) d++;
                if (c&2) d++;
                if (c&4) d++;
                if (c&8) d++;
                if (c&16) d++;
                if (c&32) d++;
                if (c&64) d++;
                if (c&128) d++;
                if (d&1)
		{
                   znptable8[c]=0;
		}
                else
		{
                   znptable8[c]=P_FLAG;
		}
		if (c == 0xb1)  pclog("znp8 b1 = %i %02X\n", d, znptable8[c]);
                if (!c) znptable8[c]|=Z_FLAG;
                if (c&0x80) znptable8[c]|=N_FLAG;
        }
        for (c=0;c<65536;c++)
        {
                d=0;
                if (c&1) d++;
                if (c&2) d++;
                if (c&4) d++;
                if (c&8) d++;
                if (c&16) d++;
                if (c&32) d++;
                if (c&64) d++;
                if (c&128) d++;
                if (d&1)
                   znptable16[c]=0;
                else
                   znptable16[c]=P_FLAG;
                if (c == 0xb1) pclog("znp16 b1 = %i %02X\n", d, znptable16[c]);
                if (c == 0x65b1) pclog("znp16 65b1 = %i %02X\n", d, znptable16[c]);
                if (!c) znptable16[c]|=Z_FLAG;
                if (c&0x8000) znptable16[c]|=N_FLAG;
      }      
}
#if 1
/* Also in mem.c */
int timetolive=0;
#endif

extern uint32_t oldcs2;
extern uint32_t oldpc2;

int indump = 0;

void dumpregs(int force)
{
        int c,d=0,e=0;
#ifndef RELEASE_BUILD
        FILE *f;
#endif

	/* Only dump when needed, and only once.. */
	if (indump || (!force && !dump_on_exit)) return;

#ifndef RELEASE_BUILD
        indump = 1;
        output=0;
	(void)plat_chdir(usr_path);
        nopageerrors=1;
        f=fopen("ram.dmp","wb");
        fwrite(ram,mem_size*1024,1,f);
        fclose(f);
        pclog("Dumping rram.dmp\n");
        f=fopen("rram.dmp","wb");
        for (c=0;c<0x1000000;c++) putc(readmemb(c),f);
        fclose(f);
        pclog("Dumping rram4.dmp\n");
        f=fopen("rram4.dmp","wb");
        for (c=0;c<0x0050000;c++) 
        {
                cpu_state.abrt = 0;
                putc(readmemb386l(0,c+0x80000000),f);
        }
        fclose(f);
        pclog("Dumping done\n");        
#endif
        if (is386)
           pclog("EAX=%08X EBX=%08X ECX=%08X EDX=%08X\nEDI=%08X ESI=%08X EBP=%08X ESP=%08X\n",EAX,EBX,ECX,EDX,EDI,ESI,EBP,ESP);
        else
           pclog("AX=%04X BX=%04X CX=%04X DX=%04X DI=%04X SI=%04X BP=%04X SP=%04X\n",AX,BX,CX,DX,DI,SI,BP,SP);
        pclog("PC=%04X CS=%04X DS=%04X ES=%04X SS=%04X FLAGS=%04X\n",cpu_state.pc,CS,DS,ES,SS,flags);
        pclog("%04X:%04X %04X:%04X\n",oldcs,cpu_state.oldpc, oldcs2, oldpc2);
        pclog("%i ins\n",ins);
        if (is386)
           pclog("In %s mode\n",(msw&1)?((eflags&VM_FLAG)?"V86":"protected"):"real");
        else
           pclog("In %s mode\n",(msw&1)?"protected":"real");
        pclog("CS : base=%06X limit=%08X access=%02X  limit_low=%08X limit_high=%08X\n",cs,_cs.limit,_cs.access, _cs.limit_low, _cs.limit_high);
        pclog("DS : base=%06X limit=%08X access=%02X  limit_low=%08X limit_high=%08X\n",ds,_ds.limit,_ds.access, _ds.limit_low, _ds.limit_high);
        pclog("ES : base=%06X limit=%08X access=%02X  limit_low=%08X limit_high=%08X\n",es,_es.limit,_es.access, _es.limit_low, _es.limit_high);
        if (is386)
        {
                pclog("FS : base=%06X limit=%08X access=%02X  limit_low=%08X limit_high=%08X\n",seg_fs,_fs.limit,_fs.access, _fs.limit_low, _fs.limit_high);
                pclog("GS : base=%06X limit=%08X access=%02X  limit_low=%08X limit_high=%08X\n",gs,_gs.limit,_gs.access, _gs.limit_low, _gs.limit_high);
        }
        pclog("SS : base=%06X limit=%08X access=%02X  limit_low=%08X limit_high=%08X\n",ss,_ss.limit,_ss.access, _ss.limit_low, _ss.limit_high);
        pclog("GDT : base=%06X limit=%04X\n",gdt.base,gdt.limit);
        pclog("LDT : base=%06X limit=%04X\n",ldt.base,ldt.limit);
        pclog("IDT : base=%06X limit=%04X\n",idt.base,idt.limit);
        pclog("TR  : base=%06X limit=%04X\n", tr.base, tr.limit);
        if (is386)
        {
                pclog("386 in %s mode   stack in %s mode\n",(use32)?"32-bit":"16-bit",(stack32)?"32-bit":"16-bit");
                pclog("CR0=%08X CR2=%08X CR3=%08X CR4=%08x\n",cr0,cr2,cr3, cr4);
        }
        pclog("Entries in readlookup : %i    writelookup : %i\n",readlnum,writelnum);
        for (c=0;c<1024*1024;c++)
        {
                if (readlookup2[c]!=0xFFFFFFFF) d++;
                if (writelookup2[c]!=0xFFFFFFFF) e++;
        }
        pclog("Entries in readlookup : %i    writelookup : %i\n",d,e);
        x87_dumpregs();
        indump = 0;
}

int resets = 0;
int x86_was_reset = 0;
void resetx86()
{
        pclog("x86 reset\n");
        resets++;
        ins = 0;
        use32=0;
	cpu_cur_status = 0;
        stack32=0;
	msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
        msw=0;
        if (is486)
                cr0 = 1 << 30;
        else
                cr0 = 0;
        cpu_cache_int_enabled = 0;
        cpu_update_waitstates();
        cr4 = 0;
        eflags=0;
        cgate32=0;
        if(AT)
        {
		loadcs(0xF000);
                cpu_state.pc=0xFFF0;
                rammask = cpu_16bitbus ? 0xFFFFFF : 0xFFFFFFFF;
        }
        else
        {
                loadcs(0xFFFF);
                cpu_state.pc=0;
                rammask = 0xfffff;
        }
        idt.base = 0;
        idt.limit = is386 ? 0x03FF : 0xFFFF;
        flags=2;
        makeznptable();
        resetreadlookup();
        makemod1table();
        resetmcr();
        FETCHCLEAR();
        x87_reset();
        cpu_set_edx();
	EAX = 0;
        ESP=0;
        mmu_perm=4;
        memset(inscounts, 0, sizeof(inscounts));
        x86seg_reset();
#ifdef USE_DYNAREC
        codegen_reset();
#endif
        x86_was_reset = 1;
	port_92_clear_reset();
	scsi_card_reset();
}

void softresetx86()
{
        use32=0;
        stack32=0;
	cpu_cur_status = 0;
	msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
        msw=0;
        cr0=0;
        cr4 = 0;
        eflags=0;
        cgate32=0;
        if(AT)
        {
		loadcs(0xF000);
                cpu_state.pc=0xFFF0;
                rammask = cpu_16bitbus ? 0xFFFFFF : 0xFFFFFFFF;
        }
        else
        {
                loadcs(0xFFFF);
                cpu_state.pc=0;
                rammask = 0xfffff;
        }
        flags=2;
        idt.base = 0;
        idt.limit = is386 ? 0x03FF : 0xFFFF;
        x86seg_reset();
        x86_was_reset = 1;
	port_92_clear_reset();
	scsi_card_reset();
}

static void setznp8(uint8_t val)
{
        flags&=~0xC4;
        flags|=znptable8[val];
}

static void setznp16(uint16_t val)
{
        flags&=~0xC4;
        flags|=znptable16[val];
}

static void setadd8(uint8_t a, uint8_t b)
{
        uint16_t c=(uint16_t)a+(uint16_t)b;
        flags&=~0x8D5;
        flags|=znptable8[c&0xFF];
        if (c&0x100) flags|=C_FLAG;
        if (!((a^b)&0x80)&&((a^c)&0x80)) flags|=V_FLAG;
        if (((a&0xF)+(b&0xF))&0x10)      flags|=A_FLAG;
}
static void setadd8nc(uint8_t a, uint8_t b)
{
        uint16_t c=(uint16_t)a+(uint16_t)b;
        flags&=~0x8D4;
        flags|=znptable8[c&0xFF];
        if (!((a^b)&0x80)&&((a^c)&0x80)) flags|=V_FLAG;
        if (((a&0xF)+(b&0xF))&0x10)      flags|=A_FLAG;
}
static void setadc8(uint8_t a, uint8_t b)
{
        uint16_t c=(uint16_t)a+(uint16_t)b+tempc;
        flags&=~0x8D5;
        flags|=znptable8[c&0xFF];
        if (c&0x100) flags|=C_FLAG;
        if (!((a^b)&0x80)&&((a^c)&0x80)) flags|=V_FLAG;
        if (((a&0xF)+(b&0xF))&0x10)      flags|=A_FLAG;
}
static void setadd16(uint16_t a, uint16_t b)
{
        uint32_t c=(uint32_t)a+(uint32_t)b;
        flags&=~0x8D5;
        flags|=znptable16[c&0xFFFF];
        if (c&0x10000) flags|=C_FLAG;
        if (!((a^b)&0x8000)&&((a^c)&0x8000)) flags|=V_FLAG;
        if (((a&0xF)+(b&0xF))&0x10)      flags|=A_FLAG;
}
static void setadd16nc(uint16_t a, uint16_t b)
{
        uint32_t c=(uint32_t)a+(uint32_t)b;
        flags&=~0x8D4;
        flags|=znptable16[c&0xFFFF];
        if (!((a^b)&0x8000)&&((a^c)&0x8000)) flags|=V_FLAG;
        if (((a&0xF)+(b&0xF))&0x10)      flags|=A_FLAG;
}
static void setadc16(uint16_t a, uint16_t b)
{
        uint32_t c=(uint32_t)a+(uint32_t)b+tempc;
        flags&=~0x8D5;
        flags|=znptable16[c&0xFFFF];
        if (c&0x10000) flags|=C_FLAG;
        if (!((a^b)&0x8000)&&((a^c)&0x8000)) flags|=V_FLAG;
        if (((a&0xF)+(b&0xF))&0x10)      flags|=A_FLAG;
}

static void setsub8(uint8_t a, uint8_t b)
{
        uint16_t c=(uint16_t)a-(uint16_t)b;
        flags&=~0x8D5;
        flags|=znptable8[c&0xFF];
        if (c&0x100) flags|=C_FLAG;
        if ((a^b)&(a^c)&0x80) flags|=V_FLAG;
        if (((a&0xF)-(b&0xF))&0x10)      flags|=A_FLAG;
}
static void setsub8nc(uint8_t a, uint8_t b)
{
        uint16_t c=(uint16_t)a-(uint16_t)b;
        flags&=~0x8D4;
        flags|=znptable8[c&0xFF];
        if ((a^b)&(a^c)&0x80) flags|=V_FLAG;
        if (((a&0xF)-(b&0xF))&0x10)      flags|=A_FLAG;
}
static void setsbc8(uint8_t a, uint8_t b)
{
        uint16_t c=(uint16_t)a-(((uint16_t)b)+tempc);
        flags&=~0x8D5;
        flags|=znptable8[c&0xFF];
        if (c&0x100) flags|=C_FLAG;
        if ((a^b)&(a^c)&0x80) flags|=V_FLAG;
        if (((a&0xF)-(b&0xF))&0x10)      flags|=A_FLAG;
}
static void setsub16(uint16_t a, uint16_t b)
{
        uint32_t c=(uint32_t)a-(uint32_t)b;
        flags&=~0x8D5;
        flags|=znptable16[c&0xFFFF];
        if (c&0x10000) flags|=C_FLAG;
        if ((a^b)&(a^c)&0x8000) flags|=V_FLAG;
        if (((a&0xF)-(b&0xF))&0x10)      flags|=A_FLAG;
}
static void setsub16nc(uint16_t a, uint16_t b)
{
        uint32_t c=(uint32_t)a-(uint32_t)b;
        flags&=~0x8D4;
        flags|=(znptable16[c&0xFFFF]&~4);
        flags|=(znptable8[c&0xFF]&4);
        if ((a^b)&(a^c)&0x8000) flags|=V_FLAG;
        if (((a&0xF)-(b&0xF))&0x10)      flags|=A_FLAG;
}
static void setsbc16(uint16_t a, uint16_t b)
{
        uint32_t c=(uint32_t)a-(((uint32_t)b)+tempc);
        flags&=~0x8D5;
        flags|=(znptable16[c&0xFFFF]&~4);
        flags|=(znptable8[c&0xFF]&4);
        if (c&0x10000) flags|=C_FLAG;
        if ((a^b)&(a^c)&0x8000) flags|=V_FLAG;
        if (((a&0xF)-(b&0xF))&0x10)      flags|=A_FLAG;
}

int current_diff = 0;
void clockhardware()
{
        int diff = cycdiff - cycles - current_diff;
        
        current_diff += diff;
  
        timer_end_period(cycles*xt_cpu_multi);
}

static int takeint = 0;


int firstrepcycle=1;

void rep(int fv)
{
        uint8_t temp = 0;
        int c=CX;
        uint8_t temp2;
        uint16_t tempw,tempw2;
        uint16_t ipc=cpu_state.oldpc;
        int changeds=0;
        uint32_t oldds = 0;
        startrep:
        temp=FETCH();

        switch (temp)
        {
                case 0x08:
                cpu_state.pc=ipc+1;
                cycles-=2;
                FETCHCLEAR();
                break;
                case 0x26: /*ES:*/
                oldds=ds;
                ds=es;
                changeds=1;
                cycles-=2;
                goto startrep;
                break;
                case 0x2E: /*CS:*/
                oldds=ds;
                ds=cs;
                changeds=1;
                cycles-=2;
                goto startrep;
                break;
                case 0x36: /*SS:*/
                oldds=ds;
                ds=ss;
                changeds=1;
                cycles-=2;
                goto startrep;
                break;
                case 0x6E: /*REP OUTSB*/
                if (c>0)
                {
                        temp2=readmemb(ds+SI);
                        outb(DX,temp2);
                        if (flags&D_FLAG) SI--;
                        else              SI++;
                        c--;
                        cycles-=5;
                }
                if (c>0) { firstrepcycle=0; cpu_state.pc=ipc; if (cpu_state.ssegs) cpu_state.ssegs++; FETCHCLEAR(); }
                else firstrepcycle=1;
                break;
                case 0xA4: /*REP MOVSB*/
                while (c>0 && !IRQTEST)
                {
                        temp2=readmemb(ds+SI);
                        writememb(es+DI,temp2);
                        if (flags&D_FLAG) { DI--; SI--; }
                        else              { DI++; SI++; }
                        c--;
                        cycles-=17;
                        clockhardware();
                        FETCHADD(17-memcycs);
                }
                if (IRQTEST && c>0) cpu_state.pc=ipc;
                break;
                case 0xA5: /*REP MOVSW*/
                while (c>0 && !IRQTEST)
                {
                        memcycs=0;
                        tempw=readmemw(ds,SI);
                        writememw(es,DI,tempw);
                        if (flags&D_FLAG) { DI-=2; SI-=2; }
                        else              { DI+=2; SI+=2; }
                        c--;
                        cycles-=17;
                        clockhardware();
                        FETCHADD(17 - memcycs);
                }
                if (IRQTEST && c>0) cpu_state.pc=ipc;
                break;
                case 0xA6: /*REP CMPSB*/
                if (fv) flags|=Z_FLAG;
                else    flags&=~Z_FLAG;
                while ((c>0) && (fv==((flags&Z_FLAG)?1:0)) && !IRQTEST)
                {
                        memcycs=0;
                        temp=readmemb(ds+SI);
                        temp2=readmemb(es+DI);
                        if (flags&D_FLAG) { DI--; SI--; }
                        else              { DI++; SI++; }
                        c--;
                        cycles -= 30;
                        setsub8(temp,temp2);
                        clockhardware();
                        FETCHADD(30 - memcycs);
                }
                if (IRQTEST && c>0 && (fv==((flags&Z_FLAG)?1:0))) cpu_state.pc=ipc;
                break;
                case 0xA7: /*REP CMPSW*/
                if (fv) flags|=Z_FLAG;
                else    flags&=~Z_FLAG;
                while ((c>0) && (fv==((flags&Z_FLAG)?1:0)) && !IRQTEST)
                {
                        memcycs=0;
                        tempw=readmemw(ds,SI);
                        tempw2=readmemw(es,DI);
                        if (flags&D_FLAG) { DI-=2; SI-=2; }
                        else              { DI+=2; SI+=2; }
                        c--;
                        cycles -= 30;
                        setsub16(tempw,tempw2);
                        clockhardware();
                        FETCHADD(30 - memcycs);
                }
                if (IRQTEST && c>0 && (fv==((flags&Z_FLAG)?1:0))) cpu_state.pc=ipc;
                break;
                case 0xAA: /*REP STOSB*/
                while (c>0 && !IRQTEST)
                {
                        memcycs=0;
                        writememb(es+DI,AL);
                        if (flags&D_FLAG) DI--;
                        else              DI++;
                        c--;
                        cycles -= 10;
                        clockhardware();
                        FETCHADD(10 - memcycs);
                }
                if (IRQTEST && c>0) cpu_state.pc=ipc;
                break;
                case 0xAB: /*REP STOSW*/
                while (c>0 && !IRQTEST)
                {
                        memcycs=0;
                        writememw(es,DI,AX);
                        if (flags&D_FLAG) DI-=2;
                        else              DI+=2;
                        c--;
                        cycles -= 10;
                        clockhardware();
                        FETCHADD(10 - memcycs);
                }
                if (IRQTEST && c>0) cpu_state.pc=ipc;
                break;
                case 0xAC: /*REP LODSB*/
                if (c>0)
                {
                        temp2=readmemb(ds+SI);
                        if (flags&D_FLAG) SI--;
                        else              SI++;
                        c--;
                        cycles-=4;
                }
                if (c>0) { firstrepcycle=0; cpu_state.pc=ipc; if (cpu_state.ssegs) cpu_state.ssegs++; FETCHCLEAR(); }
                else firstrepcycle=1;
                break;
                case 0xAD: /*REP LODSW*/
                if (c>0)
                {
                        tempw2=readmemw(ds,SI);
                        if (flags&D_FLAG) SI-=2;
                        else              SI+=2;
                        c--;
                        cycles-=4;
                }
                if (c>0) { firstrepcycle=0; cpu_state.pc=ipc; if (cpu_state.ssegs) cpu_state.ssegs++; FETCHCLEAR(); }
                else firstrepcycle=1;
                break;
                case 0xAE: /*REP SCASB*/
                if (fv) flags|=Z_FLAG;
                else    flags&=~Z_FLAG;
                if ((c>0) && (fv==((flags&Z_FLAG)?1:0)))
                {
                        temp2=readmemb(es+DI);
                        setsub8(AL,temp2);
                        if (flags&D_FLAG) DI--;
                        else              DI++;
                        c--;
                        cycles -= 15;
                }
                if ((c>0) && (fv==((flags&Z_FLAG)?1:0)))  { cpu_state.pc=ipc; firstrepcycle=0; if (cpu_state.ssegs) cpu_state.ssegs++; FETCHCLEAR(); }
                else firstrepcycle=1;
                break;
                case 0xAF: /*REP SCASW*/
                if (fv) flags|=Z_FLAG;
                else    flags&=~Z_FLAG;
                if ((c>0) && (fv==((flags&Z_FLAG)?1:0)))
                {
                        tempw=readmemw(es,DI);
                        setsub16(AX,tempw);
                        if (flags&D_FLAG) DI-=2;
                        else              DI+=2;
                        c--;
                        cycles -= 15;
                }
                if ((c>0) && (fv==((flags&Z_FLAG)?1:0)))  { cpu_state.pc=ipc; firstrepcycle=0; if (cpu_state.ssegs) cpu_state.ssegs++; FETCHCLEAR(); }
                else firstrepcycle=1;
                break;
                default:
                cpu_state.pc = ipc+1;
                        cycles-=20;
                        FETCHCLEAR();
        }
        CX=c;
        if (changeds) ds=oldds;
        if (IRQTEST)
                takeint = 1;
}


int inhlt=0;
uint16_t lastpc,lastcs;
int firstrepcycle;
int skipnextprint=0;

int instime=0;
void execx86(int cycs)
{
        uint8_t temp = 0,temp2;
        uint16_t addr,tempw,tempw2,tempw3,tempw4;
        int8_t offset;
        int tempws;
        uint32_t templ;
        unsigned int c;
        int tempi;
        int trap;

        cycles+=cycs;
        while (cycles>0)
        {
                cycdiff=cycles;
                timer_start_period(cycles*xt_cpu_multi);
                current_diff = 0;
                cycles-=nextcyc;
                nextcyc=0;
                fetchclocks=0;
                oldcs=CS;
                cpu_state.oldpc=cpu_state.pc;
                opcodestart:
                opcode=FETCH();
                tempc=flags&C_FLAG;
                trap=flags&T_FLAG;
                cpu_state.pc--;
                if (output)
                {
                                if (!skipnextprint) pclog("%04X:%04X : %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %02X %04X  %i %p %02X\n",cs,cpu_state.pc,AX,BX,CX,DX,CS,DS,ES,SS,DI,SI,BP,SP,opcode,flags, ins, ram, ram[0x1a925]);
                                skipnextprint=0;
                }
                cpu_state.pc++;
                inhlt=0;
                switch (opcode)
                {
                        case 0x00: /*ADD 8,reg*/
                        fetchea();
                        temp=geteab();
                        setadd8(temp,getr8(cpu_reg));
                        temp+=getr8(cpu_reg);
                        seteab(temp);
                        cycles-=((cpu_mod==3)?3:24);
                        break;
                        case 0x01: /*ADD 16,reg*/
                        fetchea();
                        tempw=geteaw();
                        setadd16(tempw, cpu_state.regs[cpu_reg].w);
                        tempw += cpu_state.regs[cpu_reg].w;
                        seteaw(tempw);
                        cycles-=((cpu_mod==3)?3:24);
                        break;
                        case 0x02: /*ADD cpu_reg,8*/
                        fetchea();
                        temp=geteab();
                        setadd8(getr8(cpu_reg),temp);
                        setr8(cpu_reg,getr8(cpu_reg)+temp);
                        cycles-=((cpu_mod==3)?3:13);
                        break;
                        case 0x03: /*ADD cpu_reg,16*/
                        fetchea();
                        tempw=geteaw();
                        setadd16(cpu_state.regs[cpu_reg].w,tempw);
                        cpu_state.regs[cpu_reg].w+=tempw;
                        cycles-=((cpu_mod==3)?3:13);
                        break;
                        case 0x04: /*ADD AL,#8*/
                        temp=FETCH();
                        setadd8(AL,temp);
                        AL+=temp;
                        cycles-=4;
                        break;
                        case 0x05: /*ADD AX,#16*/
                        tempw=getword();
                        setadd16(AX,tempw);
                        AX+=tempw;
                        cycles-=4;
                        break;

                        case 0x06: /*PUSH ES*/
                        if (cpu_state.ssegs) ss=oldss;
                        writememw(ss,((SP-2)&0xFFFF),ES);
                        SP-=2;
			cpu_state.last_ea = SP;
                        cycles-=14;
                        break;
                        case 0x07: /*POP ES*/
                        if (cpu_state.ssegs) ss=oldss;
                        tempw=readmemw(ss,SP);
                        loadseg(tempw,&_es);
                        SP+=2;
			cpu_state.last_ea = SP;
                        cycles-=12;
                        break;

                        case 0x08: /*OR 8,reg*/
                        fetchea();
                        temp=geteab();
                        temp|=getr8(cpu_reg);
                        setznp8(temp);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        seteab(temp);
                        cycles-=((cpu_mod==3)?3:24);
                        break;
                        case 0x09: /*OR 16,reg*/
                        fetchea();
                        tempw=geteaw();
                        tempw|=cpu_state.regs[cpu_reg].w;
                        setznp16(tempw);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        seteaw(tempw);
                        cycles-=((cpu_mod==3)?3:24);
                        break;
                        case 0x0A: /*OR cpu_reg,8*/
                        fetchea();
                        temp=geteab();
                        temp|=getr8(cpu_reg);
                        setznp8(temp);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        setr8(cpu_reg,temp);
                        cycles-=((cpu_mod==3)?3:13);
                        break;
                        case 0x0B: /*OR reg,16*/
                        fetchea();
                        tempw=geteaw();
                        tempw|=cpu_state.regs[cpu_reg].w;
                        setznp16(tempw);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        cpu_state.regs[cpu_reg].w=tempw;
                        cycles-=((cpu_mod==3)?3:13);
                        break;
                        case 0x0C: /*OR AL,#8*/
                        AL|=FETCH();
                        setznp8(AL);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        cycles-=4;
                        break;
                        case 0x0D: /*OR AX,#16*/
                        AX|=getword();
                        setznp16(AX);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        cycles-=4;
                        break;

                        case 0x0E: /*PUSH CS*/
                        if (cpu_state.ssegs) ss=oldss;
                        writememw(ss,((SP-2)&0xFFFF),CS);
                        SP-=2;
			cpu_state.last_ea = SP;
                        cycles-=14;
                        break;
                        case 0x0F: /*POP CS - 8088/8086 only*/
                        if (cpu_state.ssegs) ss=oldss;
                        tempw=readmemw(ss,SP);
                        loadseg(tempw,&_cs);
                        SP+=2;
			cpu_state.last_ea = SP;
                        cycles-=12;
                        break;

                        case 0x10: /*ADC 8,reg*/
                        fetchea();
                        temp=geteab();
                        temp2=getr8(cpu_reg);
                        setadc8(temp,temp2);
                        temp+=temp2+tempc;
                        seteab(temp);
                        cycles-=((cpu_mod==3)?3:24);
                        break;
                        case 0x11: /*ADC 16,reg*/
                        fetchea();
                        tempw=geteaw();
                        tempw2=cpu_state.regs[cpu_reg].w;
                        setadc16(tempw,tempw2);
                        tempw+=tempw2+tempc;
                        seteaw(tempw);
                        cycles-=((cpu_mod==3)?3:24);
                        break;
                        case 0x12: /*ADC cpu_reg,8*/
                        fetchea();
                        temp=geteab();
                        setadc8(getr8(cpu_reg),temp);
                        setr8(cpu_reg,getr8(cpu_reg)+temp+tempc);
                        cycles-=((cpu_mod==3)?3:13);
                        break;
                        case 0x13: /*ADC cpu_reg,16*/
                        fetchea();
                        tempw=geteaw();
                        setadc16(cpu_state.regs[cpu_reg].w,tempw);
                        cpu_state.regs[cpu_reg].w+=tempw+tempc;
                        cycles-=((cpu_mod==3)?3:13);
                        break;
                        case 0x14: /*ADC AL,#8*/
                        tempw=FETCH();
                        setadc8(AL,tempw);
                        AL+=tempw+tempc;
                        cycles-=4;
                        break;
                        case 0x15: /*ADC AX,#16*/
                        tempw=getword();
                        setadc16(AX,tempw);
                        AX+=tempw+tempc;
                        cycles-=4;
                        break;

                        case 0x16: /*PUSH SS*/
                        if (cpu_state.ssegs) ss=oldss;
                        writememw(ss,((SP-2)&0xFFFF),SS);
                        SP-=2;
                        cycles-=14;
			cpu_state.last_ea = SP;
                        break;
                        case 0x17: /*POP SS*/
                        if (cpu_state.ssegs) ss=oldss;
                        tempw=readmemw(ss,SP);
                        loadseg(tempw,&_ss);
                        SP+=2;
			cpu_state.last_ea = SP;
                        noint=1;
                        cycles-=12;
                        break;

                        case 0x18: /*SBB 8,reg*/
                        fetchea();
                        temp=geteab();
                        temp2=getr8(cpu_reg);
                        setsbc8(temp,temp2);
                        temp-=(temp2+tempc);
                        seteab(temp);
                        cycles-=((cpu_mod==3)?3:24);
                        break;
                        case 0x19: /*SBB 16,reg*/
                        fetchea();
                        tempw=geteaw();
                        tempw2=cpu_state.regs[cpu_reg].w;
                        setsbc16(tempw,tempw2);
                        tempw-=(tempw2+tempc);
                        seteaw(tempw);
                        cycles-=((cpu_mod==3)?3:24);
                        break;
                        case 0x1A: /*SBB cpu_reg,8*/
                        fetchea();
                        temp=geteab();
                        setsbc8(getr8(cpu_reg),temp);
                        setr8(cpu_reg,getr8(cpu_reg)-(temp+tempc));
                        cycles-=((cpu_mod==3)?3:13);
                        break;
                        case 0x1B: /*SBB cpu_reg,16*/
                        fetchea();
                        tempw=geteaw();
                        tempw2=cpu_state.regs[cpu_reg].w;
                        setsbc16(tempw2,tempw);
                        tempw2-=(tempw+tempc);
                        cpu_state.regs[cpu_reg].w=tempw2;
                        cycles-=((cpu_mod==3)?3:13);
                        break;
                        case 0x1C: /*SBB AL,#8*/
                        temp=FETCH();
                        setsbc8(AL,temp);
                        AL-=(temp+tempc);
                        cycles-=4;
                        break;
                        case 0x1D: /*SBB AX,#16*/
                        tempw=getword();
                        setsbc16(AX,tempw);
                        AX-=(tempw+tempc);
                        cycles-=4;
                        break;

                        case 0x1E: /*PUSH DS*/
                        if (cpu_state.ssegs) ss=oldss;
                        writememw(ss,((SP-2)&0xFFFF),DS);
                        SP-=2;
			cpu_state.last_ea = SP;
                        cycles-=14;
                        break;
                        case 0x1F: /*POP DS*/
                        if (cpu_state.ssegs) ss=oldss;
                        tempw=readmemw(ss,SP);
                        loadseg(tempw,&_ds);
                        if (cpu_state.ssegs) oldds=ds;
                        SP+=2;
			cpu_state.last_ea = SP;
                        cycles-=12;
                        break;

                        case 0x20: /*AND 8,reg*/
                        fetchea();
                        temp=geteab();
                        temp&=getr8(cpu_reg);
                        setznp8(temp);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        seteab(temp);
                        cycles-=((cpu_mod==3)?3:24);
                        break;
                        case 0x21: /*AND 16,reg*/
                        fetchea();
                        tempw=geteaw();
                        tempw&=cpu_state.regs[cpu_reg].w;
                        setznp16(tempw);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        seteaw(tempw);
                        cycles-=((cpu_mod==3)?3:24);
                        break;
                        case 0x22: /*AND cpu_reg,8*/
                        fetchea();
                        temp=geteab();
                        temp&=getr8(cpu_reg);
                        setznp8(temp);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        setr8(cpu_reg,temp);
                        cycles-=((cpu_mod==3)?3:13);
                        break;
                        case 0x23: /*AND cpu_reg,16*/
                        fetchea();
                        tempw=geteaw();
                        tempw&=cpu_state.regs[cpu_reg].w;
                        setznp16(tempw);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        cpu_state.regs[cpu_reg].w=tempw;
                        cycles-=((cpu_mod==3)?3:13);
                        break;
                        case 0x24: /*AND AL,#8*/
                        AL&=FETCH();
                        setznp8(AL);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        cycles-=4;
                        break;
                        case 0x25: /*AND AX,#16*/
                        AX&=getword();
                        setznp16(AX);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        cycles-=4;
                        break;

                        case 0x26: /*ES:*/
                        oldss=ss;
                        oldds=ds;
                        ds=ss=es;
                        cpu_state.ssegs=2;
                        cycles-=4;
                        goto opcodestart;

                        case 0x27: /*DAA*/
                        if ((flags&A_FLAG) || ((AL&0xF)>9))
                        {
                                tempi=((uint16_t)AL)+6;
                                AL+=6;
                                flags|=A_FLAG;
                                if (tempi&0x100) flags|=C_FLAG;
                        }
                        if ((flags&C_FLAG) || (AL>0x9F))
                        {
                                AL+=0x60;
                                flags|=C_FLAG;
                        }
                        setznp8(AL);
                        cycles-=4;
                        break;

                        case 0x28: /*SUB 8,reg*/
                        fetchea();
                        temp=geteab();
                        setsub8(temp,getr8(cpu_reg));
                        temp-=getr8(cpu_reg);
                        seteab(temp);
                        cycles-=((cpu_mod==3)?3:24);
                        break;
                        case 0x29: /*SUB 16,reg*/
                        fetchea();
                        tempw=geteaw();
                        setsub16(tempw,cpu_state.regs[cpu_reg].w);
                        tempw-=cpu_state.regs[cpu_reg].w;
                        seteaw(tempw);
                        cycles-=((cpu_mod==3)?3:24);
                        break;
                        case 0x2A: /*SUB cpu_reg,8*/
                        fetchea();
                        temp=geteab();
                        setsub8(getr8(cpu_reg),temp);
                        setr8(cpu_reg,getr8(cpu_reg)-temp);
                        cycles-=((cpu_mod==3)?3:13);
                        break;
                        case 0x2B: /*SUB cpu_reg,16*/
                        fetchea();
                        tempw=geteaw();
                        setsub16(cpu_state.regs[cpu_reg].w,tempw);
                        cpu_state.regs[cpu_reg].w-=tempw;
                        cycles-=((cpu_mod==3)?3:13);
                        break;
                        case 0x2C: /*SUB AL,#8*/
                        temp=FETCH();
                        setsub8(AL,temp);
                        AL-=temp;
                        cycles-=4;
                        break;
                        case 0x2D: /*SUB AX,#16*/
                        tempw=getword();
                        setsub16(AX,tempw);
                        AX-=tempw;
                        cycles-=4;
                        break;
                        case 0x2E: /*CS:*/
                        oldss=ss;
                        oldds=ds;
                        ds=ss=cs;
                        cpu_state.ssegs=2;
                        cycles-=4;
                        goto opcodestart;
                        case 0x2F: /*DAS*/
                        if ((flags&A_FLAG)||((AL&0xF)>9))
                        {
                                tempi=((uint16_t)AL)-6;
                                AL-=6;
                                flags|=A_FLAG;
                                if (tempi&0x100) flags|=C_FLAG;
                        }
                        if ((flags&C_FLAG)||(AL>0x9F))
                        {
                                AL-=0x60;
                                flags|=C_FLAG;
                        }
                        setznp8(AL);
                        cycles-=4;
                        break;
                        case 0x30: /*XOR 8,reg*/
                        fetchea();
                        temp=geteab();
                        temp^=getr8(cpu_reg);
                        setznp8(temp);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        seteab(temp);
                        cycles-=((cpu_mod==3)?3:24);
                        break;
                        case 0x31: /*XOR 16,reg*/
                        fetchea();
                        tempw=geteaw();
                        tempw^=cpu_state.regs[cpu_reg].w;
                        setznp16(tempw);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        seteaw(tempw);
                        cycles-=((cpu_mod==3)?3:24);
                        break;
                        case 0x32: /*XOR cpu_reg,8*/
                        fetchea();
                        temp=geteab();
                        temp^=getr8(cpu_reg);
                        setznp8(temp);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        setr8(cpu_reg,temp);
                        cycles-=((cpu_mod==3)?3:13);
                        break;
                        case 0x33: /*XOR cpu_reg,16*/
                        fetchea();
                        tempw=geteaw();
                        tempw^=cpu_state.regs[cpu_reg].w;
                        setznp16(tempw);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        cpu_state.regs[cpu_reg].w=tempw;
                        cycles-=((cpu_mod==3)?3:13);
                        break;
                        case 0x34: /*XOR AL,#8*/
                        AL^=FETCH();
                        setznp8(AL);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        cycles-=4;
                        break;
                        case 0x35: /*XOR AX,#16*/
                        AX^=getword();
                        setznp16(AX);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        cycles-=4;
                        break;

                        case 0x36: /*SS:*/
                        oldss=ss;
                        oldds=ds;
                        ds=ss=ss;
                        cpu_state.ssegs=2;
                        cycles-=4;
                        goto opcodestart;

                        case 0x37: /*AAA*/
                        if ((flags&A_FLAG)||((AL&0xF)>9))
                        {
                                AL+=6;
                                AH++;
                                flags|=(A_FLAG|C_FLAG);
                        }
                        else
                           flags&=~(A_FLAG|C_FLAG);
                        AL&=0xF;
                        cycles-=8;
                        break;

                        case 0x38: /*CMP 8,reg*/
                        fetchea();
                        temp=geteab();
                        setsub8(temp,getr8(cpu_reg));
                        cycles-=((cpu_mod==3)?3:13);
                        break;
                        case 0x39: /*CMP 16,reg*/
                        fetchea();
                        tempw=geteaw();
                        setsub16(tempw,cpu_state.regs[cpu_reg].w);
                        cycles-=((cpu_mod==3)?3:13);
                        break;
                        case 0x3A: /*CMP cpu_reg,8*/
                        fetchea();
                        temp=geteab();
                        setsub8(getr8(cpu_reg),temp);
                        cycles-=((cpu_mod==3)?3:13);
                        break;
                        case 0x3B: /*CMP cpu_reg,16*/
                        fetchea();
                        tempw=geteaw();
                        setsub16(cpu_state.regs[cpu_reg].w,tempw);
                        cycles-=((cpu_mod==3)?3:13);
                        break;
                        case 0x3C: /*CMP AL,#8*/
                        temp=FETCH();
                        setsub8(AL,temp);
                        cycles-=4;
                        break;
                        case 0x3D: /*CMP AX,#16*/
                        tempw=getword();
                        setsub16(AX,tempw);
                        cycles-=4;
                        break;

                        case 0x3E: /*DS:*/
                        oldss=ss;
                        oldds=ds;
                        ds=ss=ds;
                        cpu_state.ssegs=2;
                        cycles-=4;
                        goto opcodestart;

                        case 0x3F: /*AAS*/
                        if ((flags&A_FLAG)||((AL&0xF)>9))
                        {
                                AL-=6;
                                AH--;
                                flags|=(A_FLAG|C_FLAG);
                        }
                        else
                           flags&=~(A_FLAG|C_FLAG);
                        AL&=0xF;
                        cycles-=8;
                        break;

                        case 0x40: case 0x41: case 0x42: case 0x43: /*INC r16*/
                        case 0x44: case 0x45: case 0x46: case 0x47:
                        setadd16nc(cpu_state.regs[opcode&7].w,1);
                        cpu_state.regs[opcode&7].w++;
                        cycles-=3;
                        break;
                        case 0x48: case 0x49: case 0x4A: case 0x4B: /*DEC r16*/
                        case 0x4C: case 0x4D: case 0x4E: case 0x4F:
                        setsub16nc(cpu_state.regs[opcode&7].w,1);
                        cpu_state.regs[opcode&7].w--;
                        cycles-=3;
                        break;

                        case 0x50: case 0x51: case 0x52: case 0x53: /*PUSH r16*/
                        case 0x54: case 0x55: case 0x56: case 0x57:
                        if (cpu_state.ssegs) ss=oldss;
                        SP-=2;
			cpu_state.last_ea = SP;
                        writememw(ss,SP,cpu_state.regs[opcode&7].w);
                        cycles-=15;
                        break;
                        case 0x58: case 0x59: case 0x5A: case 0x5B: /*POP r16*/
                        case 0x5C: case 0x5D: case 0x5E: case 0x5F:
                        if (cpu_state.ssegs) ss=oldss;
                        SP+=2;
			cpu_state.last_ea = SP;
                        cpu_state.regs[opcode&7].w=readmemw(ss,(SP-2)&0xFFFF);
                        cycles-=12;
                        break;


			case 0x60: /*JO alias*/
                        case 0x70: /*JO*/
                        offset=(int8_t)FETCH();
                        if (flags&V_FLAG) { cpu_state.pc+=offset; cycles-=12; FETCHCLEAR(); }
                        cycles-=4;
                        break;
			case 0x61: /*JNO alias*/
                        case 0x71: /*JNO*/
                        offset=(int8_t)FETCH();
                        if (!(flags&V_FLAG)) { cpu_state.pc+=offset; cycles-=12; FETCHCLEAR(); }
                        cycles-=4;
                        break;
			case 0x62: /*JB alias*/
                        case 0x72: /*JB*/
                        offset=(int8_t)FETCH();
                        if (flags&C_FLAG) { cpu_state.pc+=offset; cycles-=12; FETCHCLEAR(); }
                        cycles-=4;
                        break;
			case 0x63: /*JNB alias*/
                        case 0x73: /*JNB*/
                        offset=(int8_t)FETCH();
                        if (!(flags&C_FLAG)) { cpu_state.pc+=offset; cycles-=12; FETCHCLEAR(); }
                        cycles-=4;
                        break;
			case 0x64: /*JE alias*/
                        case 0x74: /*JE*/
                        offset=(int8_t)FETCH();
                        if (flags&Z_FLAG) { cpu_state.pc+=offset; cycles-=12; FETCHCLEAR(); }
                        cycles-=4;
                        break;
			case 0x65: /*JNE alias*/
                        case 0x75: /*JNE*/
                        offset=(int8_t)FETCH();
                        cycles-=4;
                        if (!(flags&Z_FLAG)) { cpu_state.pc+=offset; cycles-=12; FETCHCLEAR(); }
                        break;
			case 0x66: /*JBE alias*/
                        case 0x76: /*JBE*/
                        offset=(int8_t)FETCH();
                        if (flags&(C_FLAG|Z_FLAG)) { cpu_state.pc+=offset; cycles-=12; FETCHCLEAR(); }
                        cycles-=4;
                        break;
			case 0x67: /*JNBE alias*/
                        case 0x77: /*JNBE*/
                        offset=(int8_t)FETCH();
                        if (!(flags&(C_FLAG|Z_FLAG))) { cpu_state.pc+=offset; cycles-=12; FETCHCLEAR(); }
                        cycles-=4;
                        break;
			case 0x68: /*JS alias*/
                        case 0x78: /*JS*/
                        offset=(int8_t)FETCH();
                        if (flags&N_FLAG)  { cpu_state.pc+=offset; cycles-=12; FETCHCLEAR(); }
                        cycles-=4;
                        break;
			case 0x69: /*JNS alias*/
                        case 0x79: /*JNS*/
                        offset=(int8_t)FETCH();
                        if (!(flags&N_FLAG))  { cpu_state.pc+=offset; cycles-=12; FETCHCLEAR(); }
                        cycles-=4;
                        break;
			case 0x6A: /*JP alias*/
                        case 0x7A: /*JP*/
                        offset=(int8_t)FETCH();
                        if (flags&P_FLAG)  { cpu_state.pc+=offset; cycles-=12; FETCHCLEAR(); }
                        cycles-=4;
                        break;
			case 0x6B: /*JNP alias*/
                        case 0x7B: /*JNP*/
                        offset=(int8_t)FETCH();
                        if (!(flags&P_FLAG))  { cpu_state.pc+=offset; cycles-=12; FETCHCLEAR(); }
                        cycles-=4;
                        break;
			case 0x6C: /*JL alias*/
                        case 0x7C: /*JL*/
                        offset=(int8_t)FETCH();
                        temp=(flags&N_FLAG)?1:0;
                        temp2=(flags&V_FLAG)?1:0;
                        if (temp!=temp2)  { cpu_state.pc+=offset; cycles-=12; FETCHCLEAR(); }
                        cycles-=4;
                        break;
			case 0x6D: /*JNL alias*/
                        case 0x7D: /*JNL*/
                        offset=(int8_t)FETCH();
                        temp=(flags&N_FLAG)?1:0;
                        temp2=(flags&V_FLAG)?1:0;
                        if (temp==temp2)  { cpu_state.pc+=offset; cycles-=12; FETCHCLEAR(); }
                        cycles-=4;
                        break;
			case 0x6E: /*JLE alias*/
                        case 0x7E: /*JLE*/
                        offset=(int8_t)FETCH();
                        temp=(flags&N_FLAG)?1:0;
                        temp2=(flags&V_FLAG)?1:0;
                        if ((flags&Z_FLAG) || (temp!=temp2))  { cpu_state.pc+=offset; cycles-=12; FETCHCLEAR(); }
                        cycles-=4;
                        break;
			case 0x6F: /*JNLE alias*/
                        case 0x7F: /*JNLE*/
                        offset=(int8_t)FETCH();
                        temp=(flags&N_FLAG)?1:0;
                        temp2=(flags&V_FLAG)?1:0;
                        if (!((flags&Z_FLAG) || (temp!=temp2)))  { cpu_state.pc+=offset; cycles-=12; FETCHCLEAR(); }
                        cycles-=4;
                        break;

                        case 0x80: case 0x82:
                        fetchea();
                        temp=geteab();
                        temp2=FETCH();
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*ADD b,#8*/
                                setadd8(temp,temp2);
                                seteab(temp+temp2);
                                cycles-=((cpu_mod==3)?4:23);
                                break;
                                case 0x08: /*OR b,#8*/
                                temp|=temp2;
                                setznp8(temp);
                                flags&=~(C_FLAG|V_FLAG|A_FLAG);
                                seteab(temp);
                                cycles-=((cpu_mod==3)?4:23);
                                break;
                                case 0x10: /*ADC b,#8*/
                                setadc8(temp,temp2);
                                seteab(temp+temp2+tempc);
                                cycles-=((cpu_mod==3)?4:23);
                                break;
                                case 0x18: /*SBB b,#8*/
                                setsbc8(temp,temp2);
                                seteab(temp-(temp2+tempc));
                                cycles-=((cpu_mod==3)?4:23);
                                break;
                                case 0x20: /*AND b,#8*/
                                temp&=temp2;
                                setznp8(temp);
                                flags&=~(C_FLAG|V_FLAG|A_FLAG);
                                seteab(temp);
                                cycles-=((cpu_mod==3)?4:23);
                                break;
                                case 0x28: /*SUB b,#8*/
                                setsub8(temp,temp2);
                                seteab(temp-temp2);
                                cycles-=((cpu_mod==3)?4:23);
                                break;
                                case 0x30: /*XOR b,#8*/
                                temp^=temp2;
                                setznp8(temp);
                                flags&=~(C_FLAG|V_FLAG|A_FLAG);
                                seteab(temp);
                                cycles-=((cpu_mod==3)?4:23);
                                break;
                                case 0x38: /*CMP b,#8*/
                                setsub8(temp,temp2);
                                cycles-=((cpu_mod==3)?4:14);
                                break;
                        }
                        break;

                        case 0x81:
                        fetchea();
                        tempw=geteaw();
                        tempw2=getword();
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*ADD w,#16*/
                                setadd16(tempw,tempw2);
                                tempw+=tempw2;
                                seteaw(tempw);
                                cycles-=((cpu_mod==3)?4:23);
                                break;
                                case 0x08: /*OR w,#16*/
                                tempw|=tempw2;
                                setznp16(tempw);
                                flags&=~(C_FLAG|V_FLAG|A_FLAG);
                                seteaw(tempw);
                                cycles-=((cpu_mod==3)?4:23);
                                break;
                                case 0x10: /*ADC w,#16*/
                                setadc16(tempw,tempw2);
                                tempw+=tempw2+tempc;
                                seteaw(tempw);
                                cycles-=((cpu_mod==3)?4:23);
                                break;
                                case 0x20: /*AND w,#16*/
                                tempw&=tempw2;
                                setznp16(tempw);
                                flags&=~(C_FLAG|V_FLAG|A_FLAG);
                                seteaw(tempw);
                                cycles-=((cpu_mod==3)?4:23);
                                break;
                                case 0x18: /*SBB w,#16*/
                                setsbc16(tempw,tempw2);
                                seteaw(tempw-(tempw2+tempc));
                                cycles-=((cpu_mod==3)?4:23);
                                break;
                                case 0x28: /*SUB w,#16*/
                                setsub16(tempw,tempw2);
                                tempw-=tempw2;
                                seteaw(tempw);
                                cycles-=((cpu_mod==3)?4:23);
                                break;
                                case 0x30: /*XOR w,#16*/
                                tempw^=tempw2;
                                setznp16(tempw);
                                flags&=~(C_FLAG|V_FLAG|A_FLAG);
                                seteaw(tempw);
                                cycles-=((cpu_mod==3)?4:23);
                                break;
                                case 0x38: /*CMP w,#16*/
                                setsub16(tempw,tempw2);
                                cycles-=((cpu_mod==3)?4:14);
                                break;
                        }
                        break;

                        case 0x83:
                        fetchea();
                        tempw=geteaw();
                        tempw2=FETCH();
                        if (tempw2&0x80) tempw2|=0xFF00;
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*ADD w,#8*/
                                setadd16(tempw,tempw2);
                                tempw+=tempw2;
                                seteaw(tempw);
                                cycles-=((cpu_mod==3)?4:23);
                                break;
                                case 0x08: /*OR w,#8*/
                                tempw|=tempw2;
                                setznp16(tempw);
                                seteaw(tempw);
                                flags&=~(C_FLAG|A_FLAG|V_FLAG);
                                cycles-=((cpu_mod==3)?4:23);
                                break;
                                case 0x10: /*ADC w,#8*/
                                setadc16(tempw,tempw2);
                                tempw+=tempw2+tempc;
                                seteaw(tempw);
                                cycles-=((cpu_mod==3)?4:23);
                                break;
                                case 0x18: /*SBB w,#8*/
                                setsbc16(tempw,tempw2);
                                tempw-=(tempw2+tempc);
                                seteaw(tempw);
                                cycles-=((cpu_mod==3)?4:23);
                                break;
                                case 0x20: /*AND w,#8*/
                                tempw&=tempw2;
                                setznp16(tempw);
                                seteaw(tempw);
                                cycles-=((cpu_mod==3)?4:23);
                                flags&=~(C_FLAG|A_FLAG|V_FLAG);
                                break;
                                case 0x28: /*SUB w,#8*/
                                setsub16(tempw,tempw2);
                                tempw-=tempw2;
                                seteaw(tempw);
                                cycles-=((cpu_mod==3)?4:23);
                                break;
                                case 0x30: /*XOR w,#8*/
                                tempw^=tempw2;
                                setznp16(tempw);
                                seteaw(tempw);
                                cycles-=((cpu_mod==3)?4:23);
                                flags&=~(C_FLAG|A_FLAG|V_FLAG);
                                break;
                                case 0x38: /*CMP w,#8*/
                                setsub16(tempw,tempw2);
                                cycles-=((cpu_mod==3)?4:14);
                                break;
                        }
                        break;

                        case 0x84: /*TEST b,reg*/
                        fetchea();
                        temp=geteab();
                        temp2=getr8(cpu_reg);
                        setznp8(temp&temp2);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        cycles-=((cpu_mod==3)?3:13);
                        break;
                        case 0x85: /*TEST w,reg*/
                        fetchea();
                        tempw=geteaw();
                        tempw2=cpu_state.regs[cpu_reg].w;
                        setznp16(tempw&tempw2);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        cycles-=((cpu_mod==3)?3:13);
                        break;
                        case 0x86: /*XCHG b,reg*/
                        fetchea();
                        temp=geteab();
                        seteab(getr8(cpu_reg));
                        setr8(cpu_reg,temp);
                        cycles-=((cpu_mod==3)?4:25);
                        break;
                        case 0x87: /*XCHG w,reg*/
                        fetchea();
                        tempw=geteaw();
                        seteaw(cpu_state.regs[cpu_reg].w);
                        cpu_state.regs[cpu_reg].w=tempw;
                        cycles-=((cpu_mod==3)?4:25);
                        break;

                        case 0x88: /*MOV b,reg*/
                        fetchea();
                        seteab(getr8(cpu_reg));
                        cycles-=((cpu_mod==3)?2:13);
                        break;
                        case 0x89: /*MOV w,reg*/
                        fetchea();
                        seteaw(cpu_state.regs[cpu_reg].w);
                        cycles-=((cpu_mod==3)?2:13);
                        break;
                        case 0x8A: /*MOV cpu_reg,b*/
                        fetchea();
                        temp=geteab();
                        setr8(cpu_reg,temp);
                        cycles-=((cpu_mod==3)?2:12);
                        break;
                        case 0x8B: /*MOV cpu_reg,w*/
                        fetchea();
                        tempw=geteaw();
                        cpu_state.regs[cpu_reg].w=tempw;
                        cycles-=((cpu_mod==3)?2:12);
                        break;

                        case 0x8C: /*MOV w,sreg*/
                        fetchea();
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*ES*/
                                seteaw(ES);
                                break;
                                case 0x08: /*CS*/
                                seteaw(CS);
                                break;
                                case 0x18: /*DS*/
                                if (cpu_state.ssegs) ds=oldds;
                                seteaw(DS);
                                break;
                                case 0x10: /*SS*/
                                if (cpu_state.ssegs) ss=oldss;
                                seteaw(SS);
                                break;
                        }
                        cycles-=((cpu_mod==3)?2:13);
                        break;

                        case 0x8D: /*LEA*/
                        fetchea();
                        cpu_state.regs[cpu_reg].w=(cpu_mod == 3)?cpu_state.last_ea:cpu_state.eaaddr;
                        cycles-=2;
                        break;

                        case 0x8E: /*MOV sreg,w*/
                        fetchea();
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*ES*/
                                tempw=geteaw();
                                loadseg(tempw,&_es);
                                break;
                                case 0x08: /*CS - 8088/8086 only*/
                                tempw=geteaw();
                                loadseg(tempw,&_cs);
                                break;
                                case 0x18: /*DS*/
                                tempw=geteaw();
                                loadseg(tempw,&_ds);
                                if (cpu_state.ssegs) oldds=ds;
                                break;
                                case 0x10: /*SS*/
                                tempw=geteaw();
                                loadseg(tempw,&_ss);
                                if (cpu_state.ssegs) oldss=ss;
                                break;
                        }
                        cycles-=((cpu_mod==3)?2:12);
                                skipnextprint=1;
				noint=1;
                        break;

                        case 0x8F: /*POPW*/
                        fetchea();
                        if (cpu_state.ssegs) ss=oldss;
                        tempw=readmemw(ss,SP);
                        SP+=2;
			cpu_state.last_ea = SP;
                        seteaw(tempw);
                        cycles-=25;
                        break;

                        case 0x90: /*NOP*/
                        cycles-=3;
                        break;

                        case 0x91: case 0x92: case 0x93: /*XCHG AX*/
                        case 0x94: case 0x95: case 0x96: case 0x97:
                        tempw=AX;
                        AX=cpu_state.regs[opcode&7].w;
                        cpu_state.regs[opcode&7].w=tempw;
                        cycles-=3;
                        break;

                        case 0x98: /*CBW*/
                        AH=(AL&0x80)?0xFF:0;
                        cycles-=2;
                        break;
                        case 0x99: /*CWD*/
                        DX=(AX&0x8000)?0xFFFF:0;
                        cycles-=5;
                        break;
                        case 0x9A: /*CALL FAR*/
                        tempw=getword();
                        tempw2=getword();
                        tempw3=CS;
                        tempw4=cpu_state.pc;
                        if (cpu_state.ssegs) ss=oldss;
                        cpu_state.pc=tempw;
                        loadcs(tempw2);
                        writememw(ss,(SP-2)&0xFFFF,tempw3);
                        writememw(ss,(SP-4)&0xFFFF,tempw4);
                        SP-=4;
			cpu_state.last_ea = SP;
                        cycles-=36;
                        FETCHCLEAR();
                        break;
                        case 0x9B: /*WAIT*/
                        cycles-=4;
                        break;
                        case 0x9C: /*PUSHF*/
                        if (cpu_state.ssegs) ss=oldss;
                        writememw(ss,((SP-2)&0xFFFF),flags|0xF000);
                        SP-=2;
			cpu_state.last_ea = SP;
                        cycles-=14;
                        break;
                        case 0x9D: /*POPF*/
                        if (cpu_state.ssegs) ss=oldss;
                        flags=readmemw(ss,SP)&0xFFF;
                        SP+=2;
			cpu_state.last_ea = SP;
                        cycles-=12;
                        break;
                        case 0x9E: /*SAHF*/
                        flags=(flags&0xFF00)|AH;
                        cycles-=4;
                        break;
                        case 0x9F: /*LAHF*/
                        AH=flags&0xFF;
                        cycles-=4;
                        break;

                        case 0xA0: /*MOV AL,(w)*/
                        addr=getword();
                        AL=readmemb(ds+addr);
                        cycles-=14;
                        break;
                        case 0xA1: /*MOV AX,(w)*/
                        addr=getword();
                        AX=readmemw(ds,addr);
                        cycles-=14;
                        break;
                        case 0xA2: /*MOV (w),AL*/
                        addr=getword();
                        writememb(ds+addr,AL);
                        cycles-=14;
                        break;
                        case 0xA3: /*MOV (w),AX*/
                        addr=getword();
                        writememw(ds,addr,AX);
                        cycles-=14;
                        break;

                        case 0xA4: /*MOVSB*/
                        temp=readmemb(ds+SI);
                        writememb(es+DI,temp);
                        if (flags&D_FLAG) { DI--; SI--; }
                        else              { DI++; SI++; }
                        cycles-=18;
                        break;
                        case 0xA5: /*MOVSW*/
                        tempw=readmemw(ds,SI);
                        writememw(es,DI,tempw);
                        if (flags&D_FLAG) { DI-=2; SI-=2; }
                        else              { DI+=2; SI+=2; }
                        cycles-=18;
                        break;
                        case 0xA6: /*CMPSB*/
                        temp =readmemb(ds+SI);
                        temp2=readmemb(es+DI);
                        setsub8(temp,temp2);
                        if (flags&D_FLAG) { DI--; SI--; }
                        else              { DI++; SI++; }
                        cycles-=30;
                        break;
                        case 0xA7: /*CMPSW*/
                        tempw =readmemw(ds,SI);
                        tempw2=readmemw(es,DI);
                        setsub16(tempw,tempw2);
                        if (flags&D_FLAG) { DI-=2; SI-=2; }
                        else              { DI+=2; SI+=2; }
                        cycles-=30;
                        break;
                        case 0xA8: /*TEST AL,#8*/
                        temp=FETCH();
                        setznp8(AL&temp);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        cycles-=5;
                        break;
                        case 0xA9: /*TEST AX,#16*/
                        tempw=getword();
                        setznp16(AX&tempw);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        cycles-=5;
                        break;
                        case 0xAA: /*STOSB*/
                        writememb(es+DI,AL);
                        if (flags&D_FLAG) DI--;
                        else              DI++;
                        cycles-=11;
                        break;
                        case 0xAB: /*STOSW*/
                        writememw(es,DI,AX);
                        if (flags&D_FLAG) DI-=2;
                        else              DI+=2;
                        cycles-=11;
                        break;
                        case 0xAC: /*LODSB*/
                        AL=readmemb(ds+SI);
                        if (flags&D_FLAG) SI--;
                        else              SI++;
                        cycles-=16;
                        break;
                        case 0xAD: /*LODSW*/
                        AX=readmemw(ds,SI);
                        if (flags&D_FLAG) SI-=2;
                        else              SI+=2;
                        cycles-=16;
                        break;
                        case 0xAE: /*SCASB*/
                        temp=readmemb(es+DI);
                        setsub8(AL,temp);
                        if (flags&D_FLAG) DI--;
                        else              DI++;
                        cycles-=19;
                        break;
                        case 0xAF: /*SCASW*/
                        tempw=readmemw(es,DI);
                        setsub16(AX,tempw);
                        if (flags&D_FLAG) DI-=2;
                        else              DI+=2;
                        cycles-=19;
                        break;

                        case 0xB0: /*MOV AL,#8*/
                        AL=FETCH();
                        cycles-=4;
                        break;
                        case 0xB1: /*MOV CL,#8*/
                        CL=FETCH();
                        cycles-=4;
                        break;
                        case 0xB2: /*MOV DL,#8*/
                        DL=FETCH();
                        cycles-=4;
                        break;
                        case 0xB3: /*MOV BL,#8*/
                        BL=FETCH();
                        cycles-=4;
                        break;
                        case 0xB4: /*MOV AH,#8*/
                        AH=FETCH();
                        cycles-=4;
                        break;
                        case 0xB5: /*MOV CH,#8*/
                        CH=FETCH();
                        cycles-=4;
                        break;
                        case 0xB6: /*MOV DH,#8*/
                        DH=FETCH();
                        cycles-=4;
                        break;
                        case 0xB7: /*MOV BH,#8*/
                        BH=FETCH();
                        cycles-=4;
                        break;
                        case 0xB8: case 0xB9: case 0xBA: case 0xBB: /*MOV cpu_reg,#16*/
                        case 0xBC: case 0xBD: case 0xBE: case 0xBF:
                        cpu_state.regs[opcode&7].w=getword();
                        cycles-=4;
                        break;

			case 0xC0: /*RET alias*/
                        case 0xC2: /*RET*/
                        tempw=getword();
                        if (cpu_state.ssegs) ss=oldss;
                        cpu_state.pc=readmemw(ss,SP);
                        SP+=2+tempw;
                        cycles-=24;
                        FETCHCLEAR();
                        break;
			case 0xC1: /*RET alias*/
                        case 0xC3: /*RET*/
                        if (cpu_state.ssegs) ss=oldss;
                        cpu_state.pc=readmemw(ss,SP);
                        SP+=2;
                        cycles-=20;
                        FETCHCLEAR();
                        break;
                        case 0xC4: /*LES*/
                        fetchea();
                        cpu_state.regs[cpu_reg].w=readmemw(easeg,cpu_state.eaaddr);
                        tempw=readmemw(easeg,(cpu_state.eaaddr+2)&0xFFFF);
                        loadseg(tempw,&_es);
                        cycles-=24;
                        break;
                        case 0xC5: /*LDS*/
                        fetchea();
                        cpu_state.regs[cpu_reg].w=readmemw(easeg,cpu_state.eaaddr);
                        tempw=readmemw(easeg,(cpu_state.eaaddr+2)&0xFFFF);
                        loadseg(tempw,&_ds);
                        if (cpu_state.ssegs) oldds=ds;
                        cycles-=24;
                        break;
                        case 0xC6: /*MOV b,#8*/
                        fetchea();
                        temp=FETCH();
                        seteab(temp);
                        cycles-=((cpu_mod==3)?4:14);
                        break;
                        case 0xC7: /*MOV w,#16*/
                        fetchea();
                        tempw=getword();
                        seteaw(tempw);
                        cycles-=((cpu_mod==3)?4:14);
                        break;

			case 0xC8: /*RETF alias*/
                        case 0xCA: /*RETF*/
                        tempw=getword();
                        if (cpu_state.ssegs) ss=oldss;
                        cpu_state.pc=readmemw(ss,SP);
                        loadcs(readmemw(ss,SP+2));
                        SP+=4;
                        SP+=tempw;
                        cycles-=33;
                        FETCHCLEAR();
                        break;
			case 0xC9: /*RETF alias*/
                        case 0xCB: /*RETF*/
                        if (cpu_state.ssegs) ss=oldss;
                        cpu_state.pc=readmemw(ss,SP);
                        loadcs(readmemw(ss,SP+2));
                        SP+=4;
                        cycles-=34;
                        FETCHCLEAR();
                        break;
                        case 0xCC: /*INT 3*/
                        if (cpu_state.ssegs) ss=oldss;
                        writememw(ss,((SP-2)&0xFFFF),flags|0xF000);
                        writememw(ss,((SP-4)&0xFFFF),CS);
                        writememw(ss,((SP-6)&0xFFFF),cpu_state.pc);
                        SP-=6;
                        addr=3<<2;
                        flags&=~I_FLAG;
                        flags&=~T_FLAG;
                        cpu_state.pc=readmemw(0,addr);
                        loadcs(readmemw(0,addr+2));
                        FETCHCLEAR();
                        cycles-=72;
                        break;
                        case 0xCD: /*INT*/
                        lastpc=cpu_state.pc;
                        lastcs=CS;
                        temp=FETCH();

                        if (cpu_state.ssegs) ss=oldss;
                        writememw(ss,((SP-2)&0xFFFF),flags|0xF000);
                        writememw(ss,((SP-4)&0xFFFF),CS);
                        writememw(ss,((SP-6)&0xFFFF),cpu_state.pc);
                        flags&=~T_FLAG;
                        SP-=6;
                        addr=temp<<2;
                        cpu_state.pc=readmemw(0,addr);

                        loadcs(readmemw(0,addr+2));
                        FETCHCLEAR();

                        cycles-=71;
                        break;
                        case 0xCF: /*IRET*/
                        if (cpu_state.ssegs) ss=oldss;
                        tempw=CS;
                        tempw2=cpu_state.pc;
                        cpu_state.pc=readmemw(ss,SP);
                        loadcs(readmemw(ss,((SP+2)&0xFFFF)));
                        flags=readmemw(ss,((SP+4)&0xFFFF))&0xFFF;
                        SP+=6;
                        cycles-=44;
                        FETCHCLEAR();
                        nmi_enable = 1;
                        break;
                        case 0xD0:
                        fetchea();
                        temp=geteab();
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*ROL b,1*/
                                if (temp&0x80) flags|=C_FLAG;
                                else           flags&=~C_FLAG;
                                temp<<=1;
                                if (flags&C_FLAG) temp|=1;
                                seteab(temp);
                                if ((flags&C_FLAG)^(temp>>7)) flags|=V_FLAG;
                                else                          flags&=~V_FLAG;
                                cycles-=((cpu_mod==3)?2:23);
                                break;
                                case 0x08: /*ROR b,1*/
                                if (temp&1) flags|=C_FLAG;
                                else        flags&=~C_FLAG;
                                temp>>=1;
                                if (flags&C_FLAG) temp|=0x80;
                                seteab(temp);
                                if ((temp^(temp>>1))&0x40) flags|=V_FLAG;
                                else                       flags&=~V_FLAG;
                                cycles-=((cpu_mod==3)?2:23);
                                break;
                                case 0x10: /*RCL b,1*/
                                temp2=flags&C_FLAG;
                                if (temp&0x80) flags|=C_FLAG;
                                else           flags&=~C_FLAG;
                                temp<<=1;
                                if (temp2) temp|=1;
                                seteab(temp);
                                if ((flags&C_FLAG)^(temp>>7)) flags|=V_FLAG;
                                else                          flags&=~V_FLAG;
                                cycles-=((cpu_mod==3)?2:23);
                                break;
                                case 0x18: /*RCR b,1*/
                                temp2=flags&C_FLAG;
                                if (temp&1) flags|=C_FLAG;
                                else        flags&=~C_FLAG;
                                temp>>=1;
                                if (temp2) temp|=0x80;
                                seteab(temp);
                                if ((temp^(temp>>1))&0x40) flags|=V_FLAG;
                                else                       flags&=~V_FLAG;
                                cycles-=((cpu_mod==3)?2:23);
                                break;
                                case 0x20: case 0x30: /*SHL b,1*/
                                if (temp&0x80) flags|=C_FLAG;
                                else           flags&=~C_FLAG;
                                if ((temp^(temp<<1))&0x80) flags|=V_FLAG;
                                else                       flags&=~V_FLAG;
                                temp<<=1;
                                seteab(temp);
                                setznp8(temp);
                                cycles-=((cpu_mod==3)?2:23);
                                flags|=A_FLAG;
                                break;
                                case 0x28: /*SHR b,1*/
                                if (temp&1) flags|=C_FLAG;
                                else        flags&=~C_FLAG;
                                if (temp&0x80) flags|=V_FLAG;
                                else           flags&=~V_FLAG;
                                temp>>=1;
                                seteab(temp);
                                setznp8(temp);
                                cycles-=((cpu_mod==3)?2:23);
                                flags|=A_FLAG;
                                break;
                                case 0x38: /*SAR b,1*/
                                if (temp&1) flags|=C_FLAG;
                                else        flags&=~C_FLAG;
                                temp>>=1;
                                if (temp&0x40) temp|=0x80;
                                seteab(temp);
                                setznp8(temp);
                                cycles-=((cpu_mod==3)?2:23);
                                flags|=A_FLAG;
                                flags&=~V_FLAG;
                                break;
                        }
                        break;

                        case 0xD1:
                        fetchea();
                        tempw=geteaw();
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*ROL w,1*/
                                if (tempw&0x8000) flags|=C_FLAG;
                                else              flags&=~C_FLAG;
                                tempw<<=1;
                                if (flags&C_FLAG) tempw|=1;
                                seteaw(tempw);
                                if ((flags&C_FLAG)^(tempw>>15)) flags|=V_FLAG;
                                else                            flags&=~V_FLAG;
                                cycles-=((cpu_mod==3)?2:23);
                                break;
                                case 0x08: /*ROR w,1*/
                                if (tempw&1) flags|=C_FLAG;
                                else         flags&=~C_FLAG;
                                tempw>>=1;
                                if (flags&C_FLAG) tempw|=0x8000;
                                seteaw(tempw);
                                if ((tempw^(tempw>>1))&0x4000) flags|=V_FLAG;
                                else                           flags&=~V_FLAG;
                                cycles-=((cpu_mod==3)?2:23);
                                break;
                                case 0x10: /*RCL w,1*/
                                temp2=flags&C_FLAG;
                                if (tempw&0x8000) flags|=C_FLAG;
                                else              flags&=~C_FLAG;
                                tempw<<=1;
                                if (temp2) tempw|=1;
                                seteaw(tempw);
                                if ((flags&C_FLAG)^(tempw>>15)) flags|=V_FLAG;
                                else                            flags&=~V_FLAG;
                                cycles-=((cpu_mod==3)?2:23);
                                break;
                                case 0x18: /*RCR w,1*/
                                temp2=flags&C_FLAG;
                                if (tempw&1) flags|=C_FLAG;
                                else         flags&=~C_FLAG;
                                tempw>>=1;
                                if (temp2) tempw|=0x8000;
                                seteaw(tempw);
                                if ((tempw^(tempw>>1))&0x4000) flags|=V_FLAG;
                                else                           flags&=~V_FLAG;
                                cycles-=((cpu_mod==3)?2:23);
                                break;
                                case 0x20: case 0x30: /*SHL w,1*/
                                if (tempw&0x8000) flags|=C_FLAG;
                                else              flags&=~C_FLAG;
                                if ((tempw^(tempw<<1))&0x8000) flags|=V_FLAG;
                                else                           flags&=~V_FLAG;
                                tempw<<=1;
                                seteaw(tempw);
                                setznp16(tempw);
                                cycles-=((cpu_mod==3)?2:23);
                                flags|=A_FLAG;
                                break;
                                case 0x28: /*SHR w,1*/
                                if (tempw&1) flags|=C_FLAG;
                                else         flags&=~C_FLAG;
                                if (tempw&0x8000) flags|=V_FLAG;
                                else              flags&=~V_FLAG;
                                tempw>>=1;
                                seteaw(tempw);
                                setznp16(tempw);
                                cycles-=((cpu_mod==3)?2:23);
                                flags|=A_FLAG;
                                break;

                                case 0x38: /*SAR w,1*/
                                if (tempw&1) flags|=C_FLAG;
                                else         flags&=~C_FLAG;
                                tempw>>=1;
                                if (tempw&0x4000) tempw|=0x8000;
                                seteaw(tempw);
                                setznp16(tempw);
                                cycles-=((cpu_mod==3)?2:23);
                                flags|=A_FLAG;
                                flags&=~V_FLAG;
                                break;
                        }
                        break;

                        case 0xD2:
                        fetchea();
                        temp=geteab();
                        c=CL;
                        if (!c) break;
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*ROL b,CL*/
				temp2=(temp&0x80)?1:0;
				if (!c)
				{
        	                        cycles-=((cpu_mod==3)?8:28);
	                                break;
				}
                                while (c>0)
                                {
                                        temp2=(temp&0x80)?1:0;
                                        temp=(temp<<1)|temp2;
                                        c--;
                                        cycles-=4;
                                }
                                if (temp2) flags|=C_FLAG;
                                else       flags&=~C_FLAG;
                                seteab(temp);
                                if ((flags&C_FLAG)^(temp>>7)) flags|=V_FLAG;
                                else                          flags&=~V_FLAG;
                                cycles-=((cpu_mod==3)?8:28);
                                break;
                                case 0x08: /*ROR b,CL*/
				temp2=temp&1;
				if (!c)
				{
					cycles-=((cpu_mod==3)?8:28);
					break;
				}
                                while (c>0)
                                {
                                        temp2=temp&1;
                                        temp>>=1;
                                        if (temp2) temp|=0x80;
                                        c--;
                                        cycles-=4;
                                }
                                if (temp2) flags|=C_FLAG;
                                else       flags&=~C_FLAG;
                                seteab(temp);
                                if ((temp^(temp>>1))&0x40) flags|=V_FLAG;
                                else                       flags&=~V_FLAG;
                                cycles-=((cpu_mod==3)?8:28);
                                break;
                                case 0x10: /*RCL b,CL*/
                                while (c>0)
                                {
                                        templ=flags&C_FLAG;
                                        temp2=temp&0x80;
                                        temp<<=1;
                                        if (temp2) flags|=C_FLAG;
                                        else       flags&=~C_FLAG;
                                        if (templ) temp|=1;
                                        c--;
                                        cycles-=4;
                                }
                                seteab(temp);
                                if ((flags&C_FLAG)^(temp>>7)) flags|=V_FLAG;
                                else                          flags&=~V_FLAG;
                                cycles-=((cpu_mod==3)?8:28);
                                break;
                                case 0x18: /*RCR b,CL*/
                                while (c>0)
                                {
                                        templ=flags&C_FLAG;
                                        temp2=temp&1;
                                        temp>>=1;
                                        if (temp2) flags|=C_FLAG;
                                        else       flags&=~C_FLAG;
                                        if (templ) temp|=0x80;
                                        c--;
                                        cycles-=4;
                                }
                                seteab(temp);
                                if ((temp^(temp>>1))&0x40) flags|=V_FLAG;
                                else                       flags&=~V_FLAG;
                                cycles-=((cpu_mod==3)?8:28);
                                break;
                                case 0x20: case 0x30: /*SHL b,CL*/
                                if (c > 8)
                                {
                                        temp = 0;
                                        flags &= ~C_FLAG;
                                }
                                else
                                {
                                        if ((temp<<(c-1))&0x80) flags|=C_FLAG;
                                        else                    flags&=~C_FLAG;
                                        temp<<=c;
                                }
                                seteab(temp);
                                setznp8(temp);
                                cycles-=(c*4);
                                cycles-=((cpu_mod==3)?8:28);
                                flags|=A_FLAG;
                                break;
                                case 0x28: /*SHR b,CL*/
                                if (c > 8)
                                {
                                        temp = 0;
                                        flags &= ~C_FLAG;
                                }
                                else
                                {
                                        if ((temp>>(c-1))&1) flags|=C_FLAG;
                                        else                 flags&=~C_FLAG;
                                        temp>>=c;
                                }
                                seteab(temp);
                                setznp8(temp);
                                cycles-=(c*4);
                                cycles-=((cpu_mod==3)?8:28);
                                flags|=A_FLAG;
                                break;
                                case 0x38: /*SAR b,CL*/
                                if ((temp>>(c-1))&1) flags|=C_FLAG;
                                else                 flags&=~C_FLAG;
                                while (c>0)
                                {
                                        temp>>=1;
                                        if (temp&0x40) temp|=0x80;
                                        c--;
                                        cycles-=4;
                                }
                                seteab(temp);
                                setznp8(temp);
                                cycles-=((cpu_mod==3)?8:28);
                                flags|=A_FLAG;
                                break;
                        }
                        break;

                        case 0xD3:
                        fetchea();
                        tempw=geteaw();
                        c=CL;
                        if (!c) break;
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*ROL w,CL*/
                                while (c>0)
                                {
                                        temp=(tempw&0x8000)?1:0;
                                        tempw=(tempw<<1)|temp;
                                        c--;
                                        cycles-=4;
                                }
                                if (temp) flags|=C_FLAG;
                                else      flags&=~C_FLAG;
                                seteaw(tempw);
                                if ((flags&C_FLAG)^(tempw>>15)) flags|=V_FLAG;
                                else                            flags&=~V_FLAG;
                                cycles-=((cpu_mod==3)?8:28);
                                break;
                                case 0x08: /*ROR w,CL*/
				tempw2=(tempw&1)?0x8000:0;
				if (!c)
				{
        	                        cycles-=((cpu_mod==3)?8:28);
	                                break;
				}
                                while (c>0)
                                {
                                        tempw2=(tempw&1)?0x8000:0;
                                        tempw=(tempw>>1)|tempw2;
                                        c--;
                                        cycles-=4;
                                }
                                if (tempw2) flags|=C_FLAG;
                                else        flags&=~C_FLAG;
                                seteaw(tempw);
                                if ((tempw^(tempw>>1))&0x4000) flags|=V_FLAG;
                                else                           flags&=~V_FLAG;
                                cycles-=((cpu_mod==3)?8:28);
                                break;
                                case 0x10: /*RCL w,CL*/
                                while (c>0)
                                {
                                        templ=flags&C_FLAG;
                                        if (tempw&0x8000) flags|=C_FLAG;
                                        else              flags&=~C_FLAG;
                                        tempw=(tempw<<1)|templ;
                                        c--;
                                        cycles-=4;
                                }
                                if (temp) flags|=C_FLAG;
                                else      flags&=~C_FLAG;
                                seteaw(tempw);
                                if ((flags&C_FLAG)^(tempw>>15)) flags|=V_FLAG;
                                else                            flags&=~V_FLAG;
                                cycles-=((cpu_mod==3)?8:28);
                                break;
                                case 0x18: /*RCR w,CL*/
				templ=flags&C_FLAG;
				tempw2=(templ&1)?0x8000:0;
				if (!c)
				{
	                                cycles-=((cpu_mod==3)?8:28);
					break;
				}
                                while (c>0)
                                {
                                        templ=flags&C_FLAG;
                                        tempw2=(templ&1)?0x8000:0;
                                        if (tempw&1) flags|=C_FLAG;
                                        else         flags&=~C_FLAG;
                                        tempw=(tempw>>1)|tempw2;
                                        c--;
                                        cycles-=4;
                                }
                                if (tempw2) flags|=C_FLAG;
                                else        flags&=~C_FLAG;
                                seteaw(tempw);
                                if ((tempw^(tempw>>1))&0x4000) flags|=V_FLAG;
                                else                           flags&=~V_FLAG;
                                cycles-=((cpu_mod==3)?8:28);
                                break;

                                case 0x20: case 0x30: /*SHL w,CL*/
                                if (c>16)
                                {
                                        tempw=0;
                                        flags&=~C_FLAG;
                                }
                                else
                                {
                                        if ((tempw<<(c-1))&0x8000) flags|=C_FLAG;
                                        else                       flags&=~C_FLAG;
                                        tempw<<=c;
                                }
                                seteaw(tempw);
                                setznp16(tempw);
                                cycles-=(c*4);
                                cycles-=((cpu_mod==3)?8:28);
                                flags|=A_FLAG;
                                break;

                                case 0x28:            /*SHR w,CL*/
                                if (c > 16)
                                {
                                        tempw = 0;
                                        flags &= ~C_FLAG;
                                }
                                else
                                {
                                        if ((tempw>>(c-1))&1) flags|=C_FLAG;
                                        else                  flags&=~C_FLAG;
                                        tempw>>=c;
                                }
                                seteaw(tempw);
                                setznp16(tempw);
                                cycles-=(c*4);
                                cycles-=((cpu_mod==3)?8:28);
                                flags|=A_FLAG;
                                break;

                                case 0x38:            /*SAR w,CL*/
                                tempw2=tempw&0x8000;
                                if ((tempw>>(c-1))&1) flags|=C_FLAG;
                                else                  flags&=~C_FLAG;
                                while (c>0)
                                {
                                        tempw=(tempw>>1)|tempw2;
                                        c--;
                                        cycles-=4;
                                }
                                seteaw(tempw);
                                setznp16(tempw);
                                cycles-=((cpu_mod==3)?8:28);
                                flags|=A_FLAG;
                                break;
                        }
                        break;

                        case 0xD4: /*AAM*/
                        tempws=FETCH();
                        AH=AL/tempws;
                        AL%=tempws;
                        setznp16(AX);
                        cycles-=83;
                        break;
                        case 0xD5: /*AAD*/
                        tempws=FETCH();
                        AL=(AH*tempws)+AL;
                        AH=0;
                        setznp16(AX);
                        cycles-=60;
                        break;
                        case 0xD6: /*SETALC*/
                        AL = (flags & C_FLAG) ? 0xff : 0;
                        cycles -= 4;
                        break;
                        case 0xD7: /*XLAT*/
                        addr=BX+AL;
			cpu_state.last_ea = addr;
                        AL=readmemb(ds+addr);
                        cycles-=11;
                        break;
                        case 0xD9: case 0xDA: case 0xDB: case 0xDD: /*ESCAPE*/
                        case 0xDC: case 0xDE: case 0xDF: case 0xD8:
                        fetchea();
                        geteab();
                        break;

                        case 0xE0: /*LOOPNE*/
                        offset=(int8_t)FETCH();
                        CX--;
                        if (CX && !(flags&Z_FLAG)) { cpu_state.pc+=offset; cycles-=12; FETCHCLEAR(); }
                        cycles-=6;
                        break;
                        case 0xE1: /*LOOPE*/
                        offset=(int8_t)FETCH();
                        CX--;
                        if (CX && (flags&Z_FLAG)) { cpu_state.pc+=offset; cycles-=12; FETCHCLEAR(); }
                        cycles-=6;
                        break;
                        case 0xE2: /*LOOP*/
                        offset=(int8_t)FETCH();
                        CX--;
                        if (CX) { cpu_state.pc+=offset; cycles-=12; FETCHCLEAR(); }
                        cycles-=5;
                        break;
                        case 0xE3: /*JCXZ*/
                        offset=(int8_t)FETCH();
                        if (!CX) { cpu_state.pc+=offset; cycles-=12; FETCHCLEAR(); }
                        cycles-=6;
                        break;

                        case 0xE4: /*IN AL*/
                        temp=FETCH();
                        AL=inb(temp);
                        cycles-=14;
                        break;
                        case 0xE5: /*IN AX*/
                        temp=FETCH();
                        AL=inb(temp);
                        AH=inb(temp+1);
                        cycles-=14;
                        break;
                        case 0xE6: /*OUT AL*/
                        temp=FETCH();
                        outb(temp,AL);
                        cycles-=14;
                        break;
                        case 0xE7: /*OUT AX*/
                        temp=FETCH();
                        outb(temp,AL);
                        outb(temp+1,AH);
                        cycles-=14;
                        break;

                        case 0xE8: /*CALL rel 16*/
                        tempw=getword();
                        if (cpu_state.ssegs) ss=oldss;
                        writememw(ss,((SP-2)&0xFFFF),cpu_state.pc);
                        SP-=2;
			cpu_state.last_ea = SP;
                        cpu_state.pc+=tempw;
                        cycles-=23;
                        FETCHCLEAR();
                        break;
                        case 0xE9: /*JMP rel 16*/
                        tempw = getword();
                        cpu_state.pc += tempw;
                        cycles-=15;
                        FETCHCLEAR();
                        break;
                        case 0xEA: /*JMP far*/
                        addr=getword();
                        tempw=getword();
                        cpu_state.pc=addr;
                        loadcs(tempw);
                        cycles-=15;
                        FETCHCLEAR();
                        break;
                        case 0xEB: /*JMP rel*/
                        offset=(int8_t)FETCH();
                        cpu_state.pc+=offset;
                        cycles-=15;
                        FETCHCLEAR();
                        break;
                        case 0xEC: /*IN AL,DX*/
                        AL=inb(DX);
                        cycles-=12;
                        break;
                        case 0xED: /*IN AX,DX*/
                        AL=inb(DX);
                        AH=inb(DX+1);
                        cycles-=12;
                        break;
                        case 0xEE: /*OUT DX,AL*/
                        outb(DX,AL);
                        cycles-=12;
                        break;
                        case 0xEF: /*OUT DX,AX*/
                        outb(DX,AL);
                        outb(DX+1,AH);
                        cycles-=12;
                        break;

                        case 0xF0: /*LOCK*/
			case 0xF1: /*LOCK alias*/
                        cycles-=4;
                        break;

                        case 0xF2: /*REPNE*/
                        rep(0);
                        break;
                        case 0xF3: /*REPE*/
                        rep(1);
                        break;

                        case 0xF4: /*HLT*/
                        inhlt=1;
                        cpu_state.pc--;
                        FETCHCLEAR();
                        cycles-=2;
                        break;
                        case 0xF5: /*CMC*/
                        flags^=C_FLAG;
                        cycles-=2;
                        break;

                        case 0xF6:
                        fetchea();
                        temp=geteab();
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*TEST b,#8*/
				case 0x08:
                                temp2=FETCH();
                                temp&=temp2;
                                setznp8(temp);
                                flags&=~(C_FLAG|V_FLAG|A_FLAG);
                                cycles-=((cpu_mod==3)?5:11);
                                break;
                                case 0x10: /*NOT b*/
                                temp=~temp;
                                seteab(temp);
                                cycles-=((cpu_mod==3)?3:24);
                                break;
                                case 0x18: /*NEG b*/
                                setsub8(0,temp);
                                temp=0-temp;
                                seteab(temp);
                                cycles-=((cpu_mod==3)?3:24);
                                break;
                                case 0x20: /*MUL AL,b*/
                                setznp8(AL);
                                AX=AL*temp;
                                if (AX) flags&=~Z_FLAG;
                                else    flags|=Z_FLAG;
                                if (AH) flags|=(C_FLAG|V_FLAG);
                                else    flags&=~(C_FLAG|V_FLAG);
                                cycles-=70;
                                break;
                                case 0x28: /*IMUL AL,b*/
                                setznp8(AL);
                                tempws=(int)((int8_t)AL)*(int)((int8_t)temp);
                                AX=tempws&0xFFFF;
                                if (AX) flags&=~Z_FLAG;
                                else    flags|=Z_FLAG;
                                if (AH) flags|=(C_FLAG|V_FLAG);
                                else    flags&=~(C_FLAG|V_FLAG);
                                cycles-=80;
                                break;
                                case 0x30: /*DIV AL,b*/
                                tempw=AX;
                                if (temp)
                                {
                                        tempw2=tempw%temp;
                                                AH=tempw2;
                                                tempw/=temp;
                                                AL=tempw&0xFF;
                                }
                                else
                                {
                                        pclog("DIVb BY 0 %04X:%04X\n",cs>>4,cpu_state.pc);
                                        writememw(ss,(SP-2)&0xFFFF,flags|0xF000);
                                        writememw(ss,(SP-4)&0xFFFF,CS);
                                        writememw(ss,(SP-6)&0xFFFF,cpu_state.pc);
                                        SP-=6;
                                        flags&=~I_FLAG;
                                        flags&=~T_FLAG;
                                        cpu_state.pc=readmemw(0,0);
                                        loadcs(readmemw(0,2));
                                        FETCHCLEAR();
                                }
                                cycles-=80;
                                break;
                                case 0x38: /*IDIV AL,b*/
                                tempws=(int)AX;
                                if (temp)
                                {
                                        tempw2=tempws%(int)((int8_t)temp);
                                                AH=tempw2&0xFF;
                                                tempws/=(int)((int8_t)temp);
                                                AL=tempws&0xFF;
                                }
                                else
                                {
                                        pclog("IDIVb BY 0 %04X:%04X\n",cs>>4,cpu_state.pc);
                                        writememw(ss,(SP-2)&0xFFFF,flags|0xF000);
                                        writememw(ss,(SP-4)&0xFFFF,CS);
                                        writememw(ss,(SP-6)&0xFFFF,cpu_state.pc);
                                        SP-=6;
                                        flags&=~I_FLAG;
                                        flags&=~T_FLAG;
                                        cpu_state.pc=readmemw(0,0);
                                        loadcs(readmemw(0,2));
                                        FETCHCLEAR();
                                }
                                cycles-=101;
                                break;
                        }
                        break;

                        case 0xF7:
                        fetchea();
                        tempw=geteaw();
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*TEST w*/
				case 0x08:
                                tempw2=getword();
                                setznp16(tempw&tempw2);
                                flags&=~(C_FLAG|V_FLAG|A_FLAG);
                                cycles-=((cpu_mod==3)?5:11);
                                break;
                                case 0x10: /*NOT w*/
                                seteaw(~tempw);
                                cycles-=((cpu_mod==3)?3:24);
                                break;
                                case 0x18: /*NEG w*/
                                setsub16(0,tempw);
                                tempw=0-tempw;
                                seteaw(tempw);
                                cycles-=((cpu_mod==3)?3:24);
                                break;
                                case 0x20: /*MUL AX,w*/
                                setznp16(AX);
                                templ=AX*tempw;
                                AX=templ&0xFFFF;
                                DX=templ>>16;
                                if (AX|DX) flags&=~Z_FLAG;
                                else       flags|=Z_FLAG;
                                if (DX)    flags|=(C_FLAG|V_FLAG);
                                else       flags&=~(C_FLAG|V_FLAG);
                                cycles-=118;
                                break;
                                case 0x28: /*IMUL AX,w*/
                                setznp16(AX);
                                tempws=(int)((int16_t)AX)*(int)((int16_t)tempw);
                                if ((tempws>>15) && ((tempws>>15)!=-1)) flags|=(C_FLAG|V_FLAG);
                                else                                    flags&=~(C_FLAG|V_FLAG);
                                AX=tempws&0xFFFF;
                                tempws=(uint16_t)(tempws>>16);
                                DX=tempws&0xFFFF;
                                if (AX|DX) flags&=~Z_FLAG;
                                else       flags|=Z_FLAG;
                                cycles-=128;
                                break;
                                case 0x30: /*DIV AX,w*/
                                templ=(DX<<16)|AX;
                                if (tempw)
                                {
                                        tempw2=templ%tempw;
                                        DX=tempw2;
                                        templ/=tempw;
                                        AX=templ&0xFFFF;
                                }
                                else
                                {
                                        pclog("DIVw BY 0 %04X:%04X\n",cs>>4,cpu_state.pc);
                                        writememw(ss,(SP-2)&0xFFFF,flags|0xF000);
                                        writememw(ss,(SP-4)&0xFFFF,CS);
                                        writememw(ss,(SP-6)&0xFFFF,cpu_state.pc);
                                        SP-=6;
                                        flags&=~I_FLAG;
                                        flags&=~T_FLAG;
                                        cpu_state.pc=readmemw(0,0);
                                        loadcs(readmemw(0,2));
                                        FETCHCLEAR();
                                }
                                cycles-=144;
                                break;
                                case 0x38: /*IDIV AX,w*/
                                tempws=(int)((DX<<16)|AX);
                                if (tempw)
                                {
                                        tempw2=tempws%(int)((int16_t)tempw);
                                                DX=tempw2;
                                                tempws/=(int)((int16_t)tempw);
                                                AX=tempws&0xFFFF;
                                }
                                else
                                {
                                        pclog("IDIVw BY 0 %04X:%04X\n",cs>>4,cpu_state.pc);
                                        writememw(ss,(SP-2)&0xFFFF,flags|0xF000);
                                        writememw(ss,(SP-4)&0xFFFF,CS);
                                        writememw(ss,(SP-6)&0xFFFF,cpu_state.pc);
                                        SP-=6;
                                        flags&=~I_FLAG;
                                        flags&=~T_FLAG;
                                        cpu_state.pc=readmemw(0,0);
                                        loadcs(readmemw(0,2));
                                        FETCHCLEAR();
                                }
                                cycles-=165;
                                break;
                        }
                        break;

                        case 0xF8: /*CLC*/
                        flags&=~C_FLAG;
                        cycles-=2;
                        break;
                        case 0xF9: /*STC*/
                        flags|=C_FLAG;
                        cycles-=2;
                        break;
                        case 0xFA: /*CLI*/
                        flags&=~I_FLAG;
                        cycles-=3;
                        break;
                        case 0xFB: /*STI*/
                        flags|=I_FLAG;
                        cycles-=2;
                        break;
                        case 0xFC: /*CLD*/
                        flags&=~D_FLAG;
                        cycles-=2;
                        break;
                        case 0xFD: /*STD*/
                        flags|=D_FLAG;
                        cycles-=2;
                        break;

                        case 0xFE: /*INC/DEC b*/
                        fetchea();
                        temp=geteab();
                        flags&=~V_FLAG;
                        if (rmdat&0x38)
                        {
                                setsub8nc(temp,1);
                                temp2=temp-1;
                                if ((temp&0x80) && !(temp2&0x80)) flags|=V_FLAG;
                        }
                        else
                        {
                                setadd8nc(temp,1);
                                temp2=temp+1;
                                if ((temp2&0x80) && !(temp&0x80)) flags|=V_FLAG;
                        }
                        seteab(temp2);
                        cycles-=((cpu_mod==3)?3:23);
                        break;

                        case 0xFF:
                        fetchea();
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*INC w*/
                                tempw=geteaw();
                                setadd16nc(tempw,1);
                                seteaw(tempw+1);
                                cycles-=((cpu_mod==3)?3:23);
                                break;
                                case 0x08: /*DEC w*/
                                tempw=geteaw();
                                setsub16nc(tempw,1);
                                seteaw(tempw-1);
                                cycles-=((cpu_mod==3)?3:23);
                                break;
                                case 0x10: /*CALL*/
                                tempw=geteaw();
                                if (cpu_state.ssegs) ss=oldss;
                                writememw(ss,(SP-2)&0xFFFF,cpu_state.pc);
                                SP-=2;
				cpu_state.last_ea = SP;
                                cpu_state.pc=tempw;
                                cycles-=((cpu_mod==3)?20:29);
                                FETCHCLEAR();
                                break;
                                case 0x18: /*CALL far*/
                                tempw=readmemw(easeg,cpu_state.eaaddr);
                                tempw2=readmemw(easeg,(cpu_state.eaaddr+2)&0xFFFF);
                                tempw3=CS;
                                tempw4=cpu_state.pc;
                                if (cpu_state.ssegs) ss=oldss;
                                cpu_state.pc=tempw;
                                loadcs(tempw2);
                                writememw(ss,(SP-2)&0xFFFF,tempw3);
                                writememw(ss,((SP-4)&0xFFFF),tempw4);
                                SP-=4;
				cpu_state.last_ea = SP;
                                cycles-=53;
                                FETCHCLEAR();
                                break;
                                case 0x20: /*JMP*/
                                cpu_state.pc=geteaw();
                                cycles-=((cpu_mod==3)?11:18);
                                FETCHCLEAR();
                                break;
                                case 0x28: /*JMP far*/
                                cpu_state.pc=readmemw(easeg,cpu_state.eaaddr);
                                loadcs(readmemw(easeg,(cpu_state.eaaddr+2)&0xFFFF));
                                cycles-=24;
                                FETCHCLEAR();
                                break;
                                case 0x30: /*PUSH w*/
                                case 0x38: /*PUSH w alias, reported by reenigne*/
                                tempw=geteaw();
                                if (cpu_state.ssegs) ss=oldss;
                                writememw(ss,((SP-2)&0xFFFF),tempw);
                                SP-=2;
				cpu_state.last_ea = SP;
                                cycles-=((cpu_mod==3)?15:24);
                                break;
                        }
                        break;

                        default:
                        FETCH();
                        cycles-=8;
                        break;
                }
                cpu_state.pc&=0xFFFF;

                if (cpu_state.ssegs)
                {
                        ds=oldds;
                        ss=oldss;
                        cpu_state.ssegs=0;
                }
                
                FETCHADD(((cycdiff-cycles)-memcycs)-fetchclocks);
                if ((cycdiff-cycles)<memcycs) cycles-=(memcycs-(cycdiff-cycles));
                if (romset==ROM_IBMPC)
                {
                        if ((cs+cpu_state.pc)==0xFE4A7) /*You didn't seriously think I was going to emulate the cassette, did you?*/
                        {
                                CX=1;
                                BX=0x500;
                        }
                }
                memcycs=0;

                insc++;
                clockhardware();

                if (trap && (flags&T_FLAG) && !noint)
                {
                        writememw(ss,(SP-2)&0xFFFF,flags|0xF000);
                        writememw(ss,(SP-4)&0xFFFF,CS);
                        writememw(ss,(SP-6)&0xFFFF,cpu_state.pc);
                        SP-=6;
                        addr=1<<2;
                        flags&=~I_FLAG;
                        flags&=~T_FLAG;
                        cpu_state.pc=readmemw(0,addr);
                        loadcs(readmemw(0,addr+2));
                        FETCHCLEAR();
                }
                else if (nmi && nmi_enable && nmi_mask)
                {
                        writememw(ss,(SP-2)&0xFFFF,flags|0xF000);
                        writememw(ss,(SP-4)&0xFFFF,CS);
                        writememw(ss,(SP-6)&0xFFFF,cpu_state.pc);
                        SP-=6;
                        addr=2<<2;
                        flags&=~I_FLAG;
                        flags&=~T_FLAG;
                        cpu_state.pc=readmemw(0,addr);
                        loadcs(readmemw(0,addr+2));
                        FETCHCLEAR();
                        nmi_enable = 0;
                }
                else if (takeint && !cpu_state.ssegs && !noint)
                {
                        temp=picinterrupt();
                        if (temp!=0xFF)
                        {
                                if (inhlt) cpu_state.pc++;
                                writememw(ss,(SP-2)&0xFFFF,flags|0xF000);
                                writememw(ss,(SP-4)&0xFFFF,CS);
                                writememw(ss,(SP-6)&0xFFFF,cpu_state.pc);
                                SP-=6;
                                addr=temp<<2;
                                flags&=~I_FLAG;
                                flags&=~T_FLAG;
                                cpu_state.pc=readmemw(0,addr);
                                loadcs(readmemw(0,addr+2));
                                FETCHCLEAR();
                        }
                }
                takeint = (flags&I_FLAG) && (pic.pend&~pic.mask);

                if (noint) noint=0;
                ins++;
        }
}

