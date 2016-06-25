/*The below is (with some removals) a reasonable emulation of the ET4000/W32i blitter.
  Unfortunately the Diamond Stealth 32 is actually an ET4000/W32p! Which has a different
  blitter. If only I'd dug out and looked at the card before trying to emulate it.

  This might be of use for an attempt at emulating an ET4000/W32i.
  */
#if 0

#include "ibm.h"

int et4k_b8000;

struct
{
        struct
        {
                uint32_t pattern_addr,source_addr,dest_addr;
                uint16_t pattern_off,source_off,dest_off;
                uint8_t vbus,xy_dir;
                uint8_t pattern_wrap,source_wrap;
                uint16_t count_x,count_y;
                uint8_t ctrl_routing,ctrl_reload;
                uint8_t rop_fg,rop_bg;
                uint16_t pos_x,pos_y;
        } queued,internal;
        uint32_t pattern_addr,source_addr,dest_addr;
        uint32_t pattern_back,dest_back;
        int pattern_x,source_x;
        int pattern_x_back;
        int pattern_y,source_y;
        uint8_t status;
        uint32_t cpu_input;
        int cpu_input_num;
} acl;

#define ACL_WRST 1
#define ACL_RDST 2
#define ACL_XYST 4
#define ACL_SSO  8

struct
{
        uint32_t base[3];
        uint8_t ctrl;
} mmu;

void et4000w32_reset()
{
        acl.status=0;
        acl.cpu_input_num=0;
}

void et4000w32_blit_start();
void et4000w32_blit(int count, uint32_t mix, uint32_t sdat, int cpu_input);

int et4000w32_vbus[4]={1,2,4,4};

