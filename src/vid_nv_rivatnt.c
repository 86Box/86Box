/*nVidia RIVA TNT emulation*/
#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "pci.h"
#include "rom.h"
#include "thread.h"
#include "timer.h"
#include "video.h"
#include "vid_nv_rivatnt.h"
#include "vid_svga.h"
#include "vid_svga_render.h"

typedef struct rivatnt_t
{
  mem_mapping_t   linear_mapping;
  mem_mapping_t     mmio_mapping;

  rom_t bios_rom;

  svga_t svga;

  uint32_t linear_base, linear_size;

  uint16_t rma_addr;

  uint8_t pci_regs[256];

  int memory_size;

  uint8_t ext_regs_locked;

  uint8_t read_bank;
  uint8_t write_bank;

  struct
  {
	uint32_t intr;
	uint32_t intr_en;
	uint32_t intr_line;
	uint32_t enable;
  } pmc;
  
  struct
  {
	uint32_t intr;
	uint32_t intr_en;
  } pbus;
  
  struct
  {
	uint32_t intr;
	uint32_t intr_en;
	
	uint32_t ramht;
	uint32_t ramht_addr;
	uint32_t ramht_size;
	
	uint32_t ramfc;
	uint32_t ramfc_addr;
	
	uint32_t ramro;
	uint32_t ramro_addr;
	uint32_t ramro_size;
	
	uint16_t chan_mode;
	uint16_t chan_dma;
	uint16_t chan_size; //0 = 1024, 1 = 512
	
	struct
	{
		uint32_t dmaput;
		uint32_t dmaget;
	} channels[16];
	
	struct
	{
		int chanid;
		int push_enabled;
	} caches[2];
	
	struct
	{
		int subchan;
		uint16_t method;
		uint32_t param;
	} cache0, cache1[64];
  } pfifo;
  
  struct
  {
    uint32_t addr;
    uint32_t data;
    uint8_t access_reg[4];
    uint8_t mode;
  } rma;
  
  struct
  {
	uint32_t time;
  } ptimer;
  
  struct
  {
    int width;
    int bpp;
    uint32_t config_0;
  } pfb;
  
  struct
  {
    uint32_t boot0;
  } pextdev;

  struct
  {
	uint32_t obj_handle[16][8];
	uint8_t obj_class[16][8];
	
	uint32_t intr;
  } pgraph;
  
  struct
  {
    uint32_t nvpll;
    uint32_t nv_m,nv_n,nv_p;
    
    uint32_t mpll;
    uint32_t m_m,m_n,m_p;
  
    uint32_t vpll;
    uint32_t v_m,v_n,v_p;
  
    uint32_t pll_ctrl;
  
    uint32_t gen_ctrl;
  } pramdac;
  
  uint32_t pramin[0x80000];
  
  uint32_t channels[16][8][0x2000];
  
  struct
  {
	int scl;
	int sda;
	uint8_t addr; //actually 7 bits
  } i2c;
  
  int coretime;

} rivatnt_t;

const char* rivatnt_pmc_interrupts[32] =
{
	"","","","","PMEDIA","","","","PFIFO","","","","PGRAPH","","","","PRAMDAC.VIDEO","","","","PTIMER","","","","PCRTC","","","","PBUS","","",""
};

const char* rivatnt_pbus_interrupts[32] =
{
	"BUS_ERROR","","","","","","","","","","","","","","","","","","","","","","","","","","","","","","",""	
};

const char* rivatnt_pfifo_interrupts[32] =
{
	"CACHE_ERROR","","","","RUNOUT","","","","RUNOUT_OVERFLOW","","","","DMA_PUSHER","","","","DMA_PTE","","","","","","","","","","","","","","",""	
};

static uint8_t rivatnt_pci_read(int func, int addr, void *p);
static void rivatnt_pci_write(int func, int addr, uint8_t val, void *p);

static uint8_t rivatnt_in(uint16_t addr, void *p);
static void rivatnt_out(uint16_t addr, uint8_t val, void *p);

static void rivatnt_mmio_write_l(uint32_t addr, uint32_t val, void *p);

static uint8_t rivatnt_pmc_read(uint32_t addr, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;
  uint8_t ret = 0;

  //pclog("RIVA TNT PMC read %08X %04X:%08X\n", addr, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x000000: ret = 0x00; break;
  case 0x000001: ret = 0x40; break;
  case 0x000002: ret = 0x00; break;
  case 0x000003: ret = 0x00; break;
  case 0x000100: ret = rivatnt->pmc.intr & 0xff; break;
  case 0x000101: ret = (rivatnt->pmc.intr >> 8) & 0xff; break;
  case 0x000102: ret = (rivatnt->pmc.intr >> 16) & 0xff; break;
  case 0x000103: ret = (rivatnt->pmc.intr >> 24) & 0xff; break;
  case 0x000140: ret = rivatnt->pmc.intr & 0xff; break;
  case 0x000141: ret = (rivatnt->pmc.intr_en  >> 8) & 0xff; break;
  case 0x000142: ret = (rivatnt->pmc.intr_en >> 16) & 0xff; break;
  case 0x000143: ret = (rivatnt->pmc.intr_en >> 24) & 0xff; break;
  case 0x000160: ret = rivatnt->pmc.intr_line & 0xff; break;
  case 0x000161: ret = (rivatnt->pmc.intr_line >> 8) & 0xff; break;
  case 0x000162: ret = (rivatnt->pmc.intr_line >> 16) & 0xff; break;
  case 0x000163: ret = (rivatnt->pmc.intr_line >> 24) & 0xff; break;
  case 0x000200: ret = rivatnt->pmc.enable & 0xff; break;
  case 0x000201: ret = (rivatnt->pmc.enable >> 8) & 0xff; break;
  case 0x000202: ret = (rivatnt->pmc.enable >> 16) & 0xff; break;
  case 0x000203: ret = (rivatnt->pmc.enable >> 24) & 0xff; break;
  }

  return ret;
}

