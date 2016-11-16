/*nVidia RIVA 128 emulation*/
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
#include "vid_nv_riva128.h"
#include "vid_svga.h"
#include "vid_svga_render.h"

typedef struct riva128_t
{
  mem_mapping_t   linear_mapping;
  mem_mapping_t    ramin_mapping;
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
	uint32_t obj_handle[8];
	uint8_t obj_class[8];

  uint32_t debug[4];
	
	uint32_t intr;
  uint32_t intr_en;

  uint32_t invalid;
  uint32_t invalid_en;

  uint32_t uclip_xmin, uclip_ymin, uclip_xmax, uclip_ymax;
  uint32_t oclip_xmin, oclip_ymin, oclip_xmax, oclip_ymax;

  uint32_t beta;
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

} riva128_t;

//Internally, the RIVA 128 operates in a weird 38-bit color depth, with 10 bits for RGB, and 8 bits for alpha, according to envytools.
typedef struct
{
  uint8_t a;
  unsigned r : 10;
  unsigned g : 10;
  unsigned b : 10;
} riva128_color_t;

const char* riva128_pmc_interrupts[32] =
{
	"","","","","PMEDIA","","","","PFIFO","","","","PGRAPH","","","","PRAMDAC.VIDEO","","","","PTIMER","","","","PCRTC","","","","PBUS","","",""
};

const char* riva128_pbus_interrupts[32] =
{
	"BUS_ERROR","","","","","","","","","","","","","","","","","","","","","","","","","","","","","","",""	
};

const char* riva128_pfifo_interrupts[32] =
{
	"CACHE_ERROR","","","","RUNOUT","","","","RUNOUT_OVERFLOW","","","","DMA_PUSHER","","","","DMA_PTE","","","","","","","","","","","","","","",""	
};

static uint8_t riva128_pci_read(int func, int addr, void *p);
static void riva128_pci_write(int func, int addr, uint8_t val, void *p);

static uint8_t riva128_in(uint16_t addr, void *p);
static void riva128_out(uint16_t addr, uint8_t val, void *p);

static void riva128_mmio_write_l(uint32_t addr, uint32_t val, void *p);

static riva128_color_t riva128_pgraph_expand_color(uint32_t ctx, uint32_t color)
{
  riva128_color_t ret;
  int format = ctx & 7;
  int alpha_enable = (ctx >> 3) & 1;

  switch(format)
  {
    default:
    pclog("RIVA 128 Unknown color format %i found!\n", format);
    ret.a = 0x0;
    break;
    case 0:
    ret.a = ((color >> 15) & 1) * 0xff;
    ret.r = ((color >> 10) & 0x1f) << 5;
    ret.g = ((color >> 5) & 0x1f) << 5;
    ret.b = ((color >> 0) & 0x1f) << 5;
    break;
    case 1:
    ret.a = ((color >> 24) & 0xff);
    ret.r = ((color >> 16) & 0xff) << 2;
    ret.g = ((color >> 8) & 0xff) << 2;
    ret.b = ((color >> 0) & 0xff) << 2;
    break;
    case 2:
    ret.a = ((color >> 30) & 3) * 0x55;
    ret.r = ((color >> 20) & 0x3ff);
    ret.g = ((color >> 10) & 0x3ff);
    ret.b = ((color >> 0) & 0x3ff);
    break;
    case 3:
    ret.a = ((color >> 8) & 0xff);
    ret.r = ret.g = ret.b = ((color >> 0) & 0xff) << 2;
    break;
    case 4:
    ret.a = ((color >> 16) & 0xffff) >> 8;
    ret.r = ret.g = ret.b = ((color >> 0) & 0xffff) >> 6;
    break;
  }

  if(!alpha_enable) ret.a = 0xff;

  return ret;
}

static uint32_t riva128_pgraph_blend_factor(uint32_t alpha, uint32_t beta)
{
  if(beta == 0xff) return alpha;
  if(alpha == 0xff) return beta;
  alpha >>= 4;
  beta >>= 3;
  return (alpha * beta) >> 1;
}

static uint32_t riva128_pgraph_do_blend(uint32_t factor, uint32_t dst, uint32_t src, int is_r5g5b5)
{
  factor &= 0xf8;
  if(factor == 0xf8) return src;
  if(!factor) return dst;
  src >>= 2; dst >>= 2;
  if(is_r5g5b5)
  {
    src &= 0xf8; dst &= 0xf8;
  }
  return ((dst * (0x100 - factor)) + (src * factor)) >> 6;
}

static uint8_t riva128_pmc_read(uint32_t addr, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint8_t ret = 0;

  //pclog("RIVA 128 PMC read %08X %04X:%08X\n", addr, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x000000: ret = 0x00; break;
  case 0x000001: ret = 0x40; break;
  case 0x000002: ret = 0x00; break;
  case 0x000003: ret = 0x00; break;
  case 0x000100: ret = riva128->pmc.intr & 0xff; break;
  case 0x000101: ret = (riva128->pmc.intr >> 8) & 0xff; break;
  case 0x000102: ret = (riva128->pmc.intr >> 16) & 0xff; break;
  case 0x000103: ret = (riva128->pmc.intr >> 24) & 0xff; break;
  case 0x000140: ret = riva128->pmc.intr & 0xff; break;
  case 0x000141: ret = (riva128->pmc.intr_en  >> 8) & 0xff; break;
  case 0x000142: ret = (riva128->pmc.intr_en >> 16) & 0xff; break;
  case 0x000143: ret = (riva128->pmc.intr_en >> 24) & 0xff; break;
  case 0x000160: ret = riva128->pmc.intr_line & 0xff; break;
  case 0x000161: ret = (riva128->pmc.intr_line >> 8) & 0xff; break;
  case 0x000162: ret = (riva128->pmc.intr_line >> 16) & 0xff; break;
  case 0x000163: ret = (riva128->pmc.intr_line >> 24) & 0xff; break;
  case 0x000200: ret = riva128->pmc.enable & 0xff; break;
  case 0x000201: ret = (riva128->pmc.enable >> 8) & 0xff; break;
  case 0x000202: ret = (riva128->pmc.enable >> 16) & 0xff; break;
  case 0x000203: ret = (riva128->pmc.enable >> 24) & 0xff; break;
  }

  return ret;
}

static void riva128_pmc_write(uint32_t addr, uint32_t val, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  //pclog("RIVA 128 PMC write %08X %08X %04X:%08X\n", addr, val, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x000100:
  riva128->pmc.intr &= ~val;
  break;
  case 0x000140:
  riva128->pmc.intr_en = val & 3;
  break;
  case 0x000200:
  riva128->pmc.enable = val;
  break;
  }
}