void et4000w32_mmu_write(uint32_t addr, uint8_t val)
{
        int bank;
        pclog("ET4K write %08X %02X %i %02X %02X %04X(%08X):%08X  %04X %04X %02X %08X\n",addr,val,acl.cpu_input_num,acl.status,acl.internal.ctrl_routing,CS,cs,pc,CS,DI,mmu.ctrl,mmu.base[2]);
        switch (addr&0x6000)
        {
                case 0x0000: /*MMU 0*/
                case 0x2000: /*MMU 1*/
                case 0x4000: /*MMU 2*/
                bank=(addr>>13)&3;
                if (mmu.ctrl&(1<<bank))
                {
                        if (!(acl.status&ACL_XYST))
                        {
//                                pclog("!ACL_XYST\n");
                                /*if ((acl.internal.ctrl_routing&0x30)==0x10) */acl.queued.dest_addr=(addr&0x1FFF)+mmu.base[bank];
                                acl.internal=acl.queued;
                                et4000w32_blit_start();
                                if (!(acl.internal.ctrl_routing&0x37)) et4000w32_blit(0xFFFFFF, ~0, 0, 0);
                                acl.cpu_input_num=0;
                        }
//                        else if (!(acl.internal.ctrl_routing&7)) pclog("ACL_XYST\n");
                        if (acl.internal.ctrl_routing&7)
                        {
                                acl.cpu_input=(acl.cpu_input&~(0xFF<<(acl.cpu_input_num*8)))|(val<<(acl.cpu_input_num*8));
                                acl.cpu_input_num++;
                                if (acl.cpu_input_num == et4000w32_vbus[acl.internal.vbus & 3])
                                {
                                        if ((acl.internal.ctrl_routing&7)==2)
                                           et4000w32_blit(acl.cpu_input_num << 3, acl.cpu_input, 0, 1);
                                        else if ((acl.internal.ctrl_routing&7)==1)
                                           et4000w32_blit(acl.cpu_input_num, ~0, acl.cpu_input, 2);
                                        else
                                           pclog("Bad ET4K routing %i\n",acl.internal.ctrl_routing&7);
                                        acl.cpu_input_num=0;
                                }
                        }
//                        else
//                           pclog("Not ctrl_routing\n");
                }
                else
                {
                        vram[(addr&0x1FFF)+mmu.base[bank]]=val;
                        changedvram[((addr&0x1FFF)+mmu.base[bank])>>12]=changeframecount;
                }
                break;
                case 0x6000:
                switch (addr&0x7FFF)
                {
                        case 0x7F00: mmu.base[0]=(mmu.base[0]&0xFFFFFF00)|val;       break;
                        case 0x7F01: mmu.base[0]=(mmu.base[0]&0xFFFF00FF)|(val<<8);  break;
                        case 0x7F02: mmu.base[0]=(mmu.base[0]&0xFF00FFFF)|(val<<16); break;
                        case 0x7F03: mmu.base[0]=(mmu.base[0]&0x00FFFFFF)|(val<<24); break;
                        case 0x7F04: mmu.base[1]=(mmu.base[1]&0xFFFFFF00)|val;       break;
                        case 0x7F05: mmu.base[1]=(mmu.base[1]&0xFFFF00FF)|(val<<8);  break;
                        case 0x7F06: mmu.base[1]=(mmu.base[1]&0xFF00FFFF)|(val<<16); break;
                        case 0x7F07: mmu.base[1]=(mmu.base[1]&0x00FFFFFF)|(val<<24); break;
                        case 0x7F08: mmu.base[2]=(mmu.base[2]&0xFFFFFF00)|val;       break;
                        case 0x7F09: mmu.base[2]=(mmu.base[2]&0xFFFF00FF)|(val<<8);  break;
                        case 0x7F0A: mmu.base[2]=(mmu.base[2]&0xFF00FFFF)|(val<<16); break;
                        case 0x7F0B: mmu.base[2]=(mmu.base[2]&0x00FFFFFF)|(val<<24); break;
                        case 0x7F13: mmu.ctrl=val; break;

                        case 0x7F80: acl.queued.pattern_addr=(acl.queued.pattern_addr&0xFFFFFF00)|val;       break;
                        case 0x7F81: acl.queued.pattern_addr=(acl.queued.pattern_addr&0xFFFF00FF)|(val<<8);  break;
                        case 0x7F82: acl.queued.pattern_addr=(acl.queued.pattern_addr&0xFF00FFFF)|(val<<16); break;
                        case 0x7F83: acl.queued.pattern_addr=(acl.queued.pattern_addr&0x00FFFFFF)|(val<<24); break;
                        case 0x7F84: acl.queued.source_addr =(acl.queued.source_addr &0xFFFFFF00)|val;       break;
                        case 0x7F85: acl.queued.source_addr =(acl.queued.source_addr &0xFFFF00FF)|(val<<8);  break;
                        case 0x7F86: acl.queued.source_addr =(acl.queued.source_addr &0xFF00FFFF)|(val<<16); break;
                        case 0x7F87: acl.queued.source_addr =(acl.queued.source_addr &0x00FFFFFF)|(val<<24); break;
                        case 0x7F88: acl.queued.pattern_off=(acl.queued.pattern_off&0xFF00)|val;      break;
                        case 0x7F89: acl.queued.pattern_off=(acl.queued.pattern_off&0x00FF)|(val<<8); break;
                        case 0x7F8A: acl.queued.source_off =(acl.queued.source_off &0xFF00)|val;      break;
                        case 0x7F8B: acl.queued.source_off =(acl.queued.source_off &0x00FF)|(val<<8); break;
                        case 0x7F8C: acl.queued.dest_off   =(acl.queued.dest_off   &0xFF00)|val;      break;
                        case 0x7F8D: acl.queued.dest_off   =(acl.queued.dest_off   &0x00FF)|(val<<8); break;
                        case 0x7F8E: acl.queued.vbus=val; break;
                        case 0x7F8F: acl.queued.xy_dir=val; break;
                        case 0x7F90: acl.queued.pattern_wrap=val; break;
                        case 0x7F92: acl.queued.source_wrap=val; break;
                        case 0x7F98: acl.queued.count_x    =(acl.queued.count_x    &0xFF00)|val;      break;
                        case 0x7F99: acl.queued.count_x    =(acl.queued.count_x    &0x00FF)|(val<<8); break;
                        case 0x7F9A: acl.queued.count_y    =(acl.queued.count_y    &0xFF00)|val;      break;
                        case 0x7F9B: acl.queued.count_y    =(acl.queued.count_y    &0x00FF)|(val<<8); break;
                        case 0x7F9C: acl.queued.ctrl_routing=val; break;
                        case 0x7F9D: acl.queued.ctrl_reload =val; break;
                        case 0x7F9E: acl.queued.rop_bg      =val; break;
                        case 0x7F9F: acl.queued.rop_fg      =val; break;
                        case 0x7FA0: acl.queued.dest_addr   =(acl.queued.dest_addr   &0xFFFFFF00)|val;       break;
                        case 0x7FA1: acl.queued.dest_addr   =(acl.queued.dest_addr   &0xFFFF00FF)|(val<<8);  break;
                        case 0x7FA2: acl.queued.dest_addr   =(acl.queued.dest_addr   &0xFF00FFFF)|(val<<16); break;
                        case 0x7FA3: acl.queued.dest_addr   =(acl.queued.dest_addr   &0x00FFFFFF)|(val<<24);
                        acl.internal=acl.queued;
                        et4000w32_blit_start();
                        acl.cpu_input_num=0;
                        if (!(acl.queued.ctrl_routing&0x37))
                        {
                                et4000w32_blit(0xFFFFFF, ~0, 0, 0);
                        }
                        break;
                }
                break;
        }
}