static void rivatnt_pmc_write(uint32_t addr, uint32_t val, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;
  //pclog("RIVA TNT PMC write %08X %08X %04X:%08X\n", addr, val, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x000100:
  rivatnt->pmc.intr &= ~val;
  break;
  case 0x000140:
  rivatnt->pmc.intr_en = val & 3;
  break;
  case 0x000200:
  rivatnt->pmc.enable = val;
  break;
  }
}

static void rivatnt_pmc_interrupt(int num, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;

  rivatnt->pmc.intr &= ~(1 << num);

  picint(1 << rivatnt->pci_regs[0x3c]);
}

static uint8_t rivatnt_pbus_read(uint32_t addr, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;
  uint8_t ret = 0;

  //pclog("RIVA TNT PBUS read %08X %04X:%08X\n", addr, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x001100: ret = rivatnt->pbus.intr & 0xff; break;
  case 0x001101: ret = (rivatnt->pbus.intr >> 8) & 0xff; break;
  case 0x001102: ret = (rivatnt->pbus.intr >> 16) & 0xff; break;
  case 0x001103: ret = (rivatnt->pbus.intr >> 24) & 0xff; break;
  case 0x001140: ret = rivatnt->pbus.intr & 0xff; break;
  case 0x001141: ret = (rivatnt->pbus.intr_en  >> 8) & 0xff; break;
  case 0x001142: ret = (rivatnt->pbus.intr_en >> 16) & 0xff; break;
  case 0x001143: ret = (rivatnt->pbus.intr_en >> 24) & 0xff; break;
  case 0x001800 ... 0x0018ff: ret = rivatnt_pci_read(0, addr - 0x1800, rivatnt); break;
  }

  return ret;
}

static void rivatnt_pbus_write(uint32_t addr, uint32_t val, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;
  //pclog("RIVA TNT PBUS write %08X %08X %04X:%08X\n", addr, val, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x001100:
  rivatnt->pbus.intr &= ~val;
  break;
  case 0x001140:
  rivatnt->pbus.intr_en = val;
  break;
  case 0x001800 ... 0x0018ff:
  rivatnt_pci_write(0, (addr & 0xfc) + 0, (val >> 0) & 0xff, rivatnt);
  rivatnt_pci_write(0, (addr & 0xfc) + 1, (val >> 8) & 0xff, rivatnt);
  rivatnt_pci_write(0, (addr & 0xfc) + 2, (val >> 16) & 0xff, rivatnt);
  rivatnt_pci_write(0, (addr & 0xfc) + 3, (val >> 24) & 0xff, rivatnt);
  break;
  }
}

static uint8_t rivatnt_pfifo_read(uint32_t addr, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;
  uint8_t ret = 0;

  pclog("RIVA TNT PFIFO read %08X %04X:%08X\n", addr, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x002100: ret = rivatnt->pfifo.intr & 0xff; break;
  case 0x002101: ret = (rivatnt->pfifo.intr >> 8) & 0xff; break;
  case 0x002102: ret = (rivatnt->pfifo.intr >> 16) & 0xff; break;
  case 0x002103: ret = (rivatnt->pfifo.intr >> 24) & 0xff; break;
  case 0x002140: ret = rivatnt->pfifo.intr_en & 0xff; break;
  case 0x002141: ret = (rivatnt->pfifo.intr_en >> 8) & 0xff; break;
  case 0x002142: ret = (rivatnt->pfifo.intr_en >> 16) & 0xff; break;
  case 0x002143: ret = (rivatnt->pfifo.intr_en >> 24) & 0xff; break;
  case 0x002210: ret = rivatnt->pfifo.ramht & 0xff; break;
  case 0x002211: ret = (rivatnt->pfifo.ramht >> 8) & 0xff; break;
  case 0x002212: ret = (rivatnt->pfifo.ramht >> 16) & 0xff; break;
  case 0x002213: ret = (rivatnt->pfifo.ramht >> 24) & 0xff; break;
  case 0x002214: ret = rivatnt->pfifo.ramfc & 0xff; break;
  case 0x002215: ret = (rivatnt->pfifo.ramfc >> 8) & 0xff; break;
  case 0x002216: ret = (rivatnt->pfifo.ramfc >> 16) & 0xff; break;
  case 0x002217: ret = (rivatnt->pfifo.ramfc >> 24) & 0xff; break;
  case 0x002218: ret = rivatnt->pfifo.ramro & 0xff; break;
  case 0x002219: ret = (rivatnt->pfifo.ramro >> 8) & 0xff; break;
  case 0x00221a: ret = (rivatnt->pfifo.ramro >> 16) & 0xff; break;
  case 0x00221b: ret = (rivatnt->pfifo.ramro >> 24) & 0xff; break;
  case 0x002504: ret = rivatnt->pfifo.chan_mode & 0xff; break;
  case 0x002505: ret = (rivatnt->pfifo.chan_mode >> 8) & 0xff; break;
  case 0x002506: ret = (rivatnt->pfifo.chan_mode >> 16) & 0xff; break;
  case 0x002507: ret = (rivatnt->pfifo.chan_mode >> 24) & 0xff; break;
  case 0x002508: ret = rivatnt->pfifo.chan_dma & 0xff; break;
  case 0x002509: ret = (rivatnt->pfifo.chan_dma >> 8) & 0xff; break;
  case 0x00250a: ret = (rivatnt->pfifo.chan_dma >> 16) & 0xff; break;
  case 0x00250b: ret = (rivatnt->pfifo.chan_dma >> 24) & 0xff; break;
  case 0x00250c: ret = rivatnt->pfifo.chan_size & 0xff; break;
  case 0x00250d: ret = (rivatnt->pfifo.chan_size >> 8) & 0xff; break;
  case 0x00250e: ret = (rivatnt->pfifo.chan_size >> 16) & 0xff; break;
  case 0x00250f: ret = (rivatnt->pfifo.chan_size >> 24) & 0xff; break;
  //HACK
  case 0x002400: ret = 0x10; break;
  case 0x002401: ret = 0x00; break;
  case 0x003204: ret = rivatnt->pfifo.caches[1].chanid; break;
  case 0x003214: ret = 0x10; break;
  case 0x003215: ret = 0x00; break;
  case 0x003220: ret = 0x01; break;
  }

  return ret;
}