static void riva128_pmc_interrupt(int num, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;

  riva128->pmc.intr &= ~(1 << num);

  picint(1 << riva128->pci_regs[0x3c]);
}

static uint8_t riva128_pbus_read(uint32_t addr, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint8_t ret = 0;

  //pclog("RIVA 128 PBUS read %08X %04X:%08X\n", addr, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x001100: ret = riva128->pbus.intr & 0xff; break;
  case 0x001101: ret = (riva128->pbus.intr >> 8) & 0xff; break;
  case 0x001102: ret = (riva128->pbus.intr >> 16) & 0xff; break;
  case 0x001103: ret = (riva128->pbus.intr >> 24) & 0xff; break;
  case 0x001140: ret = riva128->pbus.intr & 0xff; break;
  case 0x001141: ret = (riva128->pbus.intr_en  >> 8) & 0xff; break;
  case 0x001142: ret = (riva128->pbus.intr_en >> 16) & 0xff; break;
  case 0x001143: ret = (riva128->pbus.intr_en >> 24) & 0xff; break;
  case 0x001800 ... 0x0018ff: ret = riva128_pci_read(0, addr - 0x1800, riva128); break;
  }

  return ret;
}

static void riva128_pbus_write(uint32_t addr, uint32_t val, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  //pclog("RIVA 128 PBUS write %08X %08X %04X:%08X\n", addr, val, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x001100:
  riva128->pbus.intr &= ~val;
  break;
  case 0x001140:
  riva128->pbus.intr_en = val;
  break;
  case 0x001800 ... 0x0018ff:
  riva128_pci_write(0, (addr & 0xfc) + 0, (val >> 0) & 0xff, riva128);
  riva128_pci_write(0, (addr & 0xfc) + 1, (val >> 8) & 0xff, riva128);
  riva128_pci_write(0, (addr & 0xfc) + 2, (val >> 16) & 0xff, riva128);
  riva128_pci_write(0, (addr & 0xfc) + 3, (val >> 24) & 0xff, riva128);
  break;
  }
}

static uint8_t riva128_pfifo_read(uint32_t addr, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint8_t ret = 0;

  pclog("RIVA 128 PFIFO read %08X %04X:%08X\n", addr, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x002100: ret = riva128->pfifo.intr & 0xff; break;
  case 0x002101: ret = (riva128->pfifo.intr >> 8) & 0xff; break;
  case 0x002102: ret = (riva128->pfifo.intr >> 16) & 0xff; break;
  case 0x002103: ret = (riva128->pfifo.intr >> 24) & 0xff; break;
  case 0x002140: ret = riva128->pfifo.intr_en & 0xff; break;
  case 0x002141: ret = (riva128->pfifo.intr_en >> 8) & 0xff; break;
  case 0x002142: ret = (riva128->pfifo.intr_en >> 16) & 0xff; break;
  case 0x002143: ret = (riva128->pfifo.intr_en >> 24) & 0xff; break;
  case 0x002210: ret = riva128->pfifo.ramht & 0xff; break;
  case 0x002211: ret = (riva128->pfifo.ramht >> 8) & 0xff; break;
  case 0x002212: ret = (riva128->pfifo.ramht >> 16) & 0xff; break;
  case 0x002213: ret = (riva128->pfifo.ramht >> 24) & 0xff; break;
  case 0x002214: ret = riva128->pfifo.ramfc & 0xff; break;
  case 0x002215: ret = (riva128->pfifo.ramfc >> 8) & 0xff; break;
  case 0x002216: ret = (riva128->pfifo.ramfc >> 16) & 0xff; break;
  case 0x002217: ret = (riva128->pfifo.ramfc >> 24) & 0xff; break;
  case 0x002218: ret = riva128->pfifo.ramro & 0xff; break;
  case 0x002219: ret = (riva128->pfifo.ramro >> 8) & 0xff; break;
  case 0x00221a: ret = (riva128->pfifo.ramro >> 16) & 0xff; break;
  case 0x00221b: ret = (riva128->pfifo.ramro >> 24) & 0xff; break;
  case 0x002504: ret = riva128->pfifo.chan_mode & 0xff; break;
  case 0x002505: ret = (riva128->pfifo.chan_mode >> 8) & 0xff; break;
  case 0x002506: ret = (riva128->pfifo.chan_mode >> 16) & 0xff; break;
  case 0x002507: ret = (riva128->pfifo.chan_mode >> 24) & 0xff; break;
  case 0x002508: ret = riva128->pfifo.chan_dma & 0xff; break;
  case 0x002509: ret = (riva128->pfifo.chan_dma >> 8) & 0xff; break;
  case 0x00250a: ret = (riva128->pfifo.chan_dma >> 16) & 0xff; break;
  case 0x00250b: ret = (riva128->pfifo.chan_dma >> 24) & 0xff; break;
  case 0x00250c: ret = riva128->pfifo.chan_size & 0xff; break;
  case 0x00250d: ret = (riva128->pfifo.chan_size >> 8) & 0xff; break;
  case 0x00250e: ret = (riva128->pfifo.chan_size >> 16) & 0xff; break;
  case 0x00250f: ret = (riva128->pfifo.chan_size >> 24) & 0xff; break;
  //HACK
  case 0x002400: ret = 0x10; break;
  case 0x002401: ret = 0x00; break;
  case 0x003204: ret = riva128->pfifo.caches[1].chanid; break;
  case 0x003214: ret = 0x10; break;
  case 0x003215: ret = 0x00; break;
  case 0x003220: ret = 0x01; break;
  }

  return ret;
}

static void riva128_pfifo_write(uint32_t addr, uint32_t val, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  pclog("RIVA 128 PFIFO write %08X %08X %04X:%08X\n", addr, val, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x002100:
  riva128->pfifo.intr &= ~val;
  break;
  case 0x002140:
  riva128->pfifo.intr_en = val;
  break;
  case 0x002210:
  riva128->pfifo.ramht = val;
  riva128->pfifo.ramht_addr = (val & 0x1f0) << 8;
  switch(val & 0x30000)
  {
	case 0x00000:
	riva128->pfifo.ramht_size = 4 * 1024;
	break;
	case 0x10000:
	riva128->pfifo.ramht_size = 8 * 1024;
	break;
	case 0x20000:
	riva128->pfifo.ramht_size = 16 * 1024;
	break;
	case 0x30000:
	riva128->pfifo.ramht_size = 32 * 1024;
	break;
  }
  break;
  case 0x002214:
  riva128->pfifo.ramfc = val;
  riva128->pfifo.ramfc_addr = (val & 0x1fe) << 4;
  break;
  case 0x002218:
  riva128->pfifo.ramro = val;
  riva128->pfifo.ramro_addr = (val & 0x1fe) << 4;
  if(val & 0x10000) riva128->pfifo.ramro_size = 8192;
  else riva128->pfifo.ramro_size = 512;
  break;
  case 0x002504:
  riva128->pfifo.chan_mode = val;
  break;
  case 0x002508:
  riva128->pfifo.chan_dma = val;
  break;
  case 0x00250c:
  riva128->pfifo.chan_size = val;
  break;
  case 0x003200:
  riva128->pfifo.caches[1].push_enabled = val;
  break;
  case 0x003204:
  riva128->pfifo.caches[1].chanid = val;
  break;
  }
}