uint8_t et4000w32_mmu_read(uint32_t addr)
{
        int bank;
        pclog("ET4K read %08X %04X(%08X):%08X\n",addr,CS,cs,pc);
        switch (addr&0x6000)
        {
                case 0x0000: /*MMU 0*/
                case 0x2000: /*MMU 1*/
                case 0x4000: /*MMU 2*/
                bank=(addr>>13)&3;
                if (mmu.ctrl&(1<<bank))
                {
                        /*???*/
                        return 0xFF;
                }
                return vram[(addr&0x1FFF)+mmu.base[bank]];
                case 0x6000:
                switch (addr&0x7FFF)
                {
                        case 0x7F00: return mmu.base[0];
                        case 0x7F01: return mmu.base[0]>>8;
                        case 0x7F02: return mmu.base[0]>>16;
                        case 0x7F03: return mmu.base[0]>>24;
                        case 0x7F04: return mmu.base[1];
                        case 0x7F05: return mmu.base[1]>>8;
                        case 0x7F06: return mmu.base[1]>>16;
                        case 0x7F07: return mmu.base[1]>>24;
                        case 0x7F08: return mmu.base[2];
                        case 0x7F09: return mmu.base[2]>>8;
                        case 0x7F0A: return mmu.base[2]>>16;
                        case 0x7F0B: return mmu.base[2]>>24;
                        case 0x7F13: return mmu.ctrl;

                        case 0x7F36:
//                        if (acl.internal.pos_x!=acl.internal.count_x || acl.internal.pos_y!=acl.internal.count_y) return acl.status | ACL_XYST;
                        return acl.status & ~(ACL_XYST | ACL_SSO);
                        case 0x7F80: return acl.internal.pattern_addr;
                        case 0x7F81: return acl.internal.pattern_addr>>8;
                        case 0x7F82: return acl.internal.pattern_addr>>16;
                        case 0x7F83: return acl.internal.pattern_addr>>24;
                        case 0x7F84: return acl.internal.source_addr;
                        case 0x7F85: return acl.internal.source_addr>>8;
                        case 0x7F86: return acl.internal.source_addr>>16;
                        case 0x7F87: return acl.internal.source_addr>>24;
                        case 0x7F88: return acl.internal.pattern_off;
                        case 0x7F89: return acl.internal.pattern_off>>8;
                        case 0x7F8A: return acl.internal.source_off;
                        case 0x7F8B: return acl.internal.source_off>>8;
                        case 0x7F8C: return acl.internal.dest_off;
                        case 0x7F8D: return acl.internal.dest_off>>8;
                        case 0x7F8E: return acl.internal.vbus;
                        case 0x7F8F: return acl.internal.xy_dir;
                        case 0x7F90: return acl.internal.pattern_wrap;
                        case 0x7F92: return acl.internal.source_wrap;
                        case 0x7F98: return acl.internal.count_x;
                        case 0x7F99: return acl.internal.count_x>>8;
                        case 0x7F9A: return acl.internal.count_y;
                        case 0x7F9B: return acl.internal.count_y>>8;
                        case 0x7F9C: return acl.internal.ctrl_routing;
                        case 0x7F9D: return acl.internal.ctrl_reload;
                        case 0x7F9E: return acl.internal.rop_bg;
                        case 0x7F9F: return acl.internal.rop_fg;
                        case 0x7FA0: return acl.internal.dest_addr;
                        case 0x7FA1: return acl.internal.dest_addr>>8;
                        case 0x7FA2: return acl.internal.dest_addr>>16;
                        case 0x7FA3: return acl.internal.dest_addr>>24;
                }
                return 0xFF;
        }
}