static void rivatnt_pfifo_write(uint32_t addr, uint32_t val, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;
  pclog("RIVA TNT PFIFO write %08X %08X %04X:%08X\n", addr, val, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x002100:
  rivatnt->pfifo.intr &= ~val;
  break;
  case 0x002140:
  rivatnt->pfifo.intr_en = val;
  break;
  case 0x002210:
  rivatnt->pfifo.ramht = val;
  rivatnt->pfifo.ramht_addr = (val & 0x1f0) << 8;
  switch(val & 0x30000)
  {
	case 0x00000:
	rivatnt->pfifo.ramht_size = 4 * 1024;
	break;
	case 0x10000:
	rivatnt->pfifo.ramht_size = 8 * 1024;
	break;
	case 0x20000:
	rivatnt->pfifo.ramht_size = 16 * 1024;
	break;
	case 0x30000:
	rivatnt->pfifo.ramht_size = 32 * 1024;
	break;
  }
  break;
  case 0x002214:
  rivatnt->pfifo.ramfc = val;
  rivatnt->pfifo.ramfc_addr = (val & 0x1fe) << 4;
  break;
  case 0x002218:
  rivatnt->pfifo.ramro = val;
  rivatnt->pfifo.ramro_addr = (val & 0x1fe) << 4;
  if(val & 0x10000) rivatnt->pfifo.ramro_size = 8192;
  else rivatnt->pfifo.ramro_size = 512;
  break;
  case 0x002504:
  rivatnt->pfifo.chan_mode = val;
  break;
  case 0x002508:
  rivatnt->pfifo.chan_dma = val;
  break;
  case 0x00250c:
  rivatnt->pfifo.chan_size = val;
  break;
  case 0x003200:
  rivatnt->pfifo.caches[1].push_enabled = val;
  break;
  case 0x003204:
  rivatnt->pfifo.caches[1].chanid = val;
  break;
  }
}

static void rivatnt_pfifo_interrupt(int num, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;

  rivatnt->pfifo.intr &= ~(1 << num);

  rivatnt_pmc_interrupt(8, rivatnt);
}

static uint8_t rivatnt_ptimer_read(uint32_t addr, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;
  uint8_t ret = 0;

  //pclog("RIVA TNT PTIMER read %08X %04X:%08X\n", addr, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x009400: ret = rivatnt->ptimer.time & 0xff; break;
  case 0x009401: ret = (rivatnt->ptimer.time >> 8) & 0xff; break;
  case 0x009402: ret = (rivatnt->ptimer.time >> 16) & 0xff; break;
  case 0x009403: ret = (rivatnt->ptimer.time >> 24) & 0xff; break;
  }

  //TODO: gross hack to make NT4 happy for the time being.
  rivatnt->ptimer.time += 0x10000;
  
  return ret;
}

static uint8_t rivatnt_pfb_read(uint32_t addr, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;
  uint8_t ret = 0;

  //pclog("RIVA TNT PFB read %08X %04X:%08X\n", addr, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x100000:
  {
    switch(rivatnt->memory_size)
    {
    case 4: ret = 1; break;
    case 8: ret = 2; break;
    case 16: ret = 3; break;
    case 32: ret = 0; break;
    }
    ret |= 0x14;
    break;
  }
  case 0x100200: ret = rivatnt->pfb.config_0 & 0xff; break;
  case 0x100201: ret = (rivatnt->pfb.config_0 >> 8) & 0xff; break;
  case 0x100202: ret = (rivatnt->pfb.config_0 >> 16) & 0xff; break;
  case 0x100203: ret = (rivatnt->pfb.config_0 >> 24) & 0xff; break;
  }

  return ret;
}

static void rivatnt_pfb_write(uint32_t addr, uint32_t val, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;
  //pclog("RIVA TNT PFB write %08X %08X %04X:%08X\n", addr, val, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x100200:
  rivatnt->pfb.config_0 = val;
  rivatnt->pfb.width = (val & 0x3f) << 5;
  switch((val >> 8) & 3)
  {
  case 1: rivatnt->pfb.bpp = 8; break;
  case 2: rivatnt->pfb.bpp = 16; break;
  case 3: rivatnt->pfb.bpp = 32; break;
  }
  break;
  }
}

static uint8_t rivatnt_pextdev_read(uint32_t addr, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;
  uint8_t ret = 0;

  //pclog("RIVA TNT PEXTDEV read %08X %04X:%08X\n", addr, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x101000: ret = 0x9e; break;
  case 0x101001: ret = 0x01; break;
  }

  return ret;
}

static uint8_t rivatnt_pgraph_read(uint32_t addr, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;
  uint8_t ret = 0;

  pclog("RIVA TNT PGRAPH read %08X %04X:%08X\n", addr, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x400100: ret = rivatnt->pgraph.intr & 0xff; break;
  case 0x400101: ret = (rivatnt->pgraph.intr >> 8) & 0xff; break;
  case 0x400102: ret = (rivatnt->pgraph.intr >> 16) & 0xff; break;
  case 0x400103: ret = (rivatnt->pgraph.intr >> 24) & 0xff; break;
  }

  return ret;
}