static void riva128_pfifo_interrupt(int num, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;

  riva128->pfifo.intr &= ~(1 << num);

  riva128_pmc_interrupt(8, riva128);
}

static uint8_t riva128_ptimer_read(uint32_t addr, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint8_t ret = 0;

  //pclog("RIVA 128 PTIMER read %08X %04X:%08X\n", addr, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x009400: ret = riva128->ptimer.time & 0xff; break;
  case 0x009401: ret = (riva128->ptimer.time >> 8) & 0xff; break;
  case 0x009402: ret = (riva128->ptimer.time >> 16) & 0xff; break;
  case 0x009403: ret = (riva128->ptimer.time >> 24) & 0xff; break;
  }

  //TODO: gross hack to make NT4 happy for the time being.
  riva128->ptimer.time += 0x10000;
  
  return ret;
}

static uint8_t riva128_pfb_read(uint32_t addr, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint8_t ret = 0;

  //pclog("RIVA 128 PFB read %08X %04X:%08X\n", addr, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x100000:
  {
    switch(riva128->memory_size)
    {
    case 2: ret = 1;
    case 3: ret = 2;
    }
    ret |= 0x04;
    break;
  }
  case 0x100200: ret = riva128->pfb.config_0 & 0xff; break;
  case 0x100201: ret = (riva128->pfb.config_0 >> 8) & 0xff; break;
  case 0x100202: ret = (riva128->pfb.config_0 >> 16) & 0xff; break;
  case 0x100203: ret = (riva128->pfb.config_0 >> 24) & 0xff; break;
  }

  return ret;
}

static void riva128_pfb_write(uint32_t addr, uint32_t val, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  //pclog("RIVA 128 PFB write %08X %08X %04X:%08X\n", addr, val, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x100200:
  riva128->pfb.config_0 = val;
  riva128->pfb.width = (val & 0x3f) << 5;
  switch((val >> 8) & 3)
  {
  case 1: riva128->pfb.bpp = 8; break;
  case 2: riva128->pfb.bpp = 16; break;
  case 3: riva128->pfb.bpp = 32; break;
  }
  break;
  }
}

static uint8_t riva128_pextdev_read(uint32_t addr, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint8_t ret = 0;

  //pclog("RIVA 128 PEXTDEV read %08X %04X:%08X\n", addr, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x101000: ret = 0x9e; break;
  case 0x101001: ret = 0x01; break;
  }

  return ret;
}

static uint8_t riva128_pgraph_read(uint32_t addr, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint8_t ret = 0;

  pclog("RIVA 128 PGRAPH read %08X %04X:%08X\n", addr, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x400100: ret = riva128->pgraph.intr & 0xff; break;
  case 0x400101: ret = (riva128->pgraph.intr >> 8) & 0xff; break;
  case 0x400102: ret = (riva128->pgraph.intr >> 16) & 0xff; break;
  case 0x400103: ret = (riva128->pgraph.intr >> 24) & 0xff; break;
  case 0x400104: ret = riva128->pgraph.invalid & 0xff; break;
  case 0x400105: ret = (riva128->pgraph.invalid >> 8) & 0xff; break;
  case 0x400106: ret = (riva128->pgraph.invalid >> 16) & 0xff; break;
  case 0x400107: ret = (riva128->pgraph.invalid >> 24) & 0xff; break;
  case 0x400140: ret = riva128->pgraph.intr_en & 0xff; break;
  case 0x400141: ret = (riva128->pgraph.intr_en >> 8) & 0xff; break;
  case 0x400142: ret = (riva128->pgraph.intr_en >> 16) & 0xff; break;
  case 0x400143: ret = (riva128->pgraph.intr_en >> 24) & 0xff; break;
  case 0x400144: ret = riva128->pgraph.invalid_en & 0xff; break;
  case 0x400145: ret = (riva128->pgraph.invalid_en >> 8) & 0xff; break;
  case 0x400146: ret = (riva128->pgraph.invalid_en >> 16) & 0xff; break;
  case 0x400147: ret = (riva128->pgraph.invalid_en >> 24) & 0xff; break;

  case 0x40053c: ret = riva128->pgraph.uclip_xmin & 0xff; break;
  case 0x40053d: ret = (riva128->pgraph.uclip_xmin >> 8) & 0xff; break;
  case 0x40053e: ret = (riva128->pgraph.uclip_xmin >> 16) & 0xff; break;
  case 0x40053f: ret = (riva128->pgraph.uclip_xmin >> 24) & 0xff; break;
  case 0x400540: ret = riva128->pgraph.uclip_ymin & 0xff; break;
  case 0x400541: ret = (riva128->pgraph.uclip_ymin >> 8) & 0xff; break;
  case 0x400542: ret = (riva128->pgraph.uclip_ymin >> 16) & 0xff; break;
  case 0x400543: ret = (riva128->pgraph.uclip_ymin >> 24) & 0xff; break;
  case 0x400544: ret = riva128->pgraph.uclip_xmax & 0xff; break;
  case 0x400545: ret = (riva128->pgraph.uclip_xmax >> 8) & 0xff; break;
  case 0x400546: ret = (riva128->pgraph.uclip_xmax >> 16) & 0xff; break;
  case 0x400547: ret = (riva128->pgraph.uclip_xmax >> 24) & 0xff; break;
  case 0x400548: ret = riva128->pgraph.uclip_ymax & 0xff; break;
  case 0x400549: ret = (riva128->pgraph.uclip_ymax >> 8) & 0xff; break;
  case 0x40054a: ret = (riva128->pgraph.uclip_ymax >> 16) & 0xff; break;
  case 0x40054b: ret = (riva128->pgraph.uclip_ymax >> 24) & 0xff; break;
  case 0x400560: ret = riva128->pgraph.oclip_xmin & 0xff; break;
  case 0x400561: ret = (riva128->pgraph.oclip_xmin >> 8) & 0xff; break;
  case 0x400562: ret = (riva128->pgraph.oclip_xmin >> 16) & 0xff; break;
  case 0x400563: ret = (riva128->pgraph.oclip_xmin >> 24) & 0xff; break;
  case 0x400564: ret = riva128->pgraph.oclip_ymin & 0xff; break;
  case 0x400565: ret = (riva128->pgraph.oclip_ymin >> 8) & 0xff; break;
  case 0x400566: ret = (riva128->pgraph.oclip_ymin >> 16) & 0xff; break;
  case 0x400567: ret = (riva128->pgraph.oclip_ymin >> 24) & 0xff; break;
  case 0x400568: ret = riva128->pgraph.oclip_xmax & 0xff; break;
  case 0x400569: ret = (riva128->pgraph.oclip_xmax >> 8) & 0xff; break;
  case 0x40056a: ret = (riva128->pgraph.oclip_xmax >> 16) & 0xff; break;
  case 0x40056b: ret = (riva128->pgraph.oclip_xmax >> 24) & 0xff; break;
  case 0x40056c: ret = riva128->pgraph.oclip_ymax & 0xff; break;
  case 0x40056d: ret = (riva128->pgraph.oclip_ymax >> 8) & 0xff; break;
  case 0x40056e: ret = (riva128->pgraph.oclip_ymax >> 16) & 0xff; break;
  case 0x40056f: ret = (riva128->pgraph.oclip_ymax >> 24) & 0xff; break;
  case 0x400640: ret = riva128->pgraph.beta & 0xff; break;
  case 0x400641: ret = (riva128->pgraph.beta >> 8) & 0xff; break;
  case 0x400642: ret = (riva128->pgraph.beta >> 16) & 0xff; break;
  case 0x400643: ret = (riva128->pgraph.beta >> 24) & 0xff; break;
  }

  return ret;
}