int et4000w32_wrap_x[8]={0,0,3,7,15,31,63,0xFFFFFFFF};
int et4000w32_wrap_y[8]={1,2,4,8,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF};

void et4000w32_blit_start()
{
        pclog("Blit - %08X %08X %08X (%i,%i) %i  %i %i  %02X %02X  %02X\n",acl.internal.pattern_addr,acl.internal.source_addr,acl.internal.dest_addr,acl.internal.dest_addr%640,acl.internal.dest_addr/640,acl.internal.xy_dir,acl.internal.count_x,acl.internal.count_y,acl.internal.rop_fg,acl.internal.rop_bg, acl.internal.ctrl_routing);
        acl.pattern_addr=acl.internal.pattern_addr;
        acl.source_addr =acl.internal.source_addr;
        acl.dest_addr   =acl.internal.dest_addr;
        acl.dest_back   =acl.dest_addr;
        acl.internal.pos_x=acl.internal.pos_y=0;
        acl.pattern_x=acl.source_x=acl.pattern_y=acl.source_y=0;
        acl.status = ACL_XYST;
        if (!(acl.internal.ctrl_routing&7) || (acl.internal.ctrl_routing&4)) acl.status |= ACL_SSO;
        if (et4000w32_wrap_x[acl.internal.pattern_wrap&7])
        {
                acl.pattern_x=acl.pattern_addr&et4000w32_wrap_x[acl.internal.pattern_wrap&7];
                acl.pattern_addr&=~et4000w32_wrap_x[acl.internal.pattern_wrap&7];
        }
        if (!(acl.internal.pattern_wrap&0x80))
        {
                acl.pattern_y=(acl.pattern_addr/(et4000w32_wrap_x[acl.internal.pattern_wrap&7]+1))&(et4000w32_wrap_y[(acl.internal.pattern_wrap>>4)&7]-1);
                acl.pattern_addr&=~(((et4000w32_wrap_x[acl.internal.pattern_wrap&7]+1)*et4000w32_wrap_y[(acl.internal.pattern_wrap>>4)&7])-1);
        }
        acl.pattern_x_back=acl.pattern_x;
        acl.pattern_back=acl.pattern_addr;
}