static uint8_t rivatnt_pramdac_read(uint32_t addr, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;
  uint8_t ret = 0;

  //pclog("RIVA TNT PRAMDAC read %08X %04X:%08X\n", addr, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x680500: ret = rivatnt->pramdac.nvpll & 0xff; break;
  case 0x680501: ret = (rivatnt->pramdac.nvpll >> 8) & 0xff; break;
  case 0x680502: ret = (rivatnt->pramdac.nvpll >> 16) & 0xff; break;
  case 0x680503: ret = (rivatnt->pramdac.nvpll >> 24) & 0xff; break;
  case 0x680504: ret = rivatnt->pramdac.mpll & 0xff; break;
  case 0x680505: ret = (rivatnt->pramdac.mpll >> 8) & 0xff; break;
  case 0x680506: ret = (rivatnt->pramdac.mpll >> 16) & 0xff; break;
  case 0x680507: ret = (rivatnt->pramdac.mpll >> 24) & 0xff; break;
  case 0x680508: ret = rivatnt->pramdac.vpll & 0xff; break;
  case 0x680509: ret = (rivatnt->pramdac.vpll >> 8) & 0xff; break;
  case 0x68050a: ret = (rivatnt->pramdac.vpll >> 16) & 0xff; break;
  case 0x68050b: ret = (rivatnt->pramdac.vpll >> 24) & 0xff; break;
  case 0x68050c: ret = rivatnt->pramdac.pll_ctrl & 0xff; break;
  case 0x68050d: ret = (rivatnt->pramdac.pll_ctrl >> 8) & 0xff; break;
  case 0x68050e: ret = (rivatnt->pramdac.pll_ctrl >> 16) & 0xff; break;
  case 0x68050f: ret = (rivatnt->pramdac.pll_ctrl >> 24) & 0xff; break;
  case 0x680600: ret = rivatnt->pramdac.gen_ctrl & 0xff; break;
  case 0x680601: ret = (rivatnt->pramdac.gen_ctrl >> 8) & 0xff; break;
  case 0x680602: ret = (rivatnt->pramdac.gen_ctrl >> 16) & 0xff; break;
  case 0x680603: ret = (rivatnt->pramdac.gen_ctrl >> 24) & 0xff; break;
  }

  return ret;
}

static void rivatnt_pramdac_write(uint32_t addr, uint32_t val, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;
  //pclog("RIVA TNT PRAMDAC write %08X %08X %04X:%08X\n", addr, val, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x680500:
  rivatnt->pramdac.nvpll = val;
  rivatnt->pramdac.nv_m = val & 0xff;
  rivatnt->pramdac.nv_n = (val >> 8) & 0xff;
  rivatnt->pramdac.nv_p = (val >> 16) & 7;
  break;
  case 0x680504:
  rivatnt->pramdac.mpll = val;
  rivatnt->pramdac.m_m = val & 0xff;
  rivatnt->pramdac.m_n = (val >> 8) & 0xff;
  rivatnt->pramdac.m_p = (val >> 16) & 7;
  break;
  case 0x680508:
  rivatnt->pramdac.vpll = val;
  rivatnt->pramdac.v_m = val & 0xff;
  rivatnt->pramdac.v_n = (val >> 8) & 0xff;
  rivatnt->pramdac.v_p = (val >> 16) & 7;
  svga_recalctimings(svga);
  break;
  case 0x68050c:
  rivatnt->pramdac.pll_ctrl = val;
  break;
  case 0x680600:
  rivatnt->pramdac.gen_ctrl = val;
  break;
  }
}

static uint8_t rivatnt_ramht_lookup(uint32_t handle, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;
  pclog("RIVA TNT RAMHT lookup with handle %08X %04X:%08X\n", handle, CS, cpu_state.pc);
  
  uint8_t objclass;
  
  uint32_t ramht_base = rivatnt->pfifo.ramht_addr;
  
  uint32_t tmp = handle;
  uint32_t hash = 0;
  
  int bits;
  
  switch(rivatnt->pfifo.ramht_size)
  {
	case 4096: bits = 12;
	case 8192: bits = 13;
	case 16384: bits = 14;
	case 32768: bits = 15;
  }
  
  while(handle)
  {
	hash ^= (tmp & (rivatnt->pfifo.ramht_size - 1));
	tmp = handle >> 1;
  }
  
  hash ^= rivatnt->pfifo.caches[1].chanid << (bits - 4);
  
  objclass = rivatnt->pramin[ramht_base + (hash * 8)];
  objclass &= 0xff;
  return objclass;
}

static void rivatnt_pgraph_exec_method(int chanid, int subchanid, int offset, uint32_t val, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;
  pclog("RIVA TNT PGRAPH executing method %04X with object class on channel %01X %04X:%08X\n", offset, rivatnt->pgraph.obj_class[chanid][subchanid], chanid, val, CS, cpu_state.pc);

  switch(rivatnt->pgraph.obj_class[chanid][subchanid])
  {
    case 0x30:
    //NV1_NULL
    return;
  }
}

static void rivatnt_puller_exec_method(int chanid, int subchanid, int offset, uint32_t val, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;
  pclog("RIVA TNT Puller executing method %04X on channel %01X[%01X] %04X:%08X\n", offset, chanid, subchanid, val, CS, cpu_state.pc);
  
  if(offset < 0x100)
  {
	//These methods are executed by the puller itself.
	if(offset == 0)
	{
		rivatnt->pgraph.obj_handle[chanid][subchanid] = val;
		rivatnt->pgraph.obj_class[chanid][subchanid] = rivatnt_ramht_lookup(val, rivatnt);
	}
  }
  else
  {
	rivatnt_pgraph_exec_method(chanid, subchanid, offset, val, rivatnt);
  }
}

static void rivatnt_pusher_run(int chanid, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;

  while(rivatnt->pfifo.channels[chanid].dmaget != rivatnt->pfifo.channels[chanid].dmaput)
  {
    uint32_t dmaget = rivatnt->pfifo.channels[chanid].dmaget;
    uint32_t cmd = ((uint32_t*)svga->vram)[dmaget >> 2];
    uint32_t* params = ((uint32_t*)svga->vram)[(dmaget + 4) >> 2];
    if((cmd & 0xe0000003) == 0x20000000)
    {
      //old nv4 jump command
      rivatnt->pfifo.channels[chanid].dmaget = cmd & 0x1ffffffc;
    }
    else if((cmd & 0xe0030003) == 0)
    {
      //nv4 increasing method command
      uint32_t method = cmd & 0x1ffc;
      int subchannel = (cmd >> 13) & 7;
      int method_count = (cmd >> 18) & 0x7ff;
      for(int i = 0;i<=method_count;i++)
      {
        rivatnt_puller_exec_method(chanid, subchannel, method, params[i<<2], rivatnt);
        method+=4;
      }
    }
    else
    {
      pclog("RIVA TNT PFIFO Invalid DMA pusher command %08x!\n", cmd);
      rivatnt_pfifo_interrupt(12, rivatnt);
    }
    rivatnt->pfifo.channels[chanid].dmaget += 4;
  }
}