static void riva128_pgraph_write(uint32_t addr, uint32_t val, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  pclog("RIVA 128 PGRAPH write %08X %08X %04X:%08X\n", addr, val, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x400100:
  riva128->pgraph.intr &= ~val;
  break;
  case 0x400104:
  riva128->pgraph.invalid &= ~val;
  break;
  case 0x400140:
  riva128->pgraph.intr_en = val;
  break;
  case 0x400144:
  riva128->pgraph.invalid_en = val;
  break;
  case 0x400180:
  riva128->pgraph.debug[1] &= ~1; //Clear recent volatile reset bit on object switch.
  break;
  case 0x40053c:
  riva128->pgraph.uclip_xmin = val & 0x3ffff;
  break;
  case 0x400540:
  riva128->pgraph.uclip_ymin = val & 0x3ffff;
  break;
  case 0x400544:
  riva128->pgraph.uclip_xmax = val & 0x3ffff;
  break;
  case 0x400548:
  riva128->pgraph.uclip_ymax = val & 0x3ffff;
  break;
  case 0x400560:
  riva128->pgraph.oclip_xmin = val & 0x3ffff;
  break;
  case 0x400564:
  riva128->pgraph.oclip_ymin = val & 0x3ffff;
  break;
  case 0x400568:
  riva128->pgraph.oclip_xmax = val & 0x3ffff;
  break;
  case 0x40056c:
  riva128->pgraph.oclip_ymax = val & 0x3ffff;
  break;
  case 0x400640:
  {
    uint32_t tmp = val & 0x7f800000;
    if(val & 0x80000000) tmp = 0;
    riva128->pgraph.beta = tmp;
    break;
  }
  }
}

static uint8_t riva128_pramdac_read(uint32_t addr, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint8_t ret = 0;

  //pclog("RIVA 128 PRAMDAC read %08X %04X:%08X\n", addr, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x680500: ret = riva128->pramdac.nvpll & 0xff; break;
  case 0x680501: ret = (riva128->pramdac.nvpll >> 8) & 0xff; break;
  case 0x680502: ret = (riva128->pramdac.nvpll >> 16) & 0xff; break;
  case 0x680503: ret = (riva128->pramdac.nvpll >> 24) & 0xff; break;
  case 0x680504: ret = riva128->pramdac.mpll & 0xff; break;
  case 0x680505: ret = (riva128->pramdac.mpll >> 8) & 0xff; break;
  case 0x680506: ret = (riva128->pramdac.mpll >> 16) & 0xff; break;
  case 0x680507: ret = (riva128->pramdac.mpll >> 24) & 0xff; break;
  case 0x680508: ret = riva128->pramdac.vpll & 0xff; break;
  case 0x680509: ret = (riva128->pramdac.vpll >> 8) & 0xff; break;
  case 0x68050a: ret = (riva128->pramdac.vpll >> 16) & 0xff; break;
  case 0x68050b: ret = (riva128->pramdac.vpll >> 24) & 0xff; break;
  case 0x68050c: ret = riva128->pramdac.pll_ctrl & 0xff; break;
  case 0x68050d: ret = (riva128->pramdac.pll_ctrl >> 8) & 0xff; break;
  case 0x68050e: ret = (riva128->pramdac.pll_ctrl >> 16) & 0xff; break;
  case 0x68050f: ret = (riva128->pramdac.pll_ctrl >> 24) & 0xff; break;
  case 0x680600: ret = riva128->pramdac.gen_ctrl & 0xff; break;
  case 0x680601: ret = (riva128->pramdac.gen_ctrl >> 8) & 0xff; break;
  case 0x680602: ret = (riva128->pramdac.gen_ctrl >> 16) & 0xff; break;
  case 0x680603: ret = (riva128->pramdac.gen_ctrl >> 24) & 0xff; break;
  }

  return ret;
}

static void riva128_pramdac_write(uint32_t addr, uint32_t val, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  //pclog("RIVA 128 PRAMDAC write %08X %08X %04X:%08X\n", addr, val, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x680500:
  riva128->pramdac.nvpll = val;
  riva128->pramdac.nv_m = val & 0xff;
  riva128->pramdac.nv_n = (val >> 8) & 0xff;
  riva128->pramdac.nv_p = (val >> 16) & 7;
  break;
  case 0x680504:
  riva128->pramdac.mpll = val;
  riva128->pramdac.m_m = val & 0xff;
  riva128->pramdac.m_n = (val >> 8) & 0xff;
  riva128->pramdac.m_p = (val >> 16) & 7;
  break;
  case 0x680508:
  riva128->pramdac.vpll = val;
  riva128->pramdac.v_m = val & 0xff;
  riva128->pramdac.v_n = (val >> 8) & 0xff;
  riva128->pramdac.v_p = (val >> 16) & 7;
  svga_recalctimings(svga);
  break;
  case 0x68050c:
  riva128->pramdac.pll_ctrl = val;
  break;
  case 0x680600:
  riva128->pramdac.gen_ctrl = val;
  break;
  }
}