void et4000w32_blit(int count, uint32_t mix, uint32_t sdat, int cpu_input)
{
        int c,d;
        uint8_t pattern,source,dest,out;
        uint8_t rop;

//        if (count>400) pclog("New blit - %i,%i %06X (%i,%i) %06X %06X\n",acl.internal.count_x,acl.internal.count_y,acl.dest_addr,acl.dest_addr%640,acl.dest_addr/640,acl.source_addr,acl.pattern_addr);
//        pclog("Blit exec - %i %i %i\n",count,acl.internal.pos_x,acl.internal.pos_y);
        while (count--)
        {
                pclog("%i,%i : ",acl.internal.pos_x,acl.internal.pos_y);
                if (acl.internal.xy_dir&1)
                {
                        pattern=vram[(acl.pattern_addr-acl.pattern_x)&0x1FFFFF];
                        source =vram[(acl.source_addr -acl.source_x) &0x1FFFFF];
                        pclog("%06X %06X ",(acl.pattern_addr-acl.pattern_x)&0x1FFFFF,(acl.source_addr -acl.source_x) &0x1FFFFF);
                }
                else
                {
                        pattern=vram[(acl.pattern_addr+acl.pattern_x)&0x1FFFFF];
                        source =vram[(acl.source_addr +acl.source_x) &0x1FFFFF];
                        pclog("%06X %06X ",(acl.pattern_addr+acl.pattern_x)&0x1FFFFF,(acl.source_addr +acl.source_x) &0x1FFFFF);
                }
                if (cpu_input==2)
                {
                        source=sdat&0xFF;
                        sdat>>=8;
                }
                dest=vram[acl.dest_addr   &0x1FFFFF];
                out=0;
                pclog("%06X %i %08X   ",acl.dest_addr,mix&1,mix);
                rop = (mix & 1) ? acl.internal.rop_fg:acl.internal.rop_bg;
                mix>>=1; mix|=0x80000000;
                for (c=0;c<8;c++)
                {
                        d=(dest & (1<<c)) ? 1:0;
                        if (source & (1<<c))  d|=2;
                        if (pattern & (1<<c)) d|=4;
                        if (rop & (1<<d)) out|=(1<<c);
                }
                pclog("%06X = %02X\n",acl.dest_addr&0x1FFFFF,out);
                vram[acl.dest_addr&0x1FFFFF]=out;
                changedvram[(acl.dest_addr&0x1FFFFF)>>12]=changeframecount;

                acl.pattern_x++;
                acl.pattern_x&=et4000w32_wrap_x[acl.internal.pattern_wrap&7];
                acl.source_x++;
                acl.source_x &=et4000w32_wrap_x[acl.internal.source_wrap&7];
                if (acl.internal.xy_dir&1) acl.dest_addr--;
                else                       acl.dest_addr++;

                acl.internal.pos_x++;
                if (acl.internal.pos_x>acl.internal.count_x)
                {
                        if (acl.internal.xy_dir&2)
                        {
                                acl.pattern_addr-=(acl.internal.pattern_off+1);
                                acl.source_addr -=(acl.internal.source_off +1);
                                acl.dest_back=acl.dest_addr=acl.dest_back-(acl.internal.dest_off+1);
                        }
                        else
                        {
                                acl.pattern_addr+=acl.internal.pattern_off+1;
                                acl.source_addr +=acl.internal.source_off +1;
                                acl.dest_back=acl.dest_addr=acl.dest_back+acl.internal.dest_off+1;
                        }
                        acl.pattern_x = acl.pattern_x_back;
                        acl.source_x = 0;
                        acl.pattern_y++;
                        if (acl.pattern_y==et4000w32_wrap_y[(acl.internal.pattern_wrap>>4)&7])
                        {
                                acl.pattern_y=0;
                                acl.pattern_addr=acl.pattern_back;
                        }
                        acl.source_y++;
                        if (acl.source_y ==et4000w32_wrap_y[(acl.internal.source_wrap >>4)&7])
                        {
                                acl.source_y=0;
                                acl.source_addr=acl.internal.source_addr;
                        }

                        acl.internal.pos_y++;
                        if (acl.internal.pos_y>acl.internal.count_y)
                        {
                                acl.status = 0;
                                return;
                        }
                        acl.internal.pos_x=0;
                        if (cpu_input) return;
                }
        }
}

/*        for (y=0;y<=acl.internal.count_y;y++)
        {
                dest_back=acl.dest_addr;
                for (x=0;x<=acl.internal.count_x;x++)
                {
                        if (acl.internal.xy_dir&1)
                        {
                                pattern=vram[(acl.pattern_addr-pattern_x)&0x1FFFFF];
                                source =vram[(acl.source_addr -source_x) &0x1FFFFF];
                        }
                        else
                        {
                                pattern=vram[(acl.pattern_addr+pattern_x)&0x1FFFFF];
                                source =vram[(acl.source_addr +source_x) &0x1FFFFF];
                        }
                        dest=vram[acl.dest_addr   &0x1FFFFF];
                        out=0;
                        for (c=0;c<8;c++)
                        {
                                d=(dest&(1<<c))?1:0;
                                if (source&(1<<c))  d|=2;
                                if (pattern&(1<<c)) d|=4;
                                if (acl.internal.rop_bg&(1<<d)) out|=(1<<c);
                        }
                        vram[acl.dest_addr&0x1FFFFF]=out;
                        changedvram[(acl.dest_addr&0x1FFFFF)>>12]=changeframecount;

                        pattern_x++;
                        pattern_x&=et4000w32_wrap_x[acl.internal.pattern_wrap&7];
                        source_x++;
                        source_x &=et4000w32_wrap_x[acl.internal.source_wrap&7];
                        if (acl.internal.xy_dir&1) acl.dest_addr--;
                        else                       acl.dest_addr++;
                }
                acl.pattern_addr+=acl.internal.pattern_off+1;
                acl.source_addr +=acl.internal.source_off+1;
                acl.dest_addr=dest_back+acl.internal.dest_off+1;
                pattern_y++;
                if (pattern_y==et4000w32_wrap_y[(acl.internal.pattern_wrap>>4)&7])
                {
                        pattern_y=0;
                        acl.pattern_addr=acl.internal.pattern_addr;
                }
                source_y++;
                if (source_y ==et4000w32_wrap_y[(acl.internal.source_wrap >>4)&7])
                {
                        source_y=0;
                        acl.source_addr=acl.internal.source_addr;
                }
        }*/

#endif