static void rivatnt_user_write(uint32_t addr, uint32_t val, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;
  pclog("RIVA TNT USER write %08X %08X %04X:%08X\n", addr, val, CS, cpu_state.pc);
  
  addr -= 0x800000;
  
  int chanid = (addr >> 16) & 0xf;
  int subchanid = (addr >> 13) & 0x7;
  int offset = addr & 0x1fff;
  
  if(rivatnt->pfifo.chan_mode & (1 << chanid))
  {
	//DMA mode, at least this has docs.
	switch(offset)
	{
	case 0x40:
	rivatnt->pfifo.channels[chanid].dmaput = val;
	if(rivatnt->pfifo.caches[1].push_enabled) rivatnt_pusher_run(chanid, rivatnt);
	break;
	case 0x44:
	rivatnt->pfifo.channels[chanid].dmaget = val;
	break;
	}
  }
  else
  {
	//I don't know what to do here, as there are basically no docs on PIO PFIFO submission.
	pclog("RIVA TNT PIO PFIFO submission attempted\n");
  }
}

static uint8_t rivatnt_mmio_read(uint32_t addr, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;
  uint8_t ret = 0;

  addr &= 0xffffff;

  pclog("RIVA TNT MMIO read %08X %04X:%08X\n", addr, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x000000 ... 0x000fff:
  ret = rivatnt_pmc_read(addr, rivatnt);
  break;
  case 0x001000 ... 0x001fff:
  ret = rivatnt_pbus_read(addr, rivatnt);
  break;
  case 0x002000 ... 0x002fff:
  ret = rivatnt_pfifo_read(addr, rivatnt);
  break;
  case 0x009000 ... 0x009fff:
  ret = rivatnt_ptimer_read(addr, rivatnt);
  break;
  case 0x100000 ... 0x100fff:
  ret = rivatnt_pfb_read(addr, rivatnt);
  break;
  case 0x101000 ... 0x101fff:
  ret = rivatnt_pextdev_read(addr, rivatnt);
  break;
  case 0x6013b4 ... 0x6013b5: case 0x6013d4 ... 0x6013d5: case 0x0c03c2 ... 0x0c03c5: case 0x0c03cc ... 0x0c03cf:
  ret = rivatnt_in(addr & 0xfff, rivatnt);
  break;
  case 0x680000 ... 0x680fff:
  ret = rivatnt_pramdac_read(addr, rivatnt);
  break;
  case 0x700000 ... 0x7fffff:
  //Assuming PRAMIN is mirrored across the address space.
  ret = (rivatnt->pramin[(addr >> 2) & 0x7ffff] & (0xff << ((addr & 3) << 3))) >> ((addr & 3) << 3);
  break;
  }
  return ret;
}

static uint16_t rivatnt_mmio_read_w(uint32_t addr, void *p)
{
  addr &= 0xffffff;
  //pclog("RIVA TNT MMIO read %08X %04X:%08X\n", addr, CS, cpu_state.pc);
  return (rivatnt_mmio_read(addr+0,p) << 0) | (rivatnt_mmio_read(addr+1,p) << 8);
}

static uint32_t rivatnt_mmio_read_l(uint32_t addr, void *p)
{
  addr &= 0xffffff;
  //pclog("RIVA TNT MMIO read %08X %04X:%08X\n", addr, CS, cpu_state.pc);
  return (rivatnt_mmio_read(addr+0,p) << 0) | (rivatnt_mmio_read(addr+1,p) << 8) | (rivatnt_mmio_read(addr+2,p) << 16) | (rivatnt_mmio_read(addr+3,p) << 24);
}

static void rivatnt_mmio_write(uint32_t addr, uint8_t val, void *p)
{
  addr &= 0xffffff;
  //pclog("RIVA TNT MMIO write %08X %02X %04X:%08X\n", addr, val, CS, cpu_state.pc);
  if(addr != 0x6013d4 && addr != 0x6013d5 && addr != 0x6013b4 && addr != 0x6013b5)
  {
    uint32_t tmp = rivatnt_mmio_read_l(addr,p);
    tmp &= ~(0xff << ((addr & 3) << 3));
    tmp |= val << ((addr & 3) << 3);
    rivatnt_mmio_write_l(addr, tmp, p);
  }
  else
  {
    rivatnt_out(addr & 0xfff, val & 0xff, p);
  }
}

static void rivatnt_mmio_write_w(uint32_t addr, uint16_t val, void *p)
{
  addr &= 0xffffff;
  //pclog("RIVA TNT MMIO write %08X %04X %04X:%08X\n", addr, val, CS, cpu_state.pc);
  uint32_t tmp = rivatnt_mmio_read_l(addr,p);
  tmp &= ~(0xffff << ((addr & 2) << 4));
  tmp |= val << ((addr & 2) << 4);
  rivatnt_mmio_write_l(addr, tmp, p);
}

static void rivatnt_mmio_write_l(uint32_t addr, uint32_t val, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;

  addr &= 0xffffff;

  pclog("RIVA TNT MMIO write %08X %08X %04X:%08X\n", addr, val, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x000000 ... 0x000fff:
  rivatnt_pmc_write(addr, val, rivatnt);
  break;
  case 0x001000 ... 0x001fff:
  rivatnt_pbus_write(addr, val, rivatnt);
  break;
  case 0x002000 ... 0x002fff:
  rivatnt_pfifo_write(addr, val, rivatnt);
  break;
  case 0x100000 ... 0x100fff:
  rivatnt_pfb_write(addr, val, rivatnt);
  break;
  case 0x680000 ... 0x680fff:
  rivatnt_pramdac_write(addr, val, rivatnt);
  break;
  case 0x700000 ... 0x7fffff:
  rivatnt->pramin[(addr >> 2) & 0x7ffff] = val;
  break;
  case 0x800000 ... 0xffffff:
  rivatnt_user_write(addr, val, rivatnt);
  break;
  }
}