static uint8_t riva128_ramht_lookup(uint32_t handle, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  pclog("RIVA 128 RAMHT lookup with handle %08X %04X:%08X\n", handle, CS, cpu_state.pc);
  
  uint8_t objclass;
  
  uint32_t ramht_base = riva128->pfifo.ramht_addr;
  
  uint32_t tmp = handle;
  uint32_t hash = 0;
  
  int bits;
  
  switch(riva128->pfifo.ramht_size)
  {
	case 4096: bits = 12;
	case 8192: bits = 13;
	case 16384: bits = 14;
	case 32768: bits = 15;
  }
  
  while(handle)
  {
	hash ^= (tmp & (riva128->pfifo.ramht_size - 1));
	tmp = handle >> 1;
  }
  
  hash ^= riva128->pfifo.caches[1].chanid << (bits - 4);
  
  objclass = riva128->pramin[ramht_base + (hash * 8)];
  objclass &= 0xff;
  return objclass;
}

static void riva128_pgraph_exec_method(int subchanid, int offset, uint32_t val, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  pclog("RIVA 128 PGRAPH executing method %04X with object class on subchannel %01X %04X:%08X\n", offset, riva128->pgraph.obj_class[subchanid], subchanid, val, CS, cpu_state.pc);

  switch(riva128->pgraph.obj_class[subchanid])
  {
    case 0x30:
    //NV1_NULL
    return;
  }
}

static void riva128_puller_exec_method(int chanid, int subchanid, int offset, uint32_t val, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  pclog("RIVA 128 Puller executing method %04X on channel %01X[%01X] %04X:%08X\n", offset, chanid, subchanid, val, CS, cpu_state.pc);
  
  if(offset < 0x100)
  {
	//These methods are executed by the puller itself.
	if(offset == 0)
	{
		riva128->pgraph.obj_handle[subchanid] = val;
		riva128->pgraph.obj_class[subchanid] = riva128_ramht_lookup(val, riva128);
	}
  }
  else
  {
	riva128_pgraph_exec_method(subchanid, offset, val, riva128);
  }
}

static void riva128_pusher_run(int chanid, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;

  while(riva128->pfifo.channels[chanid].dmaget != riva128->pfifo.channels[chanid].dmaput)
  {
    uint32_t dmaget = riva128->pfifo.channels[chanid].dmaget;
    uint32_t cmd = ((uint32_t*)svga->vram)[dmaget >> 2];
    uint32_t* params = ((uint32_t*)svga->vram)[(dmaget + 4) >> 2];
    if((cmd & 0xe0030003) == 0)
    {
      //nv3 increasing method command
      uint32_t method = cmd & 0x1ffc;
      int subchannel = (cmd >> 13) & 7;
      int method_count = (cmd >> 18) & 0x7ff;
      for(int i = 0;i<=method_count;i++)
      {
        riva128_puller_exec_method(chanid, subchannel, method, params[i<<2], riva128);
        method+=4;
      }
    }
    else
    {
      pclog("RIVA 128 PFIFO Invalid DMA pusher command %08x!\n", cmd);
      riva128_pfifo_interrupt(12, riva128);
    }
    riva128->pfifo.channels[chanid].dmaget += 4;
  }
}

static void riva128_user_write(uint32_t addr, uint32_t val, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  pclog("RIVA 128 USER write %08X %08X %04X:%08X\n", addr, val, CS, cpu_state.pc);
  
  addr -= 0x800000;
  
  int chanid = (addr >> 16) & 0xf;
  int subchanid = (addr >> 13) & 0x7;
  int offset = addr & 0x1fff;
  
  if(riva128->pfifo.chan_mode & (1 << chanid))
  {
	//DMA mode, at least this has docs.
	switch(offset)
	{
	case 0x40:
	riva128->pfifo.channels[chanid].dmaput = val;
	if(riva128->pfifo.caches[1].push_enabled) riva128_pusher_run(chanid, riva128);
	break;
	case 0x44:
	riva128->pfifo.channels[chanid].dmaget = val;
	break;
	}
  }
  else
  {
	//I don't know what to do here, as there are basically no docs on PIO PFIFO submission.
	pclog("RIVA 128 PIO PFIFO submission attempted\n");
  }
}

static uint8_t riva128_mmio_read(uint32_t addr, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint8_t ret = 0;

  addr &= 0xffffff;

  pclog("RIVA 128 MMIO read %08X %04X:%08X\n", addr, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x000000 ... 0x000fff:
  ret = riva128_pmc_read(addr, riva128);
  break;
  case 0x001000 ... 0x001fff:
  ret = riva128_pbus_read(addr, riva128);
  break;
  case 0x002000 ... 0x002fff:
  ret = riva128_pfifo_read(addr, riva128);
  break;
  case 0x009000 ... 0x009fff:
  ret = riva128_ptimer_read(addr, riva128);
  break;
  case 0x100000 ... 0x100fff:
  ret = riva128_pfb_read(addr, riva128);
  break;
  case 0x101000 ... 0x101fff:
  ret = riva128_pextdev_read(addr, riva128);
  break;
  case 0x6013b4 ... 0x6013b5: case 0x6013d4 ... 0x6013d5: case 0x0c03c2 ... 0x0c03c5: case 0x0c03cc ... 0x0c03cf:
  ret = riva128_in(addr & 0xfff, riva128);
  break;
  case 0x680000 ... 0x680fff:
  ret = riva128_pramdac_read(addr, riva128);
  break;
  }
  return ret;
}

static uint16_t riva128_mmio_read_w(uint32_t addr, void *p)
{
  addr &= 0xffffff;
  //pclog("RIVA 128 MMIO read %08X %04X:%08X\n", addr, CS, cpu_state.pc);
  return (riva128_mmio_read(addr+0,p) << 0) | (riva128_mmio_read(addr+1,p) << 8);
}

static uint32_t riva128_mmio_read_l(uint32_t addr, void *p)
{
  addr &= 0xffffff;
  //pclog("RIVA 128 MMIO read %08X %04X:%08X\n", addr, CS, cpu_state.pc);
  return (riva128_mmio_read(addr+0,p) << 0) | (riva128_mmio_read(addr+1,p) << 8) | (riva128_mmio_read(addr+2,p) << 16) | (riva128_mmio_read(addr+3,p) << 24);
}

