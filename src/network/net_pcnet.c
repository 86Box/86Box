/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of the AMD PCnet LANCE NIC controller for both the ISA
 *		and PCI buses.
 *
 * Version:	@(#)net_pcnet.c	1.0.0	2019/11/09
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *		Antony T Curtis
 *
 *		Copyright 2004-2019 Antony T Curtis
 *		Copyright 2016-2019 Miran Grca.
 */
#ifdef _WIN32
#include <winsock.h>
#else
#include <arpa/inet.h>
#endif 
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#include <time.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../io.h"
#include "../timer.h"
#include "../dma.h"
#include "../mem.h"
#include "../rom.h"
#include "../pci.h"
#include "../pic.h"
#include "../random.h"
#include "../device.h"
#include "network.h"
#include "net_pcnet.h"
#include "bswap.h"


/* PCI info. */
#define PCI_VENDID		0x1022		/* AMD */
#define PCI_DEVID		0x2000		/* PCnet-PCI II (Am79c970A) */
#define PCI_REGSIZE		256		/* size of PCI space */


typedef struct {
	mem_mapping_t mmio_mapping;
    const char	*name;
    int		board;
    int		is_pci, is_vlb;
    int		PCIBase;
    int		MMIOBase;	
    uint32_t	base_address;
    int		base_irq;
	int		dma_channel;
    int		rap, isr, lnkst;
    int		card;			/* PCI card slot */
    int		pad;
    uint8_t	prom[16];
	uint16_t csr[128];
	uint16_t bcr[32];
	uint64_t lastpoll;
    pc_timer_t	poll_timer;
	int xmit_pos;
	uint32_t rdra, tdra;
	uint8_t buffer[4096];
	int tx_busy;
	int looptest;
    uint8_t	maclocal[6];		/* configured MAC (local) address */
} nic_t;

static bar_t	pcnet_pci_bar[3];
static uint8_t	pcnet_pci_regs[PCI_REGSIZE];

#define PCNET_LOOPTEST_CRC	1
#define PCNET_LOOPTEST_NOCRC	2


/* BUS CONFIGURATION REGISTERS */
#define BCR_MSRDA    0
#define BCR_MSWRA    1
#define BCR_MC       2
#define BCR_LNKST    4
#define BCR_LED1     5
#define BCR_LED2     6
#define BCR_LED3     7
#define BCR_FDC      9
#define BCR_BSBC     18
#define BCR_EECAS    19
#define BCR_SWS      20
#define BCR_PLAT     22

#define BCR_TMAULOOP(S)  !!((S)->bcr[BCR_MC  ] & 0x4000)
#define BCR_APROMWE(S)   !!((S)->bcr[BCR_MC  ] & 0x0100)
#define BCR_DWIO(S)      !!((S)->bcr[BCR_BSBC] & 0x0080)
#define BCR_SSIZE32(S)   !!((S)->bcr[BCR_SWS ] & 0x0100)
#define BCR_SWSTYLE(S)     ((S)->bcr[BCR_SWS ] & 0x00FF)

#pragma pack(push,1)
struct ether_header {
    uint8_t ether_dhost[6];
    uint8_t ether_shost[6];
    uint16_t ether_type;
};
#pragma pack(pop)

#define CSR_INIT(S)      !!(((S)->csr[0])&0x0001)
#define CSR_STRT(S)      !!(((S)->csr[0])&0x0002)
#define CSR_STOP(S)      !!(((S)->csr[0])&0x0004)
#define CSR_TDMD(S)      !!(((S)->csr[0])&0x0008)
#define CSR_TXON(S)      !!(((S)->csr[0])&0x0010)
#define CSR_RXON(S)      !!(((S)->csr[0])&0x0020)
#define CSR_INEA(S)      !!(((S)->csr[0])&0x0040)
#define CSR_BSWP(S)      !!(((S)->csr[3])&0x0004)
#define CSR_LAPPEN(S)    !!(((S)->csr[3])&0x0020)
#define CSR_DXSUFLO(S)   !!(((S)->csr[3])&0x0040)
#define CSR_ASTRP_RCV(S) !!(((S)->csr[4])&0x0800)
#define CSR_DPOLL(S)     !!(((S)->csr[4])&0x1000)
#define CSR_SPND(S)      !!(((S)->csr[5])&0x0001)
#define CSR_LTINTEN(S)   !!(((S)->csr[5])&0x4000)
#define CSR_TOKINTD(S)   !!(((S)->csr[5])&0x8000)
#define CSR_DRX(S)       !!(((S)->csr[15])&0x0001)
#define CSR_DTX(S)       !!(((S)->csr[15])&0x0002)
#define CSR_LOOP(S)      !!(((S)->csr[15])&0x0004)
#define CSR_DXMTFCS(S)   !!(((S)->csr[15])&0x0008)
#define CSR_INTL(S)      !!(((S)->csr[15])&0x0040)
#define CSR_DRCVPA(S)    !!(((S)->csr[15])&0x2000)
#define CSR_DRCVBC(S)    !!(((S)->csr[15])&0x4000)
#define CSR_PROM(S)      !!(((S)->csr[15])&0x8000)

#define CSR_CRBC(S)      ((S)->csr[40])
#define CSR_CRST(S)      ((S)->csr[41])
#define CSR_CXBC(S)      ((S)->csr[42])
#define CSR_CXST(S)      ((S)->csr[43])
#define CSR_NRBC(S)      ((S)->csr[44])
#define CSR_NRST(S)      ((S)->csr[45])
#define CSR_POLL(S)      ((S)->csr[46])
#define CSR_PINT(S)      ((S)->csr[47])
#define CSR_RCVRC(S)     ((S)->csr[72])
#define CSR_XMTRC(S)     ((S)->csr[74])
#define CSR_RCVRL(S)     ((S)->csr[76])
#define CSR_XMTRL(S)     ((S)->csr[78])
#define CSR_MISSC(S)     ((S)->csr[112])

#define CSR_IADR(S)      ((S)->csr[ 1] | ((S)->csr[ 2] << 16))
#define CSR_CRBA(S)      ((S)->csr[18] | ((S)->csr[19] << 16))
#define CSR_CXBA(S)      ((S)->csr[20] | ((S)->csr[21] << 16))
#define CSR_NRBA(S)      ((S)->csr[22] | ((S)->csr[23] << 16))
#define CSR_BADR(S)      ((S)->csr[24] | ((S)->csr[25] << 16))
#define CSR_NRDA(S)      ((S)->csr[26] | ((S)->csr[27] << 16))
#define CSR_CRDA(S)      ((S)->csr[28] | ((S)->csr[29] << 16))
#define CSR_BADX(S)      ((S)->csr[30] | ((S)->csr[31] << 16))
#define CSR_NXDA(S)      ((S)->csr[32] | ((S)->csr[33] << 16))
#define CSR_CXDA(S)      ((S)->csr[34] | ((S)->csr[35] << 16))
#define CSR_NNRD(S)      ((S)->csr[36] | ((S)->csr[37] << 16))
#define CSR_NNXD(S)      ((S)->csr[38] | ((S)->csr[39] << 16))
#define CSR_PXDA(S)      ((S)->csr[60] | ((S)->csr[61] << 16))
#define CSR_NXBA(S)      ((S)->csr[64] | ((S)->csr[65] << 16))

#define PHYSADDR(S,A) \
  (BCR_SSIZE32(S) ? (A) : (A) | ((0xff00 & (uint32_t)(dev)->csr[2])<<16))

#pragma pack(push,1)
struct pcnet_initblk16 {
    uint16_t mode;
    uint16_t padr[3];
    uint16_t ladrf[4];
    uint32_t rdra;
    uint32_t tdra;
};

struct pcnet_initblk32 {
    uint16_t mode;
    uint8_t rlen;
    uint8_t tlen;
    uint16_t padr[3];
    uint16_t _res;
    uint16_t ladrf[4];
    uint32_t rdra;
    uint32_t tdra;
};

typedef struct RMD {
    struct
    {
        uint32_t rbadr;         /**< receive buffer address */
    } rmd0;
    struct
    {
        uint32_t bcnt:12;       /**< buffer byte count (two's complement) */
        uint32_t ones:4;        /**< must be 1111b */
        uint32_t res:4;         /**< reserved */
        uint32_t bam:1;         /**< broadcast address match */
        uint32_t lafm:1;        /**< logical filter address match */
        uint32_t pam:1;         /**< physical address match */
        uint32_t bpe:1;         /**< bus parity error */
        uint32_t enp:1;         /**< end of packet */
        uint32_t stp:1;         /**< start of packet */
        uint32_t buff:1;        /**< buffer error */
        uint32_t crc:1;         /**< crc error on incoming frame */
        uint32_t oflo:1;        /**< overflow error (lost all or part of incoming frame) */
        uint32_t fram:1;        /**< frame error */
        uint32_t err:1;         /**< error occurred */
        uint32_t own:1;         /**< 0=owned by guest driver, 1=owned by controller */
    } rmd1;
    struct
    {
        uint32_t mcnt:12;       /**< message byte count */
        uint32_t zeros:4;       /**< 0000b */
        uint32_t rpc:8;         /**< receive frame tag */
        uint32_t rcc:8;         /**< receive frame tag + reserved */
    } rmd2;
    struct
    {
        uint32_t res;           /**< reserved for user defined space */
    } rmd3;
} RMD;
#pragma pack(pop)

#define TMDL_BCNT_MASK  0x0fff
#define TMDL_BCNT_SH    0
#define TMDL_ONES_MASK  0xf000
#define TMDL_ONES_SH    12

#define TMDS_BPE_MASK   0x0080
#define TMDS_BPE_SH     7
#define TMDS_ENP_MASK   0x0100
#define TMDS_ENP_SH     8
#define TMDS_STP_MASK   0x0200
#define TMDS_STP_SH     9
#define TMDS_DEF_MASK   0x0400
#define TMDS_DEF_SH     10
#define TMDS_ONE_MASK   0x0800
#define TMDS_ONE_SH     11
#define TMDS_LTINT_MASK 0x1000
#define TMDS_LTINT_SH   12
#define TMDS_NOFCS_MASK 0x2000
#define TMDS_NOFCS_SH   13
#define TMDS_ADDFCS_MASK TMDS_NOFCS_MASK
#define TMDS_ADDFCS_SH  TMDS_NOFCS_SH
#define TMDS_ERR_MASK   0x4000
#define TMDS_ERR_SH     14
#define TMDS_OWN_MASK   0x8000
#define TMDS_OWN_SH     15

#define TMDM_TRC_MASK   0x0000000f
#define TMDM_TRC_SH     0
#define TMDM_TDR_MASK   0x03ff0000
#define TMDM_TDR_SH     16
#define TMDM_RTRY_MASK  0x04000000
#define TMDM_RTRY_SH    26
#define TMDM_LCAR_MASK  0x08000000
#define TMDM_LCAR_SH    27
#define TMDM_LCOL_MASK  0x10000000
#define TMDM_LCOL_SH    28
#define TMDM_EXDEF_MASK 0x20000000
#define TMDM_EXDEF_SH   29
#define TMDM_UFLO_MASK  0x40000000
#define TMDM_UFLO_SH    30
#define TMDM_BUFF_MASK  0x80000000
#define TMDM_BUFF_SH    31

#pragma pack(push,1)
/** Transmit Message Descriptor */
typedef struct TMD {
    struct
    {
        uint32_t tbadr;         /**< transmit buffer address */
    } tmd0;
    struct
    {
        uint32_t bcnt:12;       /**< buffer byte count (two's complement) */
        uint32_t ones:4;        /**< must be 1111b */
        uint32_t res:7;         /**< reserved */
        uint32_t bpe:1;         /**< bus parity error */
        uint32_t enp:1;         /**< end of packet */
        uint32_t stp:1;         /**< start of packet */
        uint32_t def:1;         /**< deferred */
        uint32_t one:1;         /**< exactly one retry was needed to transmit a frame */
        uint32_t ltint:1;       /**< suppress interrupts after successful transmission */
        uint32_t nofcs:1;       /**< when set, the state of DXMTFCS is ignored and
                                     transmitter FCS generation is activated. */
        uint32_t err:1;         /**< error occurred */
        uint32_t own:1;         /**< 0=owned by guest driver, 1=owned by controller */
    } tmd1;
    struct
    {
        uint32_t trc:4;         /**< transmit retry count */
        uint32_t res:12;        /**< reserved */
        uint32_t tdr:10;        /**< ??? */
        uint32_t rtry:1;        /**< retry error */
        uint32_t lcar:1;        /**< loss of carrier */
        uint32_t lcol:1;        /**< late collision */
        uint32_t exdef:1;       /**< excessive deferral */
        uint32_t uflo:1;        /**< underflow error */
        uint32_t buff:1;        /**< out of buffers (ENP not found) */
    } tmd2;
    struct
    {
        uint32_t res;           /**< reserved for user defined space */
    } tmd3;
} TMD;
#pragma pack(pop)

#define RMDL_BCNT_MASK  0x0fff
#define RMDL_BCNT_SH    0
#define RMDL_ONES_MASK  0xf000
#define RMDL_ONES_SH    12

#define RMDS_BAM_MASK   0x0010
#define RMDS_BAM_SH     4
#define RMDS_LFAM_MASK  0x0020
#define RMDS_LFAM_SH    5
#define RMDS_PAM_MASK   0x0040
#define RMDS_PAM_SH     6
#define RMDS_BPE_MASK   0x0080
#define RMDS_BPE_SH     7
#define RMDS_ENP_MASK   0x0100
#define RMDS_ENP_SH     8
#define RMDS_STP_MASK   0x0200
#define RMDS_STP_SH     9
#define RMDS_BUFF_MASK  0x0400
#define RMDS_BUFF_SH    10
#define RMDS_CRC_MASK   0x0800
#define RMDS_CRC_SH     11
#define RMDS_OFLO_MASK  0x1000
#define RMDS_OFLO_SH    12
#define RMDS_FRAM_MASK  0x2000
#define RMDS_FRAM_SH    13
#define RMDS_ERR_MASK   0x4000
#define RMDS_ERR_SH     14
#define RMDS_OWN_MASK   0x8000
#define RMDS_OWN_SH     15

#define RMDM_MCNT_MASK  0x00000fff
#define RMDM_MCNT_SH    0
#define RMDM_ZEROS_MASK 0x0000f000
#define RMDM_ZEROS_SH   12
#define RMDM_RPC_MASK   0x00ff0000
#define RMDM_RPC_SH     16
#define RMDM_RCC_MASK   0xff000000
#define RMDM_RCC_SH     24

#define SET_FIELD(regp, name, field, value)             \
  (*(regp) = (*(regp) & ~(name ## _ ## field ## _MASK)) \
             | ((value) << name ## _ ## field ## _SH))

#define GET_FIELD(reg, name, field)                     \
  (((reg) & name ## _ ## field ## _MASK) >> name ## _ ## field ## _SH)

#ifdef ENABLE_PCNET_LOG
int pcnet_do_log = ENABLE_PCNET_LOG;

static void
pcnetlog(int lvl, const char *fmt, ...)
{
    va_list ap;

    if (pcnet_do_log >= lvl) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define pcnetlog(lvl, fmt, ...)
#endif

static void pcnet_csr_writew(nic_t *dev, uint16_t rap, uint16_t new_value);
static uint16_t pcnet_csr_readw(nic_t *dev, uint16_t rap);
static void pcnet_poll(void *priv);
static void pcnet_poll_timer(void *priv);
static void pcnet_bcr_writew(nic_t *dev, uint16_t rap, uint16_t val);
static uint16_t pcnet_bcr_readw(nic_t *dev, uint16_t rap);

static __inline void 
pcnet_tmd_load(nic_t *dev, TMD *tmd1, 
                                  uint32_t addr)
{
    uint32_t *tmd = (uint32_t *)tmd1;

    if (!BCR_SWSTYLE(dev)) {
        uint16_t xda[4];
        DMAPageRead(addr, (uint8_t *)&xda[0], sizeof(xda));
        le16_to_cpus(&xda[0]);
        le16_to_cpus(&xda[1]);
        le16_to_cpus(&xda[2]);
        le16_to_cpus(&xda[3]);
        tmd[0] = (xda[0]&0xffff) |
            ((xda[1]&0x00ff) << 16);
        tmd[1] = (xda[2]&0xffff)|
            ((xda[1] & 0xff00) << 16);
        tmd[2] =
            (xda[3] & 0xffff) << 16;
        tmd[3] = 0;
    } else {
        uint32_t xda[4];
        DMAPageRead(addr, (uint8_t *)&xda[0], sizeof(xda));
        le32_to_cpus(&xda[0]);
        le32_to_cpus(&xda[1]);
        le32_to_cpus(&xda[2]);
        le32_to_cpus(&xda[3]);
        if (BCR_SWSTYLE(dev) != 3) {
            memcpy(tmd, xda, sizeof(xda));
        } else {
            tmd[0] = xda[2];
            tmd[1] = xda[1];
            tmd[2] = xda[0];
            tmd[3] = xda[3];
        }
    }
}

static __inline void 
pcnet_tmd_store(nic_t *dev, TMD *tmd1,
                                   uint32_t addr)
{
    const uint32_t *tmd = (const uint32_t *)tmd1;
    if (!BCR_SWSTYLE(dev)) {
        uint16_t xda[4];
        xda[0] = tmd[0] & 0xffff;
        xda[1] = ((tmd[0]>>16)&0x00ff) |
            ((tmd[1]>>16)&0xff00);
        xda[2] = tmd[1] & 0xffff;
        xda[3] = tmd[2] >> 16;
        cpu_to_le16s(&xda[0]);
        cpu_to_le16s(&xda[1]);
        cpu_to_le16s(&xda[2]);
        cpu_to_le16s(&xda[3]);
        DMAPageWrite(addr, (uint8_t *)&xda[0], sizeof(xda));
    } else {
        uint32_t xda[4];
        if (BCR_SWSTYLE(dev) != 3) {
            memcpy(xda, tmd, sizeof(xda));
        } else {
            xda[0] = tmd[2];
            xda[1] = tmd[1];
            xda[2] = tmd[0];
            xda[3] = tmd[3];
        }
        cpu_to_le32s(&xda[0]);
        cpu_to_le32s(&xda[1]);
        cpu_to_le32s(&xda[2]);
        cpu_to_le32s(&xda[3]);
        DMAPageWrite(addr, (uint8_t *)&xda[0], sizeof(xda));
    }
}

static __inline void 
pcnet_rmd_load(nic_t *dev, RMD *rmd1,
                                  uint32_t addr)
{
    uint32_t *rmd = (uint32_t *)rmd1;

    if (!BCR_SWSTYLE(dev)) {
        uint16_t rda[4];
        DMAPageRead(addr, (uint8_t *)&rda[0], sizeof(rda));
        le16_to_cpus(&rda[0]);
        le16_to_cpus(&rda[1]);
        le16_to_cpus(&rda[2]);
        le16_to_cpus(&rda[3]);
        rmd[0] = (rda[0]&0xffff)|
            ((rda[1] & 0x00ff) << 16);
        rmd[1] = (rda[2]&0xffff)|
            ((rda[1] & 0xff00) << 16);
        rmd[2] = rda[3] & 0xffff;
        rmd[3] = 0;
    } else {
        uint32_t rda[4];
        DMAPageRead(addr,(uint8_t *)&rda[0], sizeof(rda));
        le32_to_cpus(&rda[0]);
        le32_to_cpus(&rda[1]);
        le32_to_cpus(&rda[2]);
        le32_to_cpus(&rda[3]);
        if (BCR_SWSTYLE(dev) != 3) {
            memcpy(rmd, rda, sizeof(rda));
        } else {
            rmd[0] = rda[2];
            rmd[1] = rda[1];
            rmd[2] = rda[0];
            rmd[3] = rda[3];
        }
    }
}

static __inline void 
pcnet_rmd_store(nic_t *dev, RMD *rmd1, 
                                   uint32_t addr)
{
    const uint32_t *rmd = (const uint32_t *)rmd1;

    if (!BCR_SWSTYLE(dev)) {
        uint16_t rda[4];
        rda[0] = rmd[0] & 0xffff;
        rda[1] = ((rmd[0]>>16)&0xff)|
            ((rmd[1]>>16)&0xff00);
        rda[2] = rmd[1] & 0xffff;
        rda[3] = rmd[2] & 0xffff;
        cpu_to_le16s(&rda[0]);
        cpu_to_le16s(&rda[1]);
        cpu_to_le16s(&rda[2]);
        cpu_to_le16s(&rda[3]);
        DMAPageWrite(addr, (uint8_t *)&rda[0], sizeof(rda));
    } else {
        uint32_t rda[4];
        if (BCR_SWSTYLE(dev) != 3) {
            memcpy(rda, rmd, sizeof(rda));
        } else {
            rda[0] = rmd[2];
            rda[1] = rmd[1];
            rda[2] = rmd[0];
            rda[3] = rmd[3];
        }
        cpu_to_le32s(&rda[0]);
        cpu_to_le32s(&rda[1]);
        cpu_to_le32s(&rda[2]);
        cpu_to_le32s(&rda[3]);
        DMAPageWrite(addr, (uint8_t *)&rda[0], sizeof(rda));
    }
}

#define TMDLOAD(TMD,ADDR) pcnet_tmd_load(dev,TMD,ADDR)

#define TMDSTORE(TMD,ADDR) pcnet_tmd_store(dev,TMD,ADDR)

#define RMDLOAD(RMD,ADDR) pcnet_rmd_load(dev,RMD,ADDR)

#define RMDSTORE(RMD,ADDR) pcnet_rmd_store(dev,RMD,ADDR)

#define CHECK_RMD(ADDR,RES) do {                \
    RMD rmd;                       \
    RMDLOAD(&rmd,(ADDR));                       \
    (RES) |= (rmd.rmd1.ones != 15)              \
          || (rmd.rmd2.zeros != 0);             \
} while (0)

#define CHECK_TMD(ADDR,RES) do {                \
    TMD tmd;                       \
    TMDLOAD(&tmd,(ADDR));                       \
    (RES) |= (tmd.tmd1.ones != 15);             \
} while (0)

#define CRC(crc, ch)	 (crc = (crc >> 8) ^ crctab[(crc ^ (ch)) & 0xff])

/* generated using the AUTODIN II polynomial
 *	x^32 + x^26 + x^23 + x^22 + x^16 +
 *	x^12 + x^11 + x^10 + x^8 + x^7 + x^5 + x^4 + x^2 + x^1 + 1
 */
static const uint32_t crctab[256] = {
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba,
	0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
	0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
	0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
	0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
	0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
	0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,
	0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
	0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
	0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
	0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940,
	0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
	0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116,
	0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
	0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
	0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
	0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a,
	0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
	0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818,
	0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
	0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
	0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
	0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c,
	0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
	0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
	0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
	0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
	0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
	0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086,
	0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
	0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4,
	0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
	0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
	0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
	0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
	0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
	0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe,
	0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
	0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
	0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
	0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252,
	0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
	0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60,
	0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
	0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
	0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
	0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04,
	0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
	0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a,
	0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
	0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
	0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
	0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e,
	0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
	0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
	0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
	0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
	0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
	0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0,
	0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
	0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6,
	0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
	0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
	0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d,
};

static __inline uint32_t 
lnc_mchash(const uint8_t *ether_addr)
{
#define LNC_POLYNOMIAL          0xEDB88320UL
    uint32_t crc = 0xFFFFFFFF;
    int idx, bit;
    uint8_t data;

    for (idx = 0; idx < 6; idx++) {
        for (data = *ether_addr++, bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (((crc ^ data) & 1) ? LNC_POLYNOMIAL : 0);
            data >>= 1;
        }
    }
    return crc;
#undef LNC_POLYNOMIAL
}


static __inline int 
ladr_match(nic_t *dev, uint8_t *buf, int size)
{
    if ((buf[0] & 0x01) &&
        ((uint64_t *)&dev->csr[8])[0] != 0LL) {
        int index = lnc_mchash(buf) >> 26;
        return ((uint8_t*)(dev->csr + 8))[index >> 3] & (1 << (index & 7));
    }
    return 0;
}

static __inline uint32_t 
pcnet_rdra_addr(nic_t *dev, int idx)
{
    while (idx < 1) {
        idx += CSR_RCVRL(dev);
    }
    return dev->rdra + ((CSR_RCVRL(dev) - idx) * (BCR_SWSTYLE(dev) ? 16 : 8));
}

static void 
pcnet_update_irq(nic_t *dev)
{
    int isr = 0;
    dev->csr[0] &= ~0x0080;

#if 1
    if (((dev->csr[0] & ~dev->csr[3]) & 0x5f00) ||
        (((dev->csr[4]>>1) & ~dev->csr[4]) & 0x0115) ||
        (((dev->csr[5]>>1) & dev->csr[5]) & 0x0048))
#else
    if ((!(dev->csr[3] & 0x4000) && !!(dev->csr[0] & 0x4000)) /* BABL */ ||
        (!(dev->csr[3] & 0x1000) && !!(dev->csr[0] & 0x1000)) /* MISS */ ||
        (!(dev->csr[3] & 0x0100) && !!(dev->csr[0] & 0x0100)) /* IDON */ ||
        (!(dev->csr[3] & 0x0200) && !!(dev->csr[0] & 0x0200)) /* TINT */ ||
        (!(dev->csr[3] & 0x0400) && !!(dev->csr[0] & 0x0400)) /* RINT */ ||
        (!(dev->csr[3] & 0x0800) && !!(dev->csr[0] & 0x0800)) /* MERR */ ||
        (!(dev->csr[4] & 0x0001) && !!(dev->csr[4] & 0x0002)) /* JAB */ ||
        (!(dev->csr[4] & 0x0004) && !!(dev->csr[4] & 0x0008)) /* TXSTRT */ ||
        (!(dev->csr[4] & 0x0010) && !!(dev->csr[4] & 0x0020)) /* RCVO */ ||
        (!(dev->csr[4] & 0x0100) && !!(dev->csr[4] & 0x0200)) /* MFCO */ ||
        (!!(dev->csr[5] & 0x0040) && !!(dev->csr[5] & 0x0080)) /* EXDINT */ ||
        (!!(dev->csr[5] & 0x0008) && !!(dev->csr[5] & 0x0010)) /* MPINT */)
#endif
    {

        isr = CSR_INEA(dev);
        dev->csr[0] |= 0x0080;
    }

    if (!!(dev->csr[4] & 0x0080) && CSR_INEA(dev)) { /* UINT */
        dev->csr[4] &= ~0x0080;
        dev->csr[4] |= 0x0040;
        dev->csr[0] |= 0x0080;
        isr = 1;
    }

#if 1
    if (((dev->csr[5]>>1) & dev->csr[5]) & 0x0500)
#else
    if ((!!(dev->csr[5] & 0x0400) && !!(dev->csr[5] & 0x0800)) /* SINT */ ||
        (!!(dev->csr[5] & 0x0100) && !!(dev->csr[5] & 0x0200)) /* SLPINT */ )
#endif
    {
        isr = 1;
        dev->csr[0] |= 0x0080;
    }

    if (dev->is_pci) {
	if (isr)
		pci_set_irq(dev->card, PCI_INTA);
	  else
		pci_clear_irq(dev->card, PCI_INTA);
    } else {
	if (isr)
		picint(1<<dev->base_irq);
	  else
		picintc(1<<dev->base_irq);
	}
	
    dev->isr = isr;
}

static void 
pcnet_init_params(nic_t *dev)
{
    int rlen, tlen;
    uint16_t *padr, *ladrf, mode;
    uint32_t rdra, tdra;	
	
    if (BCR_SSIZE32(dev)) {
        struct pcnet_initblk32 initblk;
        DMAPageRead(PHYSADDR(dev,CSR_IADR(dev)),
                (uint8_t *)&initblk, sizeof(initblk));
        mode = initblk.mode;
        rlen = initblk.rlen >> 4;
        tlen = initblk.tlen >> 4;
        ladrf = initblk.ladrf;
        padr = initblk.padr;
        rdra = le32_to_cpu(initblk.rdra);
        tdra = le32_to_cpu(initblk.tdra);
        dev->rdra = PHYSADDR(dev,initblk.rdra);
        dev->tdra = PHYSADDR(dev,initblk.tdra);
    } else {
        struct pcnet_initblk16 initblk;
        DMAPageRead(PHYSADDR(dev,CSR_IADR(dev)),
                (uint8_t *)&initblk, sizeof(initblk));
        mode = initblk.mode;
        ladrf = initblk.ladrf;
        padr = initblk.padr;
        rdra = le32_to_cpu(initblk.rdra);
        tdra = le32_to_cpu(initblk.tdra);
        rlen = rdra >> 29;
        tlen = tdra >> 29;
        rdra &= 0x00ffffff;
        tdra &= 0x00ffffff;
    }

    CSR_RCVRL(dev) = (rlen < 9) ? (1 << rlen) : 512;
    CSR_XMTRL(dev) = (tlen < 9) ? (1 << tlen) : 512;
    dev->csr[ 6] = (tlen << 12) | (rlen << 8);
    dev->csr[15] = le16_to_cpu(mode);
    dev->csr[ 8] = le16_to_cpu(ladrf[0]);
    dev->csr[ 9] = le16_to_cpu(ladrf[1]);
    dev->csr[10] = le16_to_cpu(ladrf[2]);
    dev->csr[11] = le16_to_cpu(ladrf[3]);
    dev->csr[12] = le16_to_cpu(padr[0]);
    dev->csr[13] = le16_to_cpu(padr[1]);
    dev->csr[14] = le16_to_cpu(padr[2]);
    dev->rdra = PHYSADDR(dev, rdra);
    dev->tdra = PHYSADDR(dev, tdra);

    CSR_RCVRC(dev) = CSR_RCVRL(dev);
    CSR_XMTRC(dev) = CSR_XMTRL(dev);

    dev->csr[0] |= 0x0101;
    dev->csr[0] &= ~0x0004;       /* clear STOP bit */
}

static void 
pcnet_start(nic_t *dev)
{
    if (!CSR_DTX(dev)) {
        dev->csr[0] |= 0x0010;    /* set TXON */
    }
    if (!CSR_DRX(dev)) {
        dev->csr[0] |= 0x0020;    /* set RXON */
    }
    dev->csr[0] &= ~0x0004;       /* clear STOP bit */
    dev->csr[0] |= 0x0002;
}

static void 
pcnet_stop(nic_t *dev)
{
    dev->csr[0] &= ~0xffeb;
    dev->csr[0] |= 0x0014;
    dev->csr[4] &= ~0x02c2;
    dev->csr[5] &= ~0x0011;
    pcnet_poll_timer(dev);
}

static void 
pcnet_rdte_poll(nic_t *dev)
{
    dev->csr[28] = dev->csr[29] = 0;
    if (dev->rdra) {
        int bad = 0;
#if 1
        uint32_t crda = pcnet_rdra_addr(dev, CSR_RCVRC(dev));
        uint32_t nrda = pcnet_rdra_addr(dev, -1 + CSR_RCVRC(dev));
        uint32_t nnrd = pcnet_rdra_addr(dev, -2 + CSR_RCVRC(dev));
#else
        uint32_t crda = dev->rdra +
            (CSR_RCVRL(dev) - CSR_RCVRC(dev)) *
            (BCR_SWSTYLE(dev) ? 16 : 8 );
        int nrdc = CSR_RCVRC(dev)<=1 ? CSR_RCVRL(dev) : CSR_RCVRC(dev)-1;
        uint32_t nrda = dev->rdra +
            (CSR_RCVRL(dev) - nrdc) *
            (BCR_SWSTYLE(dev) ? 16 : 8 );
        int nnrc = nrdc<=1 ? CSR_RCVRL(dev) : nrdc-1;
        uint32_t nnrd = dev->rdra +
            (CSR_RCVRL(dev) - nnrc) *
            (BCR_SWSTYLE(dev) ? 16 : 8 );
#endif

        CHECK_RMD(PHYSADDR(dev,crda), bad);
        if (!bad) {
            CHECK_RMD(PHYSADDR(dev,nrda), bad);
            if (bad || (nrda == crda)) nrda = 0;
            CHECK_RMD(PHYSADDR(dev,nnrd), bad);
            if (bad || (nnrd == crda)) nnrd = 0;

            dev->csr[28] = crda & 0xffff;
            dev->csr[29] = crda >> 16;
            dev->csr[26] = nrda & 0xffff;
            dev->csr[27] = nrda >> 16;
            dev->csr[36] = nnrd & 0xffff;
            dev->csr[37] = nnrd >> 16;
#ifdef PCNET_DEBUG
            if (bad) {
                printf("pcnet: BAD RMD RECORDS AFTER 0x" TARGET_FMT_plx "\n",
                       crda);
            }
        } else {
            printf("pcnet: BAD RMD RDA=0x" TARGET_FMT_plx "\n", crda);
#endif
        }
    }

    if (CSR_CRDA(dev)) {
        RMD rmd;
        RMDLOAD(&rmd, PHYSADDR(dev,CSR_CRDA(dev)));
        CSR_CRBC(dev) = rmd.rmd1.bcnt;
        CSR_CRST(dev) = ((uint32_t *)&rmd)[1] >> 16;
#ifdef PCNET_DEBUG_RMD_X
        printf("CRDA=0x%08x CRST=0x%04x RCVRC=%d RMDL=0x%04x RMDS=0x%04x RMDM=0x%08x\n",
                PHYSADDR(dev,CSR_CRDA(dev)), CSR_CRST(dev), CSR_RCVRC(dev),
                rmd.buf_length, rmd.status, rmd.msg_length);
        PRINT_RMD(&rmd);
#endif
    } else {
        CSR_CRBC(dev) = CSR_CRST(dev) = 0;
    }

    if (CSR_NRDA(dev)) {
        RMD rmd;
        RMDLOAD(&rmd, PHYSADDR(dev,CSR_NRDA(dev)));
        CSR_NRBC(dev) = rmd.rmd1.bcnt;
        CSR_NRST(dev) = ((uint32_t *)&rmd)[1] >> 16;
    } else {
        CSR_NRBC(dev) = CSR_NRST(dev) = 0;
    }

}

static int 
pcnet_tdte_poll(nic_t *dev)
{
    dev->csr[34] = dev->csr[35] = 0;
    if (dev->tdra) {
        uint32_t cxda = dev->tdra +
            (CSR_XMTRL(dev) - CSR_XMTRC(dev)) *
            (BCR_SWSTYLE(dev) ? 16 : 8);
        int bad = 0;
        CHECK_TMD(PHYSADDR(dev, cxda), bad);
        if (!bad) {
            if (CSR_CXDA(dev) != cxda) {
                dev->csr[60] = dev->csr[34];
                dev->csr[61] = dev->csr[35];
                dev->csr[62] = CSR_CXBC(dev);
                dev->csr[63] = CSR_CXST(dev);
            }
            dev->csr[34] = cxda & 0xffff;
            dev->csr[35] = cxda >> 16;
#ifdef PCNET_DEBUG_X
            printf("pcnet: BAD TMD XDA=0x%08x\n", cxda);
#endif
        }
    }

    if (CSR_CXDA(dev)) {
        TMD tmd;
        TMDLOAD(&tmd, PHYSADDR(dev,CSR_CXDA(dev)));
        CSR_CXBC(dev) = tmd.tmd1.bcnt;
        CSR_CXST(dev) = ((uint32_t *)&tmd)[1] >> 16;
    } else {
        CSR_CXBC(dev) = CSR_CXST(dev) = 0;
    }

    return !!(CSR_CXST(dev) & 0x8000);
}

#define MIN_BUF_SIZE 60

static void 
pcnet_receive(void *priv, uint8_t *buf, int size)
{
    nic_t *dev = (nic_t *)priv;
	static uint8_t BCAST[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    int is_padr = 0, is_bcast = 0, is_ladr = 0;
    uint8_t buf1[60];

    if (CSR_DRX(dev) || CSR_STOP(dev) || CSR_SPND(dev) || !size)
        return;

    /* if too small buffer, then expand it */
    if (size < MIN_BUF_SIZE) {
        memcpy(buf1, buf, size);
        memset(buf1 + size, 0, MIN_BUF_SIZE - size);
        buf = buf1;
        size = MIN_BUF_SIZE;
    }
	
    pcnetlog(1, "%s: RX %x:%x:%x:%x:%x:%x > %x:%x:%x:%x:%x:%x len %d\n", dev->name,
	buf[6], buf[7], buf[8], buf[9], buf[10], buf[11],
	buf[0], buf[1], buf[2], buf[3], buf[4], buf[5],
 	size);	

    if (CSR_PROM(dev)
        || (is_padr=(!CSR_DRCVPA(dev)) && !memcmp(buf, dev->csr + 12, 6))
        || (is_bcast=(!CSR_DRCVBC(dev) && !memcmp(buf, BCAST, 6)))
        || (is_ladr=ladr_match(dev, buf, size))) {

        pcnet_rdte_poll(dev);

        if (!(CSR_CRST(dev) & 0x8000) && dev->rdra) {
            RMD rmd;
            int rcvrc = CSR_RCVRC(dev)-1,i;
            uint32_t nrda;
            for (i = CSR_RCVRL(dev)-1; i > 0; i--, rcvrc--) {
                if (rcvrc <= 1)
                    rcvrc = CSR_RCVRL(dev);
                nrda = dev->rdra +
                    (CSR_RCVRL(dev) - rcvrc) *
                    (BCR_SWSTYLE(dev) ? 16 : 8 );
                RMDLOAD(&rmd, PHYSADDR(dev,nrda));
                if (rmd.rmd1.own) {
#ifdef PCNET_DEBUG_RMD
                    printf("pcnet - scan buffer: RCVRC=%d PREV_RCVRC=%d\n",
                                rcvrc, CSR_RCVRC(dev));
#endif
                    CSR_RCVRC(dev) = rcvrc;
                    pcnet_rdte_poll(dev);
                    break;
                }
            }
        }

        if (!(CSR_CRST(dev) & 0x8000)) {
#ifdef PCNET_DEBUG_RMD
            printf("pcnet - no buffer: RCVRC=%d\n", CSR_RCVRC(dev));
#endif
            dev->csr[0] |= 0x1000; /* Set MISS flag */
            CSR_MISSC(dev)++;
        } else {
            uint8_t *src = &dev->buffer[8];
            uint32_t crda = CSR_CRDA(dev);
            RMD rmd;
            int pktcount = 0;

			memcpy(src, buf, size);
			/* no need to compute the CRC */
			src[size] = 0;
			src[size + 1] = 0;
			src[size + 2] = 0;
			src[size + 3] = 0;
			size += 4;

#ifdef PCNET_DEBUG_MATCH
            PRINT_PKTHDR(buf);
#endif

            RMDLOAD(&rmd, PHYSADDR(dev,crda));
            /*if (!CSR_LAPPEN(dev))*/
                rmd.rmd1.stp = 1;

#define PCNET_RECV_STORE() do {                                 \
    int count = MIN(4096 - rmd.rmd1.bcnt,size);                 \
    uint32_t rbadr = PHYSADDR(dev, rmd.rmd0.rbadr);     \
    DMAPageWrite(rbadr, src, count); \
    src += count; size -= count;                           \
    rmd.rmd2.mcnt = count; rmd.rmd1.own = 0;                    \
    RMDSTORE(&rmd, PHYSADDR(dev,crda));                           \
    pktcount++;                                                 \
} while (0)

            PCNET_RECV_STORE();
            if ((size > 0) && CSR_NRDA(dev)) {
                uint32_t nrda = CSR_NRDA(dev);
#ifdef PCNET_DEBUG_RMD
                PRINT_RMD(&rmd);
#endif
                RMDLOAD(&rmd, PHYSADDR(dev,nrda));
                if (rmd.rmd1.own) {
                    crda = nrda;
                    PCNET_RECV_STORE();
#ifdef PCNET_DEBUG_RMD
                    PRINT_RMD(&rmd);
#endif
                    if ((size > 0) && (nrda=CSR_NNRD(dev))) {
                        RMDLOAD(&rmd, PHYSADDR(dev,nrda));
                        if (rmd.rmd1.own) {
                            crda = nrda;
                            PCNET_RECV_STORE();
                        }
                    }
                }
            }

#undef PCNET_RECV_STORE

            RMDLOAD(&rmd, PHYSADDR(dev,crda));
            if (size == 0) {
                rmd.rmd1.enp = 1;
                rmd.rmd1.pam = !CSR_PROM(dev) && is_padr;
                rmd.rmd1.lafm = !CSR_PROM(dev) && is_ladr;
                rmd.rmd1.bam = !CSR_PROM(dev) && is_bcast;
            } else {
                rmd.rmd1.oflo = 1;
                rmd.rmd1.buff = 1;
                rmd.rmd1.err = 1;
            }
            RMDSTORE(&rmd, PHYSADDR(dev,crda));
            dev->csr[0] |= 0x0400;

#ifdef PCNET_DEBUG
            printf("RCVRC=%d CRDA=0x%08x BLKS=%d\n",
                CSR_RCVRC(dev), PHYSADDR(dev,CSR_CRDA(dev)), pktcount);
#endif
#ifdef PCNET_DEBUG_RMD
            PRINT_RMD(&rmd);
#endif

            while (pktcount--) {
                if (CSR_RCVRC(dev) <= 1) {
                    CSR_RCVRC(dev) = CSR_RCVRL(dev);
                } else {
                    CSR_RCVRC(dev)--;
                }
            }

            pcnet_rdte_poll(dev);

        }
    }

    pcnet_poll(dev);
    pcnet_update_irq(dev);
}

static void 
pcnet_soft_reset(nic_t *dev)
{
	dev->lnkst = 0x40;
    dev->rdra = 0;
    dev->tdra = 0;
    dev->rap = 0;

    dev->bcr[BCR_BSBC] &= ~0x0080;

    dev->csr[0]   = 0x0004;
    dev->csr[3]   = 0x0000;
    dev->csr[4]   = 0x0115;
    dev->csr[5]   = 0x0000;
    dev->csr[6]   = 0x0000;
    dev->csr[8]   = 0;
    dev->csr[9]   = 0;
    dev->csr[10]  = 0;
    dev->csr[11]  = 0;
    dev->csr[12]  = le16_to_cpu(((uint16_t *)&dev->prom[0])[0]);
    dev->csr[13]  = le16_to_cpu(((uint16_t *)&dev->prom[0])[1]);
    dev->csr[14]  = le16_to_cpu(((uint16_t *)&dev->prom[0])[2]);
    dev->csr[15] &= 0x21c4;
    dev->csr[72]  = 1;
    dev->csr[74]  = 1;
    dev->csr[76]  = 1;
    dev->csr[78]  = 1;
    dev->csr[80]  = 0x1410;
    dev->csr[88]  = 0x1003;
    dev->csr[89]  = 0x0262;
    dev->csr[94]  = 0x0000;
    dev->csr[100] = 0x0200;
    dev->csr[103] = 0x0105;
    dev->csr[112] = 0x0000;
    dev->csr[114] = 0x0000;
    dev->csr[122] = 0x0000;
    dev->csr[124] = 0x0000;

    dev->tx_busy = 0;
}


static void
pcnet_hard_reset(nic_t *dev)
{
	int i;
	uint16_t checksum;
	
    pcnetlog(2, "%s: hard reset\n", dev->name);

	/* Reserved Location: must be 00h */
	dev->prom[6] = dev->prom[7] = 0x00;
	
	/* Reserved Location: must be 00h */
	dev->prom[8] = 0x00;
	
	/* Hardware ID: must be 11h if compatibility to AMD drivers is desired */
	if (dev->is_pci)
		dev->prom[9] = 0x11;
	else if (dev->is_vlb)
		dev->prom[9] = 0x10;
	else
		dev->prom[9] = 0x00;

	/* User programmable space, init with 0 */
	dev->prom[10] = dev->prom[11] = 0x00;
	
	/* LSByte of two-byte checksum, which is the sum of bytes 00h-0Bh
		and bytes 0Eh and 0Fh, must therefore be initialized with 0! */
	dev->prom[12] = dev->prom[13] = 0x00;
	
	/* Must be ASCII W (57h) if compatibility to AMD
	   driver software is desired */
	dev->prom[14] = dev->prom[15] = 0x57;
	
	for (i = 0, checksum = 0; i < 16; i++)
		checksum += dev->prom[i];
	
	*(uint16_t *)&dev->prom[12] = cpu_to_le16(checksum);

    dev->bcr[BCR_MSRDA] = 0x0005;
    dev->bcr[BCR_MSWRA] = 0x0005;
    dev->bcr[BCR_MC   ] = 0x0002;
    dev->bcr[BCR_LNKST] = 0x00c0;
    dev->bcr[BCR_LED1 ] = 0x0084;
    dev->bcr[BCR_LED2 ] = 0x0088;
    dev->bcr[BCR_LED3 ] = 0x0090;
	
	/* For ISA PnP cards, BCR8 reports IRQ/DMA (e.g. 0x0035 means IRQ 3, DMA 5). */
	if (dev->board == PCNET_ISA)
		dev->bcr[8] = (dev->dma_channel) | (dev->base_irq << 4);
	
    dev->bcr[BCR_FDC  ] = 0x0000;
    dev->bcr[BCR_BSBC ] = 0x9001;
    dev->bcr[BCR_EECAS] = 0x0002;
    dev->bcr[BCR_SWS  ] = 0x0200;
    dev->bcr[BCR_PLAT ] = 0xff06;
	
    pcnet_soft_reset(dev);
    pcnet_update_irq(dev);
}

static void 
pcnet_transmit(nic_t *dev)
{
    uint32_t xmit_cxda = 0;
    int count = CSR_XMTRL(dev)-1;
    dev->xmit_pos = -1;
    
    if (!CSR_TXON(dev)) {
        dev->csr[0] &= ~0x0008;
        return;
    }

    dev->tx_busy = 1;

txagain:
    if (pcnet_tdte_poll(dev)) {
        TMD tmd;

        TMDLOAD(&tmd, PHYSADDR(dev,CSR_CXDA(dev)));                

#ifdef PCNET_DEBUG_TMD
        printf("  TMDLOAD 0x%08x\n", PHYSADDR(dev,CSR_CXDA(dev)));
        PRINT_TMD(&tmd);
#endif
        if (tmd.tmd1.stp) {
            dev->xmit_pos = 0;                
            if (!tmd.tmd1.enp) {
                DMAPageRead(PHYSADDR(dev, tmd.tmd0.tbadr),
                                 dev->buffer, 4096 - tmd.tmd1.bcnt);
                dev->xmit_pos += 4096 - tmd.tmd1.bcnt;
            } 
            xmit_cxda = PHYSADDR(dev,CSR_CXDA(dev));
        }
        if (tmd.tmd1.enp && (dev->xmit_pos >= 0)) {
            DMAPageRead(PHYSADDR(dev, tmd.tmd0.tbadr),
                             dev->buffer + dev->xmit_pos, 4096 - tmd.tmd1.bcnt);
            dev->xmit_pos += 4096 - tmd.tmd1.bcnt;

            if (CSR_LOOP(dev)) {
				pclog("Loopback\n");
                pcnet_receive(dev, dev->buffer, dev->xmit_pos);
			} else {
				pclog("No Loopback\n");
				network_tx(dev->buffer, dev->xmit_pos);
			}
            dev->csr[0] &= ~0x0008;   /* clear TDMD */
            dev->csr[4] |= 0x0004;    /* set TXSTRT */
            dev->xmit_pos = -1;
        }

        tmd.tmd1.own = 0;
        TMDSTORE(&tmd, PHYSADDR(dev,CSR_CXDA(dev)));
        if (!CSR_TOKINTD(dev) || (CSR_LTINTEN(dev) && tmd.tmd1.ltint))
            dev->csr[0] |= 0x0200;    /* set TINT */

        if (CSR_XMTRC(dev)<=1)
            CSR_XMTRC(dev) = CSR_XMTRL(dev);
        else
            CSR_XMTRC(dev)--;
        if (count--)
            goto txagain;
    } else 
    if (dev->xmit_pos >= 0) {
        TMD tmd;
        TMDLOAD(&tmd, PHYSADDR(dev,xmit_cxda)); 
        tmd.tmd2.buff = tmd.tmd2.uflo = tmd.tmd1.err = 1;
        tmd.tmd1.own = 0;
        TMDSTORE(&tmd, PHYSADDR(dev,xmit_cxda));
        dev->csr[0] |= 0x0200;    /* set TINT */
        if (!CSR_DXSUFLO(dev)) {
            dev->csr[0] &= ~0x0010;
        } else
        if (count--)
          goto txagain;
    }

    dev->tx_busy = 0;
}

static void 
pcnet_poll(void *priv)
{
	nic_t *dev = (nic_t *)priv;
	
    if (CSR_RXON(dev)) {
        pcnet_rdte_poll(dev);
    }

    if (CSR_TDMD(dev) || (CSR_TXON(dev) && !CSR_DPOLL(dev) && pcnet_tdte_poll(dev))) {
        /* prevent recursion */
        if (dev->tx_busy)
            return;

        pcnet_transmit(dev);
    }
}

static void 
pcnet_poll_timer(void *priv)
{
	nic_t *dev = (nic_t *)priv;
	
    if (CSR_TDMD(dev))
        pcnet_transmit(dev);

    pcnet_update_irq(dev);

    if (!CSR_STOP(dev) && !CSR_SPND(dev) && !CSR_DPOLL(dev)) {
		pcnet_poll(dev);
    }
}

static void 
pcnet_csr_writew(nic_t *dev, uint16_t rap, uint16_t val)
{
    pcnetlog(3, "pcnet_csr_writew rap=%d val=0x%04x\n", rap, val);
    switch (rap) {
    case 0:
        dev->csr[0] &= ~(val & 0x7f00); /* Clear any interrupt flags */

        dev->csr[0] = (dev->csr[0] & ~0x0040) | (val & 0x0048);

        val = (val & 0x007f) | (dev->csr[0] & 0x7f00);

        /* IFF STOP, STRT and INIT are set, clear STRT and INIT */
        if ((val & 7) == 7) {
            val &= ~3;
        }
        if (!CSR_STOP(dev) && (val & 4)) {
            pcnet_stop(dev);
        }
        if (!CSR_INIT(dev) && (val & 1)) {
            pcnet_init_params(dev);
        }
        if (!CSR_STRT(dev) && (val & 2)) {
            pcnet_start(dev);
        }
        if (CSR_TDMD(dev))
            pcnet_transmit(dev);
        return;
	case 2:
		if (dev->board == PCNET_ISA)
			val &= 0x00ff;
    case 1:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 18: /* CRBAL */
    case 19: /* CRBAU */
    case 20: /* CXBAL */
    case 21: /* CXBAU */
    case 22: /* NRBAU */
    case 23: /* NRBAU */
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
    case 30:
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
    case 37:
    case 38:
    case 39:
    case 40: /* CRBC */
    case 41:
    case 42: /* CXBC */
    case 43:
    case 44:
    case 45:
    case 46: /* POLL */
    case 47: /* POLLINT */
    case 72:
    case 74:
        break;
    case 76: /* RCVRL */
    case 78: /* XMTRL */
        val = (val > 0) ? val : 512;
        break;
    case 112:
        if (CSR_STOP(dev) || CSR_SPND(dev)) {
            break;
        }
        return;
    case 3:
        break;
    case 4:
        dev->csr[4] &= ~(val & 0x026a);
        val &= ~0x026a; val |= dev->csr[4] & 0x026a;
        break;
    case 5:
        dev->csr[5] &= ~(val & 0x0a90);
        val &= ~0x0a90; val |= dev->csr[5] & 0x0a90;
        break;
    case 16:
        pcnet_csr_writew(dev,1,val);
        return;
    case 17:
        pcnet_csr_writew(dev,2,val);
        return;
    case 58:
        pcnet_bcr_writew(dev,BCR_SWS,val);
        break;
    default:
        return;
    }
    dev->csr[rap] = val;
}

static uint16_t
pcnet_csr_readw(nic_t *dev, uint16_t rap)
{
    uint16_t val;
    switch (rap) {
    case 0:
        pcnet_update_irq(dev);
        val = dev->csr[0];
        val |= (val & 0x7800) ? 0x8000 : 0;
        break;
    case 16:
        return pcnet_csr_readw(dev,1);
    case 17:
        return pcnet_csr_readw(dev,2);
    case 58:
        return pcnet_bcr_readw(dev,BCR_SWS);
    case 88:
        val = dev->csr[89];
        val <<= 16;
        val |= dev->csr[88];
        break;
    default:
        val = dev->csr[rap];
		break;
    }
#ifdef PCNET_DEBUG_CSR
    printf("pcnet_csr_readw rap=%d val=0x%04x\n", rap, val);
#endif
    return val;
}

static void 
pcnet_bcr_writew(nic_t *dev, uint16_t rap, uint16_t val)
{
    rap &= 127;
    pcnetlog(3, "pcnet_bcr_writew rap=%d val=0x%04x\n", rap, val);
    switch (rap) {
    case BCR_SWS:
        if (!(CSR_STOP(dev) || CSR_SPND(dev)))
            return;
        val &= ~0x0300;
        switch (val & 0x00ff) {
        case 0:
            val |= 0x0200;
            break;
        case 1:
            val |= 0x0100;
            break;
        case 2:
        case 3:
            val |= 0x0300;
            break;
        default:
            pcnetlog(3, "%s: Bad SWSTYLE=0x%02x\n",
                          dev->name, val & 0xff);
            val = 0x0200;
            break;
        }
#ifdef PCNET_DEBUG
       printf("BCR_SWS=0x%04x\n", val);
#endif
        /* fall through */
    case BCR_LNKST:
    case BCR_LED1:
    case BCR_LED2:
    case BCR_LED3:
    case BCR_MC:
    case BCR_FDC:
    case BCR_BSBC:
    case BCR_EECAS:
    case BCR_PLAT:
        dev->bcr[rap] = val;
        break;
    default:
        break;
    }
}

static uint16_t 
pcnet_bcr_readw(nic_t *dev, uint16_t rap)
{
    uint16_t val;
    rap &= 127;
    switch (rap) {
    case BCR_LNKST:
    case BCR_LED1:
    case BCR_LED2:
    case BCR_LED3:
        val = dev->bcr[rap] & ~0x8000;
        val |= (val & 0x017f & dev->lnkst) ? 0x8000 : 0;
        break;
    default:
        val = rap < 32 ? dev->bcr[rap] : 0;
        break;
    }
    pcnetlog(3, "pcnet_bcr_readw rap=%d val=0x%04x\n", rap, val);
    return val;
}

static void
pcnet_word_write(nic_t *dev, uint32_t addr, uint16_t val)
{
    pcnetlog(3, "pcnet_ioport_writew addr=0x%08x val=0x%04x\n", addr, val);
    if (!BCR_DWIO(dev)) {
        switch (addr & 0x0f) {
        case 0x00: /* RDP */
			pcnet_poll_timer(dev);
            pcnet_csr_writew(dev, dev->rap, val);
			pcnet_update_irq(dev);	
            break;
        case 0x02:
            dev->rap = val & 0x7f;
            break;
        case 0x06:
            pcnet_bcr_writew(dev, dev->rap, val);
            break;
        }
    }
}

static uint16_t
pcnet_word_read(nic_t *dev, uint32_t addr)
{ 
	uint16_t val = 0xffff;

    if (!BCR_DWIO(dev)) {
		switch (addr & 0x0f) {
		case 0x00: /* RDP */
			if (!CSR_POLL(dev))
				pcnet_poll_timer(dev);
			val = pcnet_csr_readw(dev, dev->rap);
			if (!dev->rap)
				goto skip_update_irq;
			break;
		case 0x02:
			val = dev->rap;
			goto skip_update_irq;
			break;
		case 0x04:
			pcnet_soft_reset(dev);
			val = 0;
			break;
		case 0x06:
			val = pcnet_bcr_readw(dev, dev->rap);
			break;
		}
	}

	pcnet_update_irq(dev);
    
skip_update_irq:	
	pcnetlog(3, "%s: Read Word mode, addr = %08x, val = %08x\n", dev->name, addr, val);
    return(val);
}

static void 
pcnet_dword_write(nic_t *dev, uint32_t addr, uint32_t val)
{
    pcnetlog(3, "pcnet_ioport_writel addr=0x%08x val=0x%08x\n", addr, val);
    if (BCR_DWIO(dev)) {
        switch (addr & 0x0f) {
        case 0x00: /* RDP */
			pcnet_poll_timer(dev);
            pcnet_csr_writew(dev, dev->rap, val & 0xffff);
			pcnet_update_irq(dev);
            break;
        case 0x04:
            dev->rap = val & 0x7f;
            break;
        case 0x0c:
            pcnet_bcr_writew(dev, dev->rap, val & 0xffff);
            break;
        }
    } else if ((addr & 0x0f) == 0) {
        /* switch device to dword i/o mode */
        pcnet_bcr_writew(dev, BCR_BSBC, pcnet_bcr_readw(dev, BCR_BSBC) | 0x0080);
        pcnetlog(3, "device switched into dword i/o mode\n");
    }
}

static uint32_t
pcnet_dword_read(nic_t *dev, uint32_t addr)
{ 
	uint32_t val = 0xffffffff;
	
    if (BCR_DWIO(dev)) {
		switch (addr & 0x0f) {
		case 0x00: /* RDP */
			if (!CSR_POLL(dev))
				pcnet_poll_timer(dev);
			val = pcnet_csr_readw(dev, dev->rap);
			if (!dev->rap)
				goto skip_update_irq;
			break;
		case 0x04:
			val = dev->rap;
			goto skip_update_irq;
			break;
		case 0x08:
			pcnet_soft_reset(dev);
			val = 0;
			break;
		case 0x0c:
			val = pcnet_bcr_readw(dev, dev->rap);
			break;
		}
	}

	pcnet_update_irq(dev);
	
skip_update_irq:	
    pcnetlog(3, "%s: Read Long mode, addr = %08x, val = %08x\n", dev->name, addr, val);
    return(val);
}

static void 
pcnet_aprom_writeb(nic_t *dev, uint32_t addr, uint32_t val)
{
    if (BCR_APROMWE(dev)) {
        dev->prom[addr & 15] = val;
    }
}

static uint32_t 
pcnet_aprom_readb(nic_t *dev, uint32_t addr)
{
    uint32_t val = dev->prom[addr & 15];
    return val;
}

static void
pcnet_write(nic_t *dev, uint32_t addr, uint32_t val, int len)
{
	uint16_t off = addr & 0x1f;
	
	if (off < 0x10) {
		if (!BCR_DWIO(dev) && len == 1)
			pcnet_aprom_writeb(dev, addr, val);
		else if (!BCR_DWIO(dev) && ((addr & 1) == 0) && len == 2) {
            pcnet_aprom_writeb(dev, addr, val);
            pcnet_aprom_writeb(dev, addr + 1, val >> 8);
		} else if (BCR_DWIO(dev) && ((addr & 3) == 0) && len == 4) {
            pcnet_aprom_writeb(dev, addr, val);
            pcnet_aprom_writeb(dev, addr + 1, val >> 8);
            pcnet_aprom_writeb(dev, addr + 2, val >> 16);
            pcnet_aprom_writeb(dev, addr + 3, val >> 24);
		}
	} else {
		if (len == 2)
			pcnet_word_write(dev, addr, val);
		else if (len == 4)
			pcnet_dword_write(dev, addr, val);
	}
}


static void
pcnet_writeb(uint16_t addr, uint8_t val, void *priv)
{
    pcnet_write((nic_t *)priv, addr, val, 1);
}


static void
pcnet_writew(uint16_t addr, uint16_t val, void *priv)
{
    pcnet_write((nic_t *)priv, addr, val, 2);
}


static void
pcnet_writel(uint16_t addr, uint32_t val, void *priv)
{
    pcnet_write((nic_t *)priv, addr, val, 4);
}

static uint32_t
pcnet_read(nic_t *dev, uint32_t addr, int len)
{
    uint32_t retval = 0xffffffff;
	uint16_t off = addr & 0x1f;

    pcnetlog(3, "%s: read addr %x, off %x, len %d\n", dev->name, addr, off, len);

	if (off < 0x10) {
		if (!BCR_DWIO(dev) && len == 1) {
			retval = pcnet_aprom_readb(dev, addr);
		} else if (!BCR_DWIO(dev) && ((addr & 1) == 0) && len == 2) {
			retval = pcnet_aprom_readb(dev, addr) | (pcnet_aprom_readb(dev, addr + 1) << 8);
		} else if (BCR_DWIO(dev) && ((addr & 3) == 0) && len == 4) {
			retval = pcnet_aprom_readb(dev, addr) | (pcnet_aprom_readb(dev, addr + 1) << 8) |
					(pcnet_aprom_readb(dev, addr + 2) << 16) | (pcnet_aprom_readb(dev, addr + 3) << 24);
		}
	} else {
		if (len == 2)
			retval = pcnet_word_read(dev, addr);
		else if (len == 4)
			retval = pcnet_dword_read(dev, addr);
	}
	pcnetlog(3, "%s: value in read - %08x\n",
				dev->name, retval);
    return(retval);
}


static uint8_t
pcnet_readb(uint16_t addr, void *priv)
{
    return(pcnet_read((nic_t *)priv, addr, 1));
}


static uint16_t
pcnet_readw(uint16_t addr, void *priv)
{
    return(pcnet_read((nic_t *)priv, addr, 2));
}


static uint32_t
pcnet_readl(uint16_t addr, void *priv)
{
    return(pcnet_read((nic_t *)priv, addr, 4));
}

static void
pcnet_mmio_writeb(uint32_t addr, uint8_t val, void *priv)
{
    pcnet_write((nic_t *)priv, addr, val, 1);
}


static void
pcnet_mmio_writew(uint32_t addr, uint16_t val, void *priv)
{
    pcnet_write((nic_t *)priv, addr, val, 2);
}


static void
pcnet_mmio_writel(uint32_t addr, uint32_t val, void *priv)
{
    pcnet_write((nic_t *)priv, addr, val, 4);
}

static uint8_t
pcnet_mmio_readb(uint32_t addr, void *priv)
{
    return(pcnet_read((nic_t *)priv, addr, 1));
}


static uint16_t
pcnet_mmio_readw(uint32_t addr, void *priv)
{
    return(pcnet_read((nic_t *)priv, addr, 2));
}


static uint32_t
pcnet_mmio_readl(uint32_t addr, void *priv)
{
    return(pcnet_read((nic_t *)priv, addr, 4));
}


static void
pcnet_mem_init(nic_t *dev, uint32_t addr)
{
    mem_mapping_add(&dev->mmio_mapping, addr, 32,
		    pcnet_mmio_readb, pcnet_mmio_readw, pcnet_mmio_readl,
		    pcnet_mmio_writeb, pcnet_mmio_writew, pcnet_mmio_writel,
		    NULL, MEM_MAPPING_EXTERNAL, dev);
}

static void
pcnet_mem_set_addr(nic_t *dev, uint32_t base)
{
    mem_mapping_set_addr(&dev->mmio_mapping, base, 32);
}

static void
pcnet_mem_disable(nic_t *dev)
{
    mem_mapping_disable(&dev->mmio_mapping);
}

static void
pcnet_ioremove(nic_t *dev, uint16_t addr, int len)
{
    if (dev->is_pci || dev->is_vlb) {
	io_removehandler(addr, len,
			 pcnet_readb, pcnet_readw, pcnet_readl,
			 pcnet_writeb, pcnet_writew, pcnet_writel, dev);
    } else {
	io_removehandler(addr, len,
			 pcnet_readb, pcnet_readw, NULL,
			 pcnet_writeb, pcnet_writew, NULL, dev);
	}
}

static void
pcnet_ioset(nic_t *dev, uint16_t addr, int len)
{	
	pcnet_ioremove(dev, addr, len);

    if (dev->is_pci || dev->is_vlb) {
	io_sethandler(addr, len,
			 pcnet_readb, pcnet_readw, pcnet_readl,
			 pcnet_writeb, pcnet_writew, pcnet_writel, dev);
    } else {
	io_sethandler(addr, len,
			 pcnet_readb, pcnet_readw, NULL,
			 pcnet_writeb, pcnet_writew, NULL, dev);	
	}
}

static void
pcnet_pci_write(int func, int addr, uint8_t val, void *p)
{
    nic_t *dev = (nic_t *)p;
    uint8_t valxor;

    pcnetlog(2, "%s: Write value %02X to register %02X\n", dev->name, val, addr & 0xff);

    switch (addr) 
	{
	case 0x04:
		valxor = (val & 0x57) ^ pcnet_pci_regs[addr];
		if (valxor & PCI_COMMAND_IO) {
			pcnet_ioremove(dev, dev->PCIBase, 32);
			if ((dev->PCIBase != 0) && (val & PCI_COMMAND_IO))
				pcnet_ioset(dev, dev->PCIBase, 32);
		}
		if (valxor & PCI_COMMAND_MEM) {
			pcnet_mem_disable(dev);
			if ((dev->MMIOBase != 0) && (val & PCI_COMMAND_MEM))
				pcnet_mem_set_addr(dev, dev->MMIOBase);
		}
		pcnet_pci_regs[addr] = val & 0x57;
		break;

	case 0x05:
		pcnet_pci_regs[addr] = val & 0x01;
		break;

	case 0x0D:
		pcnet_pci_regs[addr] = val;
		break;

	case 0x10: case 0x11: case 0x12: case 0x13:
		/* I/O Base set. */
		/* First, remove the old I/O. */
		pcnet_ioremove(dev, dev->PCIBase, 32);
		/* Then let's set the PCI regs. */
		pcnet_pci_bar[0].addr_regs[addr & 3] = val;
		/* Then let's calculate the new I/O base. */
		pcnet_pci_bar[0].addr &= 0xff00;
		dev->PCIBase = pcnet_pci_bar[0].addr;
		/* Log the new base. */
		pcnetlog(2, "%s: New I/O base is %04X\n" , dev->name, dev->PCIBase);
		/* We're done, so get out of the here. */
		if (pcnet_pci_regs[4] & PCI_COMMAND_IO) {
			if (dev->PCIBase != 0) {
				pcnet_ioset(dev, dev->PCIBase, 32);
			}
		}
		return;

	case 0x15: case 0x16: case 0x17:
		/* MMIO Base set. */
		/* First, remove the old I/O. */
		pcnet_mem_disable(dev);
		/* Then let's set the PCI regs. */
		pcnet_pci_bar[1].addr_regs[addr & 3] = val;
		/* Then let's calculate the new I/O base. */
		pcnet_pci_bar[1].addr &= 0xffffc000;
		dev->MMIOBase = pcnet_pci_bar[1].addr & 0xffffc000;
		/* Log the new base. */
		pcnetlog(2, "%s: New MMIO base is %08X\n" , dev->name, dev->MMIOBase);
		/* We're done, so get out of the here. */
		if (pcnet_pci_regs[4] & PCI_COMMAND_MEM) {
			if (dev->MMIOBase != 0)
				pcnet_mem_set_addr(dev, dev->MMIOBase);
		}
		return;	

	case 0x3C:
		pcnet_pci_regs[addr] = val;
		dev->base_irq = val;
		return;
    }
}

static uint8_t
pcnet_pci_read(int func, int addr, void *p)
{
    nic_t *dev = (nic_t *)p;

    pcnetlog(2, "%s: Read to register %02X\n", dev->name, addr & 0xff);

    switch (addr) {
	case 0x00:
		return 0x22;
	case 0x01:
		return 0x10;
	case 0x02:
		return 0x00;
	case 0x03:
		return 0x20;
	case 0x04:
		return pcnet_pci_regs[0x04] & 0x57;	/*Respond to IO and memory accesses*/
	case 0x05:
		return pcnet_pci_regs[0x05] & 0x01;
	case 0x06:
		return 0x80;
	case 0x07:
		return 2;
	case 0x08:
		return 0x10;		/*Revision ID*/
	case 0x09:
		return 0;			/*Programming interface*/
	case 0x0A:
		return 0;			/*devubclass*/
	case 0x0B:
		return 2;			/*Class code*/
	case 0x0D:
		return pcnet_pci_regs[addr];
	case 0x0E:
		return 0;			/*Header type */
	case 0x10:
		return 1;			/*I/O space*/
	case 0x11:
		return pcnet_pci_bar[0].addr_regs[1];
	case 0x12:
		return pcnet_pci_bar[0].addr_regs[2];
	case 0x13:
		return pcnet_pci_bar[0].addr_regs[3];
	case 0x14:
		return 0;			/*Memory space*/
	case 0x15:
		return pcnet_pci_bar[1].addr_regs[1];
	case 0x16:
		return pcnet_pci_bar[1].addr_regs[2];
	case 0x17:
		return pcnet_pci_bar[1].addr_regs[3];
	case 0x3C:
		return dev->base_irq;
	case 0x3D:
		return PCI_INTA;
	case 0x3E:
		return 0x06;
	case 0x3F:
		return 0xff;
    }

    return(0);
}

static void *
pcnet_init(const device_t *info)
{
    uint32_t mac;
    nic_t *dev;
#ifdef ENABLE_NIC_LOG
    int i;
#endif

    /* Get the desired debug level. */
#ifdef ENABLE_NIC_LOG
    i = device_get_config_int("debug");
    if (i > 0) pcnet_do_log = i;
#endif

    dev = malloc(sizeof(nic_t));
    memset(dev, 0x00, sizeof(nic_t));
    dev->name = info->name;
    dev->board = info->local;
	
	dev->is_pci = !!(info->flags & DEVICE_PCI);
	dev->is_vlb = !!(info->flags & DEVICE_VLB);
	
	if (dev->is_pci) {
		pcnet_mem_init(dev, 0x0fffff00);
		pcnet_mem_disable(dev);
	}

	dev->maclocal[0] = 0x00;  /* 00:0C:87 (AMD OID) */
	dev->maclocal[1] = 0x0C;
	dev->maclocal[2] = 0x87;

    /* See if we have a local MAC address configured. */
    mac = device_get_config_mac("mac", -1);

    /* Set up our BIA. */
    if (mac & 0xff000000) {
	/* Generate new local MAC. */
	dev->maclocal[3] = random_generate();
	dev->maclocal[4] = random_generate();
	dev->maclocal[5] = random_generate();
	mac = (((int) dev->maclocal[3]) << 16);
	mac |= (((int) dev->maclocal[4]) << 8);
	mac |= ((int) dev->maclocal[5]);
	device_set_config_mac("mac", mac);
    } else {
	dev->maclocal[3] = (mac>>16) & 0xff;
	dev->maclocal[4] = (mac>>8) & 0xff;
	dev->maclocal[5] = (mac & 0xff);
    }

    memcpy(dev->prom, dev->maclocal, sizeof(dev->maclocal));

    /*
     * Make this device known to the I/O system.
     * PCI devices start with address spaces inactive.
     */
    if (dev->is_pci) {
		/*
		 * Configure the PCI space registers.
		 *
		 * We do this here, so the I/O routines are generic.
		 */
		
		/* Enable our address space in PCI. */
		pcnet_pci_bar[0].addr_regs[0] = 1;
		pcnet_pci_bar[1].addr_regs[0] = 0;
		pcnet_pci_regs[0x04] = 3;

		/* Add device to the PCI bus, keep its slot number. */
		dev->card = pci_add_card(PCI_ADD_NORMAL,
					 pcnet_pci_read, pcnet_pci_write, dev);
    } else {
		dev->base_address = device_get_config_hex16("base");
		dev->base_irq = device_get_config_int("irq");
		if (dev->is_vlb)
			dev->dma_channel = 0;
		else
			dev->dma_channel = device_get_config_int("dma");
		pcnet_ioset(dev, dev->base_address, 0x20);
	}

    pcnetlog(2, "%s: I/O=%04x, IRQ=%d, MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
	dev->name, dev->base_address, dev->base_irq,
	dev->prom[0], dev->prom[1], dev->prom[2],
	dev->prom[3], dev->prom[4], dev->prom[5]);

    pcnetlog(1, "%s: %s attached IO=0x%X IRQ=%d\n", dev->name,
	dev->is_pci?"PCI":"ISA", dev->base_address, dev->base_irq);

    /* Attach ourselves to the network module. */
    network_attach(dev, dev->prom, pcnet_receive);	

	/* Reset the board. */
	pcnet_hard_reset(dev);

    return(dev);
}


static void
pcnet_close(void *priv)
{
    nic_t *dev = (nic_t *)priv;

    pcnetlog(1, "%s: closed\n", dev->name);

    free(dev);
}


static const device_config_t pcnet_pci_config[] =
{
	{
		"mac", "MAC Address", CONFIG_MAC, "", -1
	},
	{
		"", "", -1
	}
};


static const device_config_t pcnet_isa_config[] =
{
	{
		"base", "Address", CONFIG_HEX16, "", 0x300,
		{
			{
				"0x300", 0x300
			},
			{
				"0x320", 0x320
			},
			{
				"0x340", 0x340
			},
			{
				"0x360", 0x360
			},
			{
				""
			}
		},
	},
	{
		"irq", "IRQ", CONFIG_SELECTION, "", 3,
		{
			{
				"IRQ 3", 3
			},
			{
				"IRQ 4", 4
			},			
			{
				"IRQ 5", 5
			},
			{
				"IRQ 9", 9
			},
			{
				""
			}
		},
	},
	{
		"dma", "DMA channel", CONFIG_SELECTION, "", 5,
		{
			{
				"DMA 3", 3
			},
			{
				"DMA 5", 5
			},
			{
				"DMA 6", 6
			},
			{
				"DMA 7", 7
			},
			{
				""
			}
		},
	},	
	{
		"mac", "MAC Address", CONFIG_MAC, "", -1
	},
	{
		"", "", -1
	}
};

static const device_config_t pcnet_vlb_config[] =
{
	{
		"base", "Address", CONFIG_HEX16, "", 0x300,
		{
			{
				"0x300", 0x300
			},
			{
				"0x320", 0x320
			},
			{
				"0x340", 0x340
			},
			{
				"0x360", 0x360
			},
			{
				""
			}
		},
	},
	{
		"irq", "IRQ", CONFIG_SELECTION, "", 3,
		{
			{
				"IRQ 3", 3
			},
			{
				"IRQ 4", 4
			},			
			{
				"IRQ 5", 5
			},
			{
				"IRQ 9", 9
			},
			{
				""
			}
		},
	},
	{
		"mac", "MAC Address", CONFIG_MAC, "", -1
	},
	{
		"", "", -1
	}
};

const device_t pcnet_pci_device = {
    "AMD PCnet-PCI",
    DEVICE_PCI,
    PCNET_PCI,
    pcnet_init, pcnet_close, NULL,
    NULL, NULL, NULL,
    pcnet_pci_config
};

const device_t pcnet_isa_device = {
    "AMD PCnet-ISA",
    DEVICE_AT | DEVICE_ISA,
    PCNET_ISA,
    pcnet_init, pcnet_close, NULL,
    NULL, NULL, NULL,
    pcnet_isa_config
};

const device_t pcnet_vlb_device = {
    "AMD PCnet-VL",
    DEVICE_VLB,
    PCNET_VLB,
    pcnet_init, pcnet_close, NULL,
    NULL, NULL, NULL,
    pcnet_vlb_config
};