static void rivatnt_poll(void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;
	svga_t *svga = &rivatnt->svga;
}

static uint8_t rivatnt_rma_in(uint16_t addr, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;
  uint8_t ret = 0;

  addr &= 0xff;

  //pclog("RIVA TNT RMA read %04X %04X:%08X\n", addr, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x00: ret = 0x65; break;
  case 0x01: ret = 0xd0; break;
  case 0x02: ret = 0x16; break;
  case 0x03: ret = 0x2b; break;
  case 0x08: case 0x09: case 0x0a: case 0x0b: ret = rivatnt_mmio_read(rivatnt->rma.addr + (addr & 3), rivatnt); break;
  }

  return ret;
}

static void rivatnt_rma_out(uint16_t addr, uint8_t val, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;

  addr &= 0xff;

  //pclog("RIVA TNT RMA write %04X %02X %04X:%08X\n", addr, val, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x04:
  rivatnt->rma.addr &= ~0xff;
  rivatnt->rma.addr |= val;
  break;
  case 0x05:
  rivatnt->rma.addr &= ~0xff00;
  rivatnt->rma.addr |= (val << 8);
  break;
  case 0x06:
  rivatnt->rma.addr &= ~0xff0000;
  rivatnt->rma.addr |= (val << 16);
  break;
  case 0x07:
  rivatnt->rma.addr &= ~0xff000000;
  rivatnt->rma.addr |= (val << 24);
  break;
  case 0x08: case 0x0c: case 0x10: case 0x14:
  rivatnt->rma.data &= ~0xff;
  rivatnt->rma.data |= val;
  break;
  case 0x09: case 0x0d: case 0x11: case 0x15:
  rivatnt->rma.data &= ~0xff00;
  rivatnt->rma.data |= (val << 8);
  break;
  case 0x0a: case 0x0e: case 0x12: case 0x16:
  rivatnt->rma.data &= ~0xff0000;
  rivatnt->rma.data |= (val << 16);
  break;
  case 0x0b: case 0x0f: case 0x13: case 0x17:
  rivatnt->rma.data &= ~0xff000000;
  rivatnt->rma.data |= (val << 24);
  if(rivatnt->rma.addr < 0x1000000) rivatnt_mmio_write_l(rivatnt->rma.addr & 0xffffff, rivatnt->rma.data, rivatnt);
  else svga_writel_linear((rivatnt->rma.addr - 0x1000000), rivatnt->rma.data, svga);
  break;
  }

  if(addr & 0x10) rivatnt->rma.addr+=4;
}

static uint8_t rivatnt_in(uint16_t addr, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;
  uint8_t ret = 0;

  switch (addr)
  {
  case 0x3D0 ... 0x3D3:
  //pclog("RIVA TNT RMA BAR Register read %04X %04X:%08X\n", addr, CS, cpu_state.pc);
  if(!(rivatnt->rma.mode & 1)) return ret;
  ret = rivatnt_rma_in(rivatnt->rma_addr + ((rivatnt->rma.mode & 0xe) << 1) + (addr & 3), rivatnt);
  return ret;
  }
  
  if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
    addr ^= 0x60;

    //        if (addr != 0x3da) pclog("S3 in %04X %04X:%08X  ", addr, CS, cpu_state.pc);
  switch (addr)
  {
  case 0x3D4:
  ret = svga->crtcreg;
  break;
  case 0x3D5:
  switch(svga->crtcreg)
  {
  case 0x3e:
  ret = (rivatnt->i2c.sda << 3) | (rivatnt->i2c.scl << 2);
  break;
  default:
  ret = svga->crtc[svga->crtcreg];
  break;
  }
  //if(svga->crtcreg > 0x18)
  //  pclog("RIVA TNT Extended CRTC read %02X %04X:%08X\n", svga->crtcreg, CS, cpu_state.pc);
  break;
  default:
  ret = svga_in(addr, svga);
  break;
  }
  //        if (addr != 0x3da) pclog("%02X\n", ret);
  return ret;
}

static void rivatnt_out(uint16_t addr, uint8_t val, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;

  uint8_t old;
  
  switch(addr)
  {
  case 0x3D0 ... 0x3D3:
  //pclog("RIVA TNT RMA BAR Register write %04X %02x %04X:%08X\n", addr, val, CS, cpu_state.pc);
  rivatnt->rma.access_reg[addr & 3] = val;
  if(!(rivatnt->rma.mode & 1)) return;
  rivatnt_rma_out(rivatnt->rma_addr + ((rivatnt->rma.mode & 0xe) << 1) + (addr & 3), rivatnt->rma.access_reg[addr & 3], rivatnt);
  return;
  }

  if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
    addr ^= 0x60;

  switch(addr)
  {
  case 0x3D4:
  svga->crtcreg = val;
  return;
  case 0x3D5:
  if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
    return;
  if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
    val = (svga->crtc[7] & ~0x10) | (val & 0x10);
  old = svga->crtc[svga->crtcreg];
  svga->crtc[svga->crtcreg] = val;
  switch(svga->crtcreg)
  {
  case 0x1a:
  svga_recalctimings(svga);
  break;
  case 0x1e:
  rivatnt->read_bank = val;
  if (svga->chain4) svga->read_bank = rivatnt->read_bank << 15;
  else              svga->read_bank = rivatnt->read_bank << 13;
  break;
  case 0x1d:
  rivatnt->write_bank = val;
  if (svga->chain4) svga->write_bank = rivatnt->write_bank << 15;
  else              svga->write_bank = rivatnt->write_bank << 13;
  break;
  case 0x26:
  if (!svga->attrff)
    svga->attraddr = val & 31;
  break;
  case 0x19:
  case 0x25:
  case 0x28:
  case 0x2d:
  svga_recalctimings(svga);
  break;
  case 0x38:
  rivatnt->rma.mode = val & 0xf;
  break;
  case 0x3f:
  rivatnt->i2c.scl = (val & 0x20) ? 1 : 0;
  rivatnt->i2c.sda = (val & 0x10) ? 1 : 0;
  break;
  }
  //if(svga->crtcreg > 0x18)
  //  pclog("RIVA TNT Extended CRTC write %02X %02x %04X:%08X\n", svga->crtcreg, val, CS, cpu_state.pc);
  if (old != val)
  {
    if (svga->crtcreg < 0xE || svga->crtcreg > 0x10)
    {
      svga->fullchange = changeframecount;
      svga_recalctimings(svga);
    }
  }
  return;
  }

  svga_out(addr, val, svga);
}