static void riva128_mmio_write(uint32_t addr, uint8_t val, void *p)
{
  addr &= 0xffffff;
  //pclog("RIVA 128 MMIO write %08X %02X %04X:%08X\n", addr, val, CS, cpu_state.pc);
  if(addr != 0x6013d4 && addr != 0x6013d5 && addr != 0x6013b4 && addr != 0x6013b5)
  {
    uint32_t tmp = riva128_mmio_read_l(addr,p);
    tmp &= ~(0xff << ((addr & 3) << 3));
    tmp |= val << ((addr & 3) << 3);
    riva128_mmio_write_l(addr, tmp, p);
  }
  else
  {
    riva128_out(addr & 0xfff, val & 0xff, p);
  }
}

static void riva128_mmio_write_w(uint32_t addr, uint16_t val, void *p)
{
  addr &= 0xffffff;
  //pclog("RIVA 128 MMIO write %08X %04X %04X:%08X\n", addr, val, CS, cpu_state.pc);
  uint32_t tmp = riva128_mmio_read_l(addr,p);
  tmp &= ~(0xffff << ((addr & 2) << 4));
  tmp |= val << ((addr & 2) << 4);
  riva128_mmio_write_l(addr, tmp, p);
}

static void riva128_mmio_write_l(uint32_t addr, uint32_t val, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;

  addr &= 0xffffff;

  pclog("RIVA 128 MMIO write %08X %08X %04X:%08X\n", addr, val, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x000000 ... 0x000fff:
  riva128_pmc_write(addr, val, riva128);
  break;
  case 0x001000 ... 0x001fff:
  riva128_pbus_write(addr, val, riva128);
  break;
  case 0x002000 ... 0x002fff:
  riva128_pfifo_write(addr, val, riva128);
  break;
  case 0x100000 ... 0x100fff:
  riva128_pfb_write(addr, val, riva128);
  break;
  case 0x680000 ... 0x680fff:
  riva128_pramdac_write(addr, val, riva128);
  break;
  case 0x800000 ... 0xffffff:
  riva128_user_write(addr, val, riva128);
  break;
  }
}

static void riva128_poll(void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	svga_t *svga = &riva128->svga;
}

static uint8_t riva128_rma_in(uint16_t addr, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint8_t ret = 0;

  addr &= 0xff;

  //pclog("RIVA 128 RMA read %04X %04X:%08X\n", addr, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x00: ret = 0x65; break;
  case 0x01: ret = 0xd0; break;
  case 0x02: ret = 0x16; break;
  case 0x03: ret = 0x2b; break;
  case 0x08: case 0x09: case 0x0a: case 0x0b: ret = riva128_mmio_read(riva128->rma.addr + (addr & 3), riva128); break;
  }

  return ret;
}

static void riva128_rma_out(uint16_t addr, uint8_t val, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;

  addr &= 0xff;

  //pclog("RIVA 128 RMA write %04X %02X %04X:%08X\n", addr, val, CS, cpu_state.pc);

  switch(addr)
  {
  case 0x04:
  riva128->rma.addr &= ~0xff;
  riva128->rma.addr |= val;
  break;
  case 0x05:
  riva128->rma.addr &= ~0xff00;
  riva128->rma.addr |= (val << 8);
  break;
  case 0x06:
  riva128->rma.addr &= ~0xff0000;
  riva128->rma.addr |= (val << 16);
  break;
  case 0x07:
  riva128->rma.addr &= ~0xff000000;
  riva128->rma.addr |= (val << 24);
  break;
  case 0x08: case 0x0c: case 0x10: case 0x14:
  riva128->rma.data &= ~0xff;
  riva128->rma.data |= val;
  break;
  case 0x09: case 0x0d: case 0x11: case 0x15:
  riva128->rma.data &= ~0xff00;
  riva128->rma.data |= (val << 8);
  break;
  case 0x0a: case 0x0e: case 0x12: case 0x16:
  riva128->rma.data &= ~0xff0000;
  riva128->rma.data |= (val << 16);
  break;
  case 0x0b: case 0x0f: case 0x13: case 0x17:
  riva128->rma.data &= ~0xff000000;
  riva128->rma.data |= (val << 24);
  if(riva128->rma.addr < 0x1000000) riva128_mmio_write_l(riva128->rma.addr & 0xffffff, riva128->rma.data, riva128);
  else svga_writel_linear((riva128->rma.addr - 0x1000000), riva128->rma.data, svga);
  break;
  }

  if(addr & 0x10) riva128->rma.addr+=4;
}

static uint8_t riva128_in(uint16_t addr, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint8_t ret = 0;

  switch (addr)
  {
  case 0x3D0 ... 0x3D3:
  //pclog("RIVA 128 RMA BAR Register read %04X %04X:%08X\n", addr, CS, cpu_state.pc);
  if(!(riva128->rma.mode & 1)) return ret;
  ret = riva128_rma_in(riva128->rma_addr + ((riva128->rma.mode & 0xe) << 1) + (addr & 3), riva128);
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
  ret = (riva128->i2c.sda << 3) | (riva128->i2c.scl << 2);
  break;
  default:
  ret = svga->crtc[svga->crtcreg];
  break;
  }
  //if(svga->crtcreg > 0x18)
  //  pclog("RIVA 128 Extended CRTC read %02X %04X:%08X\n", svga->crtcreg, CS, cpu_state.pc);
  break;
  default:
  ret = svga_in(addr, svga);
  break;
  }
  //        if (addr != 0x3da) pclog("%02X\n", ret);
  return ret;
}

static void riva128_out(uint16_t addr, uint8_t val, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;

  uint8_t old;
  
  switch(addr)
  {
  case 0x3D0 ... 0x3D3:
  //pclog("RIVA 128 RMA BAR Register write %04X %02x %04X:%08X\n", addr, val, CS, cpu_state.pc);
  riva128->rma.access_reg[addr & 3] = val;
  if(!(riva128->rma.mode & 1)) return;
  riva128_rma_out(riva128->rma_addr + ((riva128->rma.mode & 0xe) << 1) + (addr & 3), riva128->rma.access_reg[addr & 3], riva128);
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
  riva128->read_bank = val;
  if (svga->chain4) svga->read_bank = riva128->read_bank << 15;
  else              svga->read_bank = riva128->read_bank << 13;
  break;
  case 0x1d:
  riva128->write_bank = val;
  if (svga->chain4) svga->write_bank = riva128->write_bank << 15;
  else              svga->write_bank = riva128->write_bank << 13;
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
  riva128->rma.mode = val & 0xf;
  break;
  case 0x3f:
  riva128->i2c.scl = (val & 0x20) ? 1 : 0;
  riva128->i2c.sda = (val & 0x10) ? 1 : 0;
  break;
  }
  //if(svga->crtcreg > 0x18)
  //  pclog("RIVA 128 Extended CRTC write %02X %02x %04X:%08X\n", svga->crtcreg, val, CS, cpu_state.pc);
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

static uint32_t riva128_ramin_readl(uint32_t addr, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint32_t ret = riva128->pramin[(addr & 0x1ffffc) >> 2];
  return ret;
}

static uint8_t riva128_ramin_readb(uint32_t addr, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint32_t ret = riva128->pramin[(addr & 0x1ffffc) >> 2];
  ret >>= 24 - ((addr & 3) << 3);
  return ret;
}

static uint16_t riva128_ramin_readw(uint32_t addr, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint32_t ret = riva128->pramin[(addr & 0x1ffffc) >> 2];
  ret >>= 16 - ((addr & 2) << 3);
  return ret;
}

static void riva128_ramin_writel(uint32_t addr, uint32_t val, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  riva128->pramin[(addr & 0x1ffffc) >> 2] = val;
}

static void riva128_ramin_writeb(uint32_t addr, uint8_t val, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint32_t tmp = riva128_ramin_readl(addr,p);
  tmp &= ~(0xff << ((addr & 3) << 3));
  tmp |= val << ((addr & 3) << 3);
  riva128_ramin_writel(addr, tmp, p);
}

static void riva128_ramin_writew(uint32_t addr, uint16_t val, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint32_t tmp = riva128_ramin_readl(addr,p);
  tmp &= ~(0xffff << ((addr & 2) << 4));
  tmp |= val << ((addr & 2) << 4);
  riva128_ramin_writel(addr, tmp, p);
}

static uint8_t riva128_pci_read(int func, int addr, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint8_t ret = 0;
  //pclog("RIVA 128 PCI read %02X %04X:%08X\n", addr, CS, cpu_state.pc);
  switch (addr)
  {
    case 0x00: ret = 0xd2; break; /*'nVidia'*/
    case 0x01: ret = 0x12; break;

    case 0x02: ret = 0x18; break; /*'RIVA 128'*/
    case 0x03: ret = 0x00; break;

    case 0x04: ret = riva128->pci_regs[0x04] & 0x37; break;
    case 0x05: ret = riva128->pci_regs[0x05] & 0x01; break;

    case 0x06: ret = 0x20; break;
    case 0x07: ret = riva128->pci_regs[0x07] & 0x73; break;

    case 0x08: ret = 0x01; break; /*Revision ID*/
    case 0x09: ret = 0; break; /*Programming interface*/

    case 0x0a: ret = 0x00; break; /*Supports VGA interface*/
    case 0x0b: ret = 0x03; /*output = 3; */break;

    case 0x0e: ret = 0x00; break; /*Header type*/

    case 0x13:
    case 0x17:
    ret = riva128->pci_regs[addr];
    break;

    case 0x2c: case 0x2d: case 0x2e: case 0x2f:
    ret = riva128->pci_regs[addr];
    //if(CS == 0x0028) output = 3;
    break;

    case 0x30: return riva128->pci_regs[0x30] & 0x01; /*BIOS ROM address*/
    case 0x31: return 0x00;
    case 0x32: return riva128->pci_regs[0x32];
    case 0x33: return riva128->pci_regs[0x33];

    case 0x34: ret = 0x00; break;

    case 0x3c: ret = riva128->pci_regs[0x3c]; break;

    case 0x3d: ret = 0x01; break; /*INTA*/

    case 0x3e: ret = 0x03; break;
    case 0x3f: ret = 0x01; break;

  }
  //        pclog("%02X\n", ret);
  return ret;
}

static void riva128_reenable_svga_mappings(svga_t *svga)
{
        switch (svga->gdcreg[6] & 0xc) /*Banked framebuffer*/
        {
                case 0x0: /*128k at A0000*/
                mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x20000);
                svga->banked_mask = 0xffff;
                break;
                case 0x4: /*64k at A0000*/
                mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
                svga->banked_mask = 0xffff;
                break;
                case 0x8: /*32k at B0000*/
                mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
                svga->banked_mask = 0x7fff;
                break;
                case 0xC: /*32k at B8000*/
                mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);
                svga->banked_mask = 0x7fff;
                break;
        }
}

static void riva128_pci_write(int func, int addr, uint8_t val, void *p)
{
  //pclog("RIVA 128 PCI write %02X %02X %04X:%08X\n", addr, val, CS, cpu_state.pc);
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  switch (addr)
  {
    case 0x00: case 0x01: case 0x02: case 0x03:
    case 0x08: case 0x09: case 0x0a: case 0x0b:
    case 0x3d: case 0x3e: case 0x3f:
    return;

    case PCI_REG_COMMAND:
    riva128->pci_regs[PCI_REG_COMMAND] = val & 0x27;
    if (val & PCI_COMMAND_IO)
    {
      io_sethandler(0x03c0, 0x0020, riva128_in, NULL, NULL, riva128_out, NULL, NULL, riva128);
      uint32_t mmio_addr = riva128->pci_regs[0x13] << 24;
      uint32_t linear_addr = riva128->pci_regs[0x17] << 24;
      if (!mmio_addr && !linear_addr)
      {
	riva128_reenable_svga_mappings(svga);
      }
      if (mmio_addr)
      {
      	mem_mapping_set_addr(&riva128->mmio_mapping, mmio_addr, 0x1000000);
      }
      if (linear_addr)
      {
      	mem_mapping_set_addr(&riva128->linear_mapping, linear_addr, 0xc00000);
        mem_mapping_set_addr(&riva128->ramin_mapping, linear_addr + 0xc00000, 0x200000);
        svga->linear_base = linear_addr;
      }
    }
    else
    {
      io_removehandler(0x03c0, 0x0020, riva128_in, NULL, NULL, riva128_out, NULL, NULL, riva128);
      mem_mapping_disable(&svga->mapping);
      mem_mapping_disable(&riva128->mmio_mapping);
      mem_mapping_disable(&riva128->linear_mapping);
      mem_mapping_disable(&riva128->ramin_mapping);
    }
    return;

    case 0x05:
    riva128->pci_regs[0x05] = val & 0x01;
    return;

    case 0x07:
    riva128->pci_regs[0x07] = (riva128->pci_regs[0x07] & 0x8f) | (val & 0x70);
    return;

    case 0x13:
    {
      riva128->pci_regs[addr] = val;
      uint32_t mmio_addr = riva128->pci_regs[0x13] << 24;
      if (mmio_addr)
      {
      	mem_mapping_set_addr(&riva128->mmio_mapping, mmio_addr, 0x1000000);
      }
      else
      {
      	mem_mapping_disable(&riva128->mmio_mapping);
      }
      return;
    }

    case 0x17:
    {
      riva128->pci_regs[addr] = val;
      uint32_t linear_addr = riva128->pci_regs[0x17] << 24;
      if (linear_addr)
      {
      	mem_mapping_set_addr(&riva128->linear_mapping, linear_addr, 0xc00000);
        mem_mapping_set_addr(&riva128->ramin_mapping, linear_addr + 0xc00000, 0x200000);
        svga->linear_base = linear_addr;
      }
      else
      {
      	mem_mapping_disable(&riva128->linear_mapping);
        mem_mapping_disable(&riva128->ramin_mapping);
      }
      return;
    }

    case 0x30: case 0x32: case 0x33:
    riva128->pci_regs[addr] = val;
    if (riva128->pci_regs[0x30] & 0x01)
    {
      uint32_t addr = (riva128->pci_regs[0x32] << 16) | (riva128->pci_regs[0x33] << 24);
      //                        pclog("RIVA 128 bios_rom enabled at %08x\n", addr);
      mem_mapping_set_addr(&riva128->bios_rom.mapping, addr, 0x8000);
    }
    else
    {
      //                        pclog("RIVA 128 bios_rom disabled\n");
      mem_mapping_disable(&riva128->bios_rom.mapping);
    }
    return;

    case 0x3c:
    riva128->pci_regs[0x3c] = val & 0x0f;
    return;

    case 0x40: case 0x41: case 0x42: case 0x43:
    riva128->pci_regs[addr - 0x14] = val; //0x40-0x43 are ways to write to 0x2c-0x2f
    return;
  }
}