static uint8_t rivatnt_pci_read(int func, int addr, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;
  uint8_t ret = 0;
  //pclog("RIVA TNT PCI read %02X %04X:%08X\n", addr, CS, cpu_state.pc);
  switch (addr)
  {
    case 0x00: ret = 0xde; break; /*'nVidia'*/
    case 0x01: ret = 0x10; break;

    case 0x02: ret = 0x20; break; /*'RIVA TNT'*/
    case 0x03: ret = 0x00; break;

    case 0x04: ret = rivatnt->pci_regs[0x04] & 0x37; break;
    case 0x05: ret = rivatnt->pci_regs[0x05] & 0x01; break;

    case 0x06: ret = 0x20; break;
    case 0x07: ret = rivatnt->pci_regs[0x07] & 0x73; break;

    case 0x08: ret = 0x01; break; /*Revision ID*/
    case 0x09: ret = 0; break; /*Programming interface*/

    case 0x0a: ret = 0x00; break; /*Supports VGA interface*/
    case 0x0b: ret = 0x03; /*output = 3; */break;

    case 0x0e: ret = 0x00; break; /*Header type*/

    case 0x13:
    case 0x17:
    ret = rivatnt->pci_regs[addr];
    break;

    case 0x2c: case 0x2d: case 0x2e: case 0x2f:
    ret = rivatnt->pci_regs[addr];
    //if(CS == 0x0028) output = 3;
    break;

    case 0x34: ret = 0x00; break;

    case 0x3c: ret = rivatnt->pci_regs[0x3c]; break;

    case 0x3d: ret = 0x01; break; /*INTA*/

    case 0x3e: ret = 0x03; break;
    case 0x3f: ret = 0x01; break;

  }
  //        pclog("%02X\n", ret);
  return ret;
}

static void rivatnt_pci_write(int func, int addr, uint8_t val, void *p)
{
  //pclog("RIVA TNT PCI write %02X %02X %04X:%08X\n", addr, val, CS, cpu_state.pc);
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  svga_t *svga = &rivatnt->svga;
  switch (addr)
  {
    case 0x00: case 0x01: case 0x02: case 0x03:
    case 0x08: case 0x09: case 0x0a: case 0x0b:
    case 0x3d: case 0x3e: case 0x3f:
    return;

    case PCI_REG_COMMAND:
    if (val & PCI_COMMAND_IO)
    {
      io_removehandler(0x03c0, 0x0020, rivatnt_in, NULL, NULL, rivatnt_out, NULL, NULL, rivatnt);
      io_sethandler(0x03c0, 0x0020, rivatnt_in, NULL, NULL, rivatnt_out, NULL, NULL, rivatnt);
    }
    else
      io_removehandler(0x03c0, 0x0020, rivatnt_in, NULL, NULL, rivatnt_out, NULL, NULL, rivatnt);
    rivatnt->pci_regs[PCI_REG_COMMAND] = val & 0x37;
    return;

    case 0x05:
    rivatnt->pci_regs[0x05] = val & 0x01;
    return;

    case 0x07:
    rivatnt->pci_regs[0x07] = (rivatnt->pci_regs[0x07] & 0x8f) | (val & 0x70);
    return;

    case 0x13:
    {
      rivatnt->pci_regs[addr] = val;
      uint32_t mmio_addr = val << 24;
      mem_mapping_set_addr(&rivatnt->mmio_mapping, mmio_addr, 0x1000000);
      return;
    }

    case 0x17:
    {
      rivatnt->pci_regs[addr] = val;
      uint32_t linear_addr = (val << 24);
      mem_mapping_set_addr(&rivatnt->linear_mapping, linear_addr, 0x1000000);
      svga->linear_base = linear_addr;
      return;
    }

    case 0x30: case 0x32: case 0x33:
    rivatnt->pci_regs[addr] = val;
    if (rivatnt->pci_regs[0x30] & 0x01)
    {
      uint32_t addr = (rivatnt->pci_regs[0x32] << 16) | (rivatnt->pci_regs[0x33] << 24);
      //                        pclog("RIVA TNT bios_rom enabled at %08x\n", addr);
      mem_mapping_set_addr(&rivatnt->bios_rom.mapping, addr, 0x8000);
      mem_mapping_enable(&rivatnt->bios_rom.mapping);
    }
    else
    {
      //                        pclog("RIVA TNT bios_rom disabled\n");
      mem_mapping_disable(&rivatnt->bios_rom.mapping);
    }
    return;

    case 0x3c:
    rivatnt->pci_regs[0x3c] = val & 0x0f;
    return;

    case 0x40: case 0x41: case 0x42: case 0x43:
    rivatnt->pci_regs[addr - 0x14] = val; //0x40-0x43 are ways to write to 0x2c-0x2f
    return;
  }
}