static void riva128_recalctimings(svga_t *svga)
{
  riva128_t *riva128 = (riva128_t *)svga->p;

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

  /*if((svga->crtc[0x28] & 3) != 0)
  {
    if(svga->crtc[0x1a] & 2) svga_set_ramdac_type(svga, RAMDAC_6BIT);
    else svga_set_ramdac_type(svga, RAMDAC_8BIT);
  }
  else svga_set_ramdac_type(svga, RAMDAC_6BIT);*/
  
  if (((svga->miscout >> 2) & 2) == 2)
  {
	double freq = 0;
	
	//if(riva128->pextdev.boot0 & 0x40) freq = 14318180.0;
	freq = 13500000.0;

	if(riva128->pramdac.v_m == 0) freq = 0;
	else
	{
		freq = (freq * riva128->pramdac.v_n) / (1 << riva128->pramdac.v_p) / riva128->pramdac.v_m;
		//pclog("RIVA 128 Pixel clock is %f Hz\n", freq);
	}
	
        svga->clock = cpuclock / freq;
  }
}

static void *riva128_init()
{
  riva128_t *riva128 = malloc(sizeof(riva128_t));
  memset(riva128, 0, sizeof(riva128_t));

  riva128->memory_size = device_get_config_int("memory");

  svga_init(&riva128->svga, riva128, riva128->memory_size << 20,
  riva128_recalctimings,
  riva128_in, riva128_out,
  NULL, NULL);

  rom_init(&riva128->bios_rom, "roms/Diamond_V330_rev-e.vbi", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
  if (PCI)
    mem_mapping_disable(&riva128->bios_rom.mapping);

  mem_mapping_add(&riva128->mmio_mapping,     0, 0,
    riva128_mmio_read,
    riva128_mmio_read_w,
    riva128_mmio_read_l,
    riva128_mmio_write,
    riva128_mmio_write_w,
    riva128_mmio_write_l,
    NULL,
    0,
    riva128);

  mem_mapping_add(&riva128->ramin_mapping,     0, 0,
    riva128_ramin_readb,
    riva128_ramin_readw,
    riva128_ramin_readl,
    riva128_ramin_writeb,
    riva128_ramin_writew,
    riva128_ramin_writel,
    NULL,
    0,
    riva128);

  mem_mapping_add(&riva128->linear_mapping,   0, 0,
    svga_read_linear,
    svga_readw_linear,
    svga_readl_linear,
    svga_write_linear,
    svga_writew_linear,
    svga_writel_linear,
    NULL,
    0,
    &riva128->svga);

  io_sethandler(0x03c0, 0x0020, riva128_in, NULL, NULL, riva128_out, NULL, NULL, riva128);

  // riva128->pci_regs[4] = 3;
  riva128->pci_regs[4] = 7;
  riva128->pci_regs[5] = 0;
  riva128->pci_regs[6] = 0;
  riva128->pci_regs[7] = 2;
  
  riva128->pci_regs[0x2c] = 0x02;
  riva128->pci_regs[0x2d] = 0x11;
  riva128->pci_regs[0x2e] = 0x16;
  riva128->pci_regs[0x2f] = 0x10;
        
  riva128->pci_regs[0x30] = 0x00;
  riva128->pci_regs[0x32] = 0x0c;
  riva128->pci_regs[0x33] = 0x00;

  //riva128->pci_regs[0x3c] = 3;

  riva128->pmc.intr = 0;
  riva128->pbus.intr = 0;
  riva128->pfifo.intr = 0;
  riva128->pgraph.intr = 0;
  
  pci_add(riva128_pci_read, riva128_pci_write, riva128);

  return riva128;
}

static void riva128_close(void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  FILE *f = fopen("vram.dmp", "wb");
  fwrite(riva128->svga.vram, 4 << 20, 1, f);
  fclose(f);

  svga_close(&riva128->svga);

  free(riva128);
}

static int riva128_available()
{
  return rom_present("roms/Diamond_V330_rev-e.vbi");
}

static void riva128_speed_changed(void *p)
{
  riva128_t *riva128 = (riva128_t *)p;

  svga_recalctimings(&riva128->svga);
}

static void riva128_force_redraw(void *p)
{
  riva128_t *riva128 = (riva128_t *)p;

  riva128->svga.fullchange = changeframecount;
}

static void riva128_add_status_info(char *s, int max_len, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;

  svga_add_status_info(s, max_len, &riva128->svga);
}

static device_config_t riva128_config[] =
{
  {
    .name = "memory",
    .description = "Memory size",
    .type = CONFIG_SELECTION,
    .selection =
    {
      {
        .description = "2 MB",
        .value = 2
      },
      {
        .description = "4 MB",
        .value = 4
      },
      {
        .description = ""
      }
    },
    .default_int = 4
  },
  {
    .type = -1
  }
};

device_t riva128_device =
{
        "nVidia RIVA 128",
        0,
        riva128_init,
        riva128_close,
        riva128_available,
        riva128_speed_changed,
        riva128_force_redraw,
        riva128_add_status_info,
        riva128_config
};