static void rivatnt_recalctimings(svga_t *svga)
{
  rivatnt_t *rivatnt = (rivatnt_t *)svga->p;

  svga->ma_latch += (svga->crtc[0x19] & 0x1f) << 16;
  svga->rowoffset += (svga->crtc[0x19] & 0xe0) << 3;
  if (svga->crtc[0x25] & 0x01) svga->vtotal      += 0x400;
  if (svga->crtc[0x25] & 0x02) svga->dispend     += 0x400;
  if (svga->crtc[0x25] & 0x04) svga->vblankstart += 0x400;
  if (svga->crtc[0x25] & 0x08) svga->vsyncstart  += 0x400;
  if (svga->crtc[0x25] & 0x10) svga->htotal      += 0x100;
  if (svga->crtc[0x2d] & 0x01) svga->hdisp       += 0x100;
  //The effects of the large screen bit seem to just be doubling the row offset.
  //However, these large modes still don't work. Possibly core SVGA bug? It does report 640x2 res after all.
  if (!(svga->crtc[0x1a] & 0x04)) svga->rowoffset <<= 1;
  switch(svga->crtc[0x28] & 3)
  {
    case 1:
    svga->bpp = 8;
    svga->lowres = 0;
    svga->render = svga_render_8bpp_highres;
    break;
    case 2:
    svga->bpp = 16;
    svga->lowres = 0;
    svga->render = svga_render_16bpp_highres;
    break;
    case 3:
    svga->bpp = 32;
    svga->lowres = 0;
    svga->render = svga_render_32bpp_highres;
    break;
  }

  if((svga->crtc[0x28] & 3) != 0)
  {
    if(svga->crtc[0x1a] & 2) svga_set_ramdac_type(svga, RAMDAC_6BIT);
    else svga_set_ramdac_type(svga, RAMDAC_8BIT);
  }
  else svga_set_ramdac_type(svga, RAMDAC_6BIT);
  
  if (((svga->miscout >> 2) & 2) == 2)
  {
	double freq = 0;
	
	//if(rivatnt->pextdev.boot0 & 0x40) freq = 14318180.0;
	freq = 13500000.0;

	if(rivatnt->pramdac.v_m == 0) freq = 0;
	else
	{
		freq = (freq * rivatnt->pramdac.v_n) / (1 << rivatnt->pramdac.v_p) / rivatnt->pramdac.v_m;
		//pclog("RIVA TNT Pixel clock is %f Hz\n", freq);
	}
	
        svga->clock = cpuclock / freq;
  }
}

static void *rivatnt_init()
{
  rivatnt_t *rivatnt = malloc(sizeof(rivatnt_t));
  memset(rivatnt, 0, sizeof(rivatnt_t));

  rivatnt->memory_size = device_get_config_int("memory");

  svga_init(&rivatnt->svga, rivatnt, rivatnt->memory_size << 20,
  rivatnt_recalctimings,
  rivatnt_in, rivatnt_out,
  NULL, NULL);

  rom_init(&rivatnt->bios_rom, "roms/NV4_creative.rom", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
  if (PCI)
    mem_mapping_disable(&rivatnt->bios_rom.mapping);

  mem_mapping_add(&rivatnt->mmio_mapping,     0, 0,
    rivatnt_mmio_read,
    rivatnt_mmio_read_w,
    rivatnt_mmio_read_l,
    rivatnt_mmio_write,
    rivatnt_mmio_write_w,
    rivatnt_mmio_write_l,
    NULL,
    0,
    rivatnt);
  mem_mapping_add(&rivatnt->linear_mapping,   0, 0,
    svga_read_linear,
    svga_readw_linear,
    svga_readl_linear,
    svga_write_linear,
    svga_writew_linear,
    svga_writel_linear,
    NULL,
    0,
    &rivatnt->svga);

  io_sethandler(0x03c0, 0x0020, rivatnt_in, NULL, NULL, rivatnt_out, NULL, NULL, rivatnt);

  rivatnt->pci_regs[4] = 3;
  rivatnt->pci_regs[5] = 0;
  rivatnt->pci_regs[6] = 0;
  rivatnt->pci_regs[7] = 2;
  
  rivatnt->pci_regs[0x2c] = 0x02;
  rivatnt->pci_regs[0x2d] = 0x11;
  rivatnt->pci_regs[0x2e] = 0x16;
  rivatnt->pci_regs[0x2f] = 0x10;

  //rivatnt->pci_regs[0x3c] = 3;

  rivatnt->pmc.intr = 0;
  rivatnt->pbus.intr = 0;
  rivatnt->pfifo.intr = 0;
  rivatnt->pgraph.intr = 0;
  
  pci_add(rivatnt_pci_read, rivatnt_pci_write, rivatnt);

  return rivatnt;
}

static void rivatnt_close(void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;
  FILE *f = fopen("vram.dmp", "wb");
  fwrite(rivatnt->svga.vram, 4 << 20, 1, f);
  fclose(f);

  svga_close(&rivatnt->svga);

  free(rivatnt);
}

static int rivatnt_available()
{
  return rom_present("roms/NV4_creative.rom");
}

static void rivatnt_speed_changed(void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;

  svga_recalctimings(&rivatnt->svga);
}

static void rivatnt_force_redraw(void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;

  rivatnt->svga.fullchange = changeframecount;
}

static void rivatnt_add_status_info(char *s, int max_len, void *p)
{
  rivatnt_t *rivatnt = (rivatnt_t *)p;

  svga_add_status_info(s, max_len, &rivatnt->svga);
}

static device_config_t rivatnt_config[] =
{
  {
    .name = "memory",
    .description = "Memory size",
    .type = CONFIG_SELECTION,
    .selection =
    {
      {
        .description = "4 MB",
        .value = 4
      },
      {
        .description = "8 MB",
        .value = 8
      },
      {
        .description = "16 MB",
        .value = 16
      },
      {
        .description = ""
      }
    },
    .default_int = 16
  },
  {
    .type = -1
  }
};

device_t rivatnt_device =
{
        "nVidia RIVA TNT",
        0,
        rivatnt_init,
        rivatnt_close,
        rivatnt_available,
        rivatnt_speed_changed,
        rivatnt_force_redraw,
        rivatnt_add_status_info,
        rivatnt_config
};
