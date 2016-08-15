/////////////////////////////////////////////////////////////////////////
// $Id: ne2k.cc,v 1.56.2.1 2004/02/02 22:37:22 cbothamy Exp $
/////////////////////////////////////////////////////////////////////////
//
//  Copyright (C) 2002  MandrakeSoft S.A.
//
//    MandrakeSoft S.A.
//    43, rue d'Aboukir
//    75002 Paris - France
//    http://www.linux-mandrake.com/
//    http://www.mandrakesoft.com/
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA

// Peter Grehan (grehan@iprg.nokia.com) coded all of this
// NE2000/ether stuff.
//#include "vl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "slirp/slirp.h"
#include "slirp/queue.h"
#include <pcap.h>

#include "ibm.h"
#include "device.h"

#include "config.h"
#include "nethandler.h"

#include "io.h"
#include "mem.h"
#include "nethandler.h"
#include "rom.h"

#include "ne2000.h"
#include "pci.h"
#include "pic.h"
#include "timer.h"

//THIS IS THE DEFAULT MAC ADDRESS .... so it wont place nice with multiple VMs. YET.
uint8_t maclocal[6] = {0xac, 0xde, 0x48, 0x88, 0xbb, 0xaa};

#define NETBLOCKING 0           //we won't block our pcap

static HINSTANCE net_hLib = 0;                      /* handle to DLL */
static char* net_lib_name = "wpcap.dll";
pcap_t *net_pcap;
typedef pcap_t* (__cdecl * PCAP_OPEN_LIVE)(const char *, int, int, int, char *);
typedef int (__cdecl * PCAP_SENDPACKET)(pcap_t* handle, const u_char* msg, int len);
typedef int (__cdecl * PCAP_SETNONBLOCK)(pcap_t *, int, char *);
typedef const u_char*(__cdecl *PCAP_NEXT)(pcap_t *, struct pcap_pkthdr *);
typedef const char*(__cdecl *PCAP_LIB_VERSION)(void);
typedef void (__cdecl *PCAP_CLOSE)(pcap_t *);
typedef int  (__cdecl *PCAP_GETNONBLOCK)(pcap_t *p, char *errbuf);
typedef int (__cdecl *PCAP_COMPILE)(pcap_t *p, struct bpf_program *fp, const char *str, int optimize, bpf_u_int32 netmask);
typedef int (__cdecl *PCAP_SETFILTER)(pcap_t *p, struct bpf_program *fp);

PCAP_LIB_VERSION        _pcap_lib_version;
PCAP_OPEN_LIVE          _pcap_open_live;
PCAP_SENDPACKET         _pcap_sendpacket;
PCAP_SETNONBLOCK        _pcap_setnonblock;
PCAP_NEXT               _pcap_next;
PCAP_CLOSE              _pcap_close;
PCAP_GETNONBLOCK        _pcap_getnonblock;
PCAP_COMPILE            _pcap_compile;
PCAP_SETFILTER          _pcap_setfilter;

queueADT        slirpq;
int net_slirp_inited=0;
int net_is_slirp=1;     //by default we go with slirp
int net_is_pcap=0;      //and pretend pcap is dead.
int fizz=0;
void slirp_tic();

#define BX_RESET_HARDWARE 0
#define BX_RESET_SOFTWARE 1

//Never completely fill the ne2k ring so that we never
// hit the unclear completely full buffer condition.
#define BX_NE2K_NEVER_FULL_RING (1)

#define  BX_NE2K_MEMSIZ    (32*1024)
#define  BX_NE2K_MEMSTART  (16*1024)
#define  BX_NE2K_MEMEND    (BX_NE2K_MEMSTART + BX_NE2K_MEMSIZ)

uint8_t rtl8029as_eeprom[128];

typedef struct ne2000_t
{
  //
  // ne2k register state

  //
  // Page 0
  //
  //  Command Register - 00h read/write
  struct CR_t {
    int  stop;          // STP - Software Reset command
    int  start;         // START - start the NIC
    int  tx_packet;     // TXP - initiate packet transmission
    uint8_t    rdma_cmd;      // RD0,RD1,RD2 - Remote DMA command
    uint8_t      pgsel;         // PS0,PS1 - Page select
  } CR;
  // Interrupt Status Register - 07h read/write
  struct ISR_t {
    int  pkt_rx;        // PRX - packet received with no errors
    int  pkt_tx;        // PTX - packet transmitted with no errors
    int  rx_err;        // RXE - packet received with 1 or more errors
    int  tx_err;        // TXE - packet tx'd       "  " "    "    "
    int  overwrite;     // OVW - rx buffer resources exhausted
    int  cnt_oflow;     // CNT - network tally counter MSB's set
    int  rdma_done;     // RDC - remote DMA complete
    int  reset;         // RST - reset status
  } ISR;
  // Interrupt Mask Register - 0fh write
  struct IMR_t {
    int  rx_inte;       // PRXE - packet rx interrupt enable
    int  tx_inte;       // PTXE - packet tx interrput enable
    int  rxerr_inte;    // RXEE - rx error interrupt enable
    int  txerr_inte;    // TXEE - tx error interrupt enable
    int  overw_inte;    // OVWE - overwrite warn int enable
    int  cofl_inte;     // CNTE - counter o'flow int enable
    int  rdma_inte;     // RDCE - remote DMA complete int enable
    int  reserved;      //  D7 - reserved
  } IMR;
  // Data Configuration Register - 0eh write
  struct DCR_t {
    int  wdsize;        // WTS - 8/16-bit select
    int  endian;        // BOS - byte-order select
    int  longaddr;      // LAS - long-address select
    int  loop;          // LS  - loopback select
    int  auto_rx;       // AR  - auto-remove rx packets with remote DMA
    uint8_t    fifo_size;       // FT0,FT1 - fifo threshold
  } DCR;
  // Transmit Configuration Register - 0dh write
  struct TCR_t {
    int  crc_disable;   // CRC - inhibit tx CRC
    uint8_t    loop_cntl;       // LB0,LB1 - loopback control
    int  ext_stoptx;    // ATD - allow tx disable by external mcast
    int  coll_prio;     // OFST - backoff algorithm select
    uint8_t    reserved;      //  D5,D6,D7 - reserved
  } TCR;
  // Transmit Status Register - 04h read
  struct TSR_t {
    int  tx_ok;         // PTX - tx complete without error
    int  reserved;      //  D1 - reserved
    int  collided;      // COL - tx collided >= 1 times
    int  aborted;       // ABT - aborted due to excessive collisions
    int  no_carrier;    // CRS - carrier-sense lost
    int  fifo_ur;       // FU  - FIFO underrun
    int  cd_hbeat;      // CDH - no tx cd-heartbeat from transceiver
    int  ow_coll;       // OWC - out-of-window collision
  } TSR;
  // Receive Configuration Register - 0ch write
  struct RCR_t {
    int  errors_ok;     // SEP - accept pkts with rx errors
    int  runts_ok;      // AR  - accept < 64-byte runts
    int  broadcast;     // AB  - accept eth broadcast address
    int  multicast;     // AM  - check mcast hash array
    int  promisc;       // PRO - accept all packets
    int  monitor;       // MON - check pkts, but don't rx
    uint8_t    reserved;        //  D6,D7 - reserved
  } RCR;
  // Receive Status Register - 0ch read
  struct RSR_t {
    int  rx_ok;         // PRX - rx complete without error
    int  bad_crc;       // CRC - Bad CRC detected
    int  bad_falign;    // FAE - frame alignment error
    int  fifo_or;       // FO  - FIFO overrun
    int  rx_missed;     // MPA - missed packet error
    int  rx_mbit;       // PHY - unicast or mcast/bcast address match
    int  rx_disabled;   // DIS - set when in monitor mode
    int  deferred;      // DFR - collision active
  } RSR;

  uint16_t local_dma;   // 01,02h read ; current local DMA addr
  uint8_t  page_start;  // 01h write ; page start register
  uint8_t  page_stop;   // 02h write ; page stop register
  uint8_t  bound_ptr;   // 03h read/write ; boundary pointer
  uint8_t  tx_page_start; // 04h write ; transmit page start register
  uint8_t  num_coll;    // 05h read  ; number-of-collisions register
  uint16_t tx_bytes;    // 05,06h write ; transmit byte-count register
  uint8_t  fifo;        // 06h read  ; FIFO
  uint16_t remote_dma;  // 08,09h read ; current remote DMA addr
  uint16_t remote_start;  // 08,09h write ; remote start address register
  uint16_t remote_bytes;  // 0a,0bh write ; remote byte-count register
  uint8_t  tallycnt_0;  // 0dh read  ; tally counter 0 (frame align errors)
  uint8_t  tallycnt_1;  // 0eh read  ; tally counter 1 (CRC errors)
  uint8_t  tallycnt_2;  // 0fh read  ; tally counter 2 (missed pkt errors)

  //
  // Page 1
  //
  //   Command Register 00h (repeated)
  //
  uint8_t  physaddr[6];  // 01-06h read/write ; MAC address
  uint8_t  curr_page;    // 07h read/write ; current page register
  uint8_t  mchash[8];    // 08-0fh read/write ; multicast hash array

  //
  // Page 2  - diagnostic use only
  //
  //   Command Register 00h (repeated)
  //
  //   Page Start Register 01h read  (repeated)
  //   Page Stop Register  02h read  (repeated)
  //   Current Local DMA Address 01,02h write (repeated)
  //   Transmit Page start address 04h read (repeated)
  //   Receive Configuration Register 0ch read (repeated)
  //   Transmit Configuration Register 0dh read (repeated)
  //   Data Configuration Register 0eh read (repeated)
  //   Interrupt Mask Register 0fh read (repeated)
  //
  uint8_t  rempkt_ptr;   // 03h read/write ; remote next-packet pointer
  uint8_t  localpkt_ptr; // 05h read/write ; local next-packet pointer
  uint16_t address_cnt;  // 06,07h read/write ; address counter

    //
    // Page 3  - should never be modified.
    //

    // Novell ASIC state
  uint8_t  macaddr[32];          // ASIC ROM'd MAC address, even bytes
  uint8_t  mem[BX_NE2K_MEMSIZ];  // on-chip packet memory

    // ne2k internal state
  uint32_t base_address;
  int    base_irq;
  int    tx_timer_index;
  int    tx_timer_active;

  rom_t bios_rom;
  
} ne2000_t;

int disable_netbios = 0;

void ne2000_tx_event(void *p, uint32_t val);
uint32_t ne2000_chipmem_read(ne2000_t *ne2000, uint32_t address, unsigned int io_len);
void ne2000_page0_write(ne2000_t *ne2000, uint32_t offset, uint32_t value, unsigned io_len);
void ne2000_rx_frame(void *p, const void *buf, int io_len);

int ne2000_do_log = 0;

void ne2000_log(const char *format, ...)
{
   if (ne2000_do_log)
   {
	va_list ap;
	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
	fflush(stdout);
   }
}

static void ne2000_setirq(ne2000_t *ne2000, int irq)
{
    ne2000->base_irq = irq;
}
//
// reset - restore state to power-up, cancelling all i/o
//
static void ne2000_reset(void *p, int reset)
{
  ne2000_t *ne2000 = (ne2000_t *)p;
  int i;

  ne2000_log("ne2000 reset\n");

		// Initialise the mac address area by doubling the physical address
		ne2000->macaddr[0]  = ne2000->physaddr[0];
		ne2000->macaddr[1]  = ne2000->physaddr[0];
		ne2000->macaddr[2]  = ne2000->physaddr[1];
		ne2000->macaddr[3]  = ne2000->physaddr[1];
		ne2000->macaddr[4]  = ne2000->physaddr[2];
		ne2000->macaddr[5]  = ne2000->physaddr[2];
		ne2000->macaddr[6]  = ne2000->physaddr[3];
		ne2000->macaddr[7]  = ne2000->physaddr[3];
		ne2000->macaddr[8]  = ne2000->physaddr[4];
		ne2000->macaddr[9]  = ne2000->physaddr[4];
		ne2000->macaddr[10] = ne2000->physaddr[5];
		ne2000->macaddr[11] = ne2000->physaddr[5];

		// ne2k signature
		for (i = 12; i < 32; i++)
			ne2000->macaddr[i] = 0x57;

  // Zero out registers and memory
  memset( & ne2000->CR,  0, sizeof(ne2000->CR) );
  memset( & ne2000->ISR, 0, sizeof(ne2000->ISR));
  memset( & ne2000->IMR, 0, sizeof(ne2000->IMR));
  memset( & ne2000->DCR, 0, sizeof(ne2000->DCR));
  memset( & ne2000->TCR, 0, sizeof(ne2000->TCR));
  memset( & ne2000->TSR, 0, sizeof(ne2000->TSR));
  //memset( & ne2000->RCR, 0, sizeof(ne2000->RCR));
  memset( & ne2000->RSR, 0, sizeof(ne2000->RSR));
  ne2000->tx_timer_active = 0;
  ne2000->local_dma  = 0;
  ne2000->page_start = 0;
  ne2000->page_stop  = 0;
  ne2000->bound_ptr  = 0;
  ne2000->tx_page_start = 0;
  ne2000->num_coll   = 0;
  ne2000->tx_bytes   = 0;
  ne2000->fifo       = 0;
  ne2000->remote_dma = 0;
  ne2000->remote_start = 0;
  ne2000->remote_bytes = 0;
  ne2000->tallycnt_0 = 0;
  ne2000->tallycnt_1 = 0;
  ne2000->tallycnt_2 = 0;

  //memset( & ne2000->physaddr, 0, sizeof(ne2000->physaddr));
  //memset( & ne2000->mchash, 0, sizeof(ne2000->mchash));
  ne2000->curr_page = 0;

  ne2000->rempkt_ptr   = 0;
  ne2000->localpkt_ptr = 0;
  ne2000->address_cnt  = 0;

  memset( & ne2000->mem, 0, sizeof(ne2000->mem));

  // Set power-up conditions
  ne2000->CR.stop      = 1;
    ne2000->CR.rdma_cmd  = 4;
  ne2000->ISR.reset    = 1;
  ne2000->DCR.longaddr = 1;
  picint(1 << ne2000->base_irq);
  picintc(1 << ne2000->base_irq);
  //DEV_pic_lower_irq(ne2000->base_irq);
}

#include "bswap.h"

//
// read_cr/write_cr - utility routines for handling reads/writes to
// the Command Register
//
uint32_t ne2000_read_cr(ne2000_t *ne2000)
{
	uint32_t val;

	val = (((ne2000->CR.pgsel    & 0x03) << 6) |
	((ne2000->CR.rdma_cmd & 0x07) << 3) |
	(ne2000->CR.tx_packet << 2) |
	(ne2000->CR.start     << 1) |
	(ne2000->CR.stop));
	ne2000_log("%s: read CR returns 0x%02x\n", (network_card_current == 1) ? "NE2000" : "RTL8029AS", val);
	return val;
}

void ne2000_write_cr(ne2000_t *ne2000, uint32_t value)
{
	ne2000_log("%s: wrote 0x%02x to CR\n", (network_card_current == 1) ? "NE2000" : "RTL8029AS", value);

	// Validate remote-DMA
	if ((value & 0x38) == 0x00) {
		ne2000_log("CR write - invalid rDMA value 0\n");
		value |= 0x20; /* dma_cmd == 4 is a safe default */
	}

	// Check for s/w reset
	if (value & 0x01) {
		ne2000->ISR.reset = 1;
		ne2000->CR.stop   = 1;
	} else {
		ne2000->CR.stop = 0;
	}

	ne2000->CR.rdma_cmd = (value & 0x38) >> 3;

	// If start command issued, the RST bit in the ISR
	// must be cleared
	if ((value & 0x02) && !ne2000->CR.start) {
		ne2000->ISR.reset = 0;
	}

	ne2000->CR.start = ((value & 0x02) == 0x02);
	ne2000->CR.pgsel = (value & 0xc0) >> 6;

	// Check for send-packet command
	if (ne2000->CR.rdma_cmd == 3) {
		// Set up DMA read from receive ring
		ne2000->remote_start = ne2000->remote_dma = ne2000->bound_ptr * 256;
		ne2000->remote_bytes = (uint16_t) ne2000_chipmem_read(ne2000, ne2000->bound_ptr * 256 + 2, 2);
		ne2000_log("Sending buffer #x%x length %d\n", ne2000->remote_start, ne2000->remote_bytes);
	}

	// Check for start-tx
	if ((value & 0x04) && ne2000->TCR.loop_cntl) {
		if (ne2000->TCR.loop_cntl != 1) {
		  ne2000_log("Loop mode %d not supported\n", ne2000->TCR.loop_cntl);
		} else {
		  ne2000_rx_frame(ne2000, &ne2000->mem[ne2000->tx_page_start*256 - BX_NE2K_MEMSTART],
					ne2000->tx_bytes);
		}
	} else if (value & 0x04) {
		if (ne2000->CR.stop || (!ne2000->CR.start && (network_card_current == 1))) {
		  if (ne2000->tx_bytes == 0) /* njh@bandsman.co.uk */
			return; /* Solaris9 probe */
		  ne2000_log("CR write - tx start, dev in reset\n");
		}

		if (ne2000->tx_bytes == 0)
		  ne2000_log("CR write - tx start, tx bytes == 0\n");

		// Send the packet to the system driver
		ne2000->CR.tx_packet = 1;
		if(net_is_slirp) 
		{
			slirp_input(&ne2000->mem[ne2000->tx_page_start*256 - BX_NE2K_MEMSTART], ne2000->tx_bytes);
			ne2000_log("ne2000 slirp sending packet\n");
		}
		if(net_is_pcap && net_pcap!=NULL) 
		{
			_pcap_sendpacket(net_pcap, &ne2000->mem[ne2000->tx_page_start*256 - BX_NE2K_MEMSTART], ne2000->tx_bytes);
			ne2000_log("ne2000 pcap sending packet\n");
		}

		// some more debug
		if (ne2000->tx_timer_active)
		  ne2000_log("CR write, tx timer still active\n");

		ne2000_tx_event(ne2000, value);
	  }

	// Linux probes for an interrupt by setting up a remote-DMA read
	// of 0 bytes with remote-DMA completion interrupts enabled.
	// Detect this here
	if (ne2000->CR.rdma_cmd == 0x01 &&
		ne2000->CR.start &&
		ne2000->remote_bytes == 0) {
		ne2000->ISR.rdma_done = 1;
		if (ne2000->IMR.rdma_inte) {
		  picint(1 << ne2000->base_irq);
		}
	}
}

//
// chipmem_read/chipmem_write - access the 64K private RAM.
// The ne2000 memory is accessed through the data port of
// the asic (offset 0) after setting up a remote-DMA transfer.
// Both byte and word accesses are allowed.
// The first 16 bytes contains the MAC address at even locations,
// and there is 16K of buffer memory starting at 16K
//

uint32_t ne2000_chipmem_read(ne2000_t *ne2000, uint32_t address, unsigned int io_len)
{
  uint32_t retval = 0;

  if ((io_len == 2) && (address & 0x1))
    ne2000_log("unaligned chipmem word read\n");

  // ROM'd MAC address
  if ((address >=0) && (address <= 31)) {
    retval = ne2000->macaddr[address];
    if ((io_len == 2) || (io_len == 4)) {
      retval |= (ne2000->macaddr[address + 1] << 8);
    }
    if (io_len == 4) {
      retval |= (ne2000->macaddr[address + 2] << 16);
      retval |= (ne2000->macaddr[address + 3] << 24);
    }
    return (retval);
  }

  if ((address >= BX_NE2K_MEMSTART) && (address < BX_NE2K_MEMEND)) {
    retval = ne2000->mem[address - BX_NE2K_MEMSTART];
    if ((io_len == 2) || (io_len == 4)) {
      retval |= (ne2000->mem[address - BX_NE2K_MEMSTART + 1] << 8);
    }
    if (io_len == 4) {
      retval |= (ne2000->mem[address - BX_NE2K_MEMSTART + 2] << 16);
      retval |= (ne2000->mem[address - BX_NE2K_MEMSTART + 3] << 24);
    }
    return (retval);
  }

  ne2000_log("out-of-bounds chipmem read, %04X\n", address);

  return (0xff);
}

void ne2000_chipmem_write(ne2000_t *ne2000, uint32_t address, uint32_t value, unsigned io_len)
{
  if ((io_len == 2) && (address & 0x1))
    ne2000_log("unaligned chipmem word write\n");

  if ((address >= BX_NE2K_MEMSTART) && (address < BX_NE2K_MEMEND)) {
    ne2000->mem[address - BX_NE2K_MEMSTART] = value & 0xff;
    if ((io_len == 2) || (io_len == 4)) {
      ne2000->mem[address - BX_NE2K_MEMSTART + 1] = value >> 8;
    }
    if (io_len == 4) {
      ne2000->mem[address - BX_NE2K_MEMSTART + 2] = value >> 16;
      ne2000->mem[address - BX_NE2K_MEMSTART + 3] = value >> 24;
    }
  } else
    ne2000_log("out-of-bounds chipmem write, %04X\n", address);
}

//
// asic_read/asic_write - This is the high 16 bytes of i/o space
// (the lower 16 bytes is for the DS8390). Only two locations
// are used: offset 0, which is used for data transfer, and
// offset 0xf, which is used to reset the device.
// The data transfer port is used to as 'external' DMA to the
// DS8390. The chip has to have the DMA registers set up, and
// after that, insw/outsw instructions can be used to move
// the appropriate number of bytes to/from the device.
//
uint32_t ne2000_asic_read(ne2000_t *ne2000, uint32_t offset, unsigned int io_len)
{
  uint32_t retval = 0;

  switch (offset) {
  case 0x0:  // Data register
    //
    // A read remote-DMA command must have been issued,
    // and the source-address and length registers must
    // have been initialised.
    //
    if (io_len > ne2000->remote_bytes) {
      ne2000_log("dma read underrun iolen=%d remote_bytes=%d\n",io_len,ne2000->remote_bytes);
      //return 0;
    }

    ne2000_log("%s read DMA: addr=%4x remote_bytes=%d\n",(network_card_current == 1) ? "NE2000" : "RTL8029AS",ne2000->remote_dma,ne2000->remote_bytes);
    retval = ne2000_chipmem_read(ne2000, ne2000->remote_dma, io_len);
    //
    // The 8390 bumps the address and decreases the byte count
    // by the selected word size after every access, not by
    // the amount of data requested by the host (io_len).
    //
    if (io_len == 4) {
      ne2000->remote_dma += io_len;
    } else {
      ne2000->remote_dma += (ne2000->DCR.wdsize + 1);
    }
    if (ne2000->remote_dma == ne2000->page_stop << 8) {
      ne2000->remote_dma = ne2000->page_start << 8;
    }
    // keep s.remote_bytes from underflowing
    if (ne2000->remote_bytes > ne2000->DCR.wdsize)
      if (io_len == 4) {
        ne2000->remote_bytes -= io_len;
      } else {
        ne2000->remote_bytes -= (ne2000->DCR.wdsize + 1);
      }
    else
      ne2000->remote_bytes = 0;

    // If all bytes have been written, signal remote-DMA complete
    if (ne2000->remote_bytes == 0) {
      ne2000->ISR.rdma_done = 1;
      if (ne2000->IMR.rdma_inte) {
        picint(1 << ne2000->base_irq);
      }
    }
    break;

  case 0xf:  // Reset register
    ne2000_reset(ne2000, BX_RESET_SOFTWARE);
    break;

  default:
    ne2000_log("asic read invalid address %04x\n", (unsigned) offset);
    break;
  }

  return (retval);
}

void ne2000_asic_write(ne2000_t *ne2000, uint32_t offset, uint32_t value, unsigned io_len)
{
  ne2000_log("%s: asic write addr=0x%02x, value=0x%04x\n", (network_card_current == 1) ? "NE2000" : "RTL8029AS",(unsigned) offset, (unsigned) value);
  switch (offset) {
  case 0x0:  // Data register - see asic_read for a description

    if ((io_len > 1) && (ne2000->DCR.wdsize == 0)) {
      ne2000_log("dma write length %d on byte mode operation\n", io_len);
      break;
    }
    if (ne2000->remote_bytes == 0) {
      ne2000_log("dma write, byte count 0\n");
    }

    ne2000_chipmem_write(ne2000, ne2000->remote_dma, value, io_len);
    if (io_len == 4) {
      ne2000->remote_dma += io_len;
    } else {
      ne2000->remote_dma += (ne2000->DCR.wdsize + 1);
    }
    if (ne2000->remote_dma == ne2000->page_stop << 8) {
      ne2000->remote_dma = ne2000->page_start << 8;
    }

    if (io_len == 4) {
      ne2000->remote_bytes -= io_len;
    } else {
      ne2000->remote_bytes -= (ne2000->DCR.wdsize + 1);
    }
    if (ne2000->remote_bytes > BX_NE2K_MEMSIZ)
      ne2000->remote_bytes = 0;

    // If all bytes have been written, signal remote-DMA complete
    if (ne2000->remote_bytes == 0) {
      ne2000->ISR.rdma_done = 1;
      if (ne2000->IMR.rdma_inte) {
        picint(1 << ne2000->base_irq);
      }
    }
    break;

  case 0xf:  // Reset register
    // end of reset pulse
    break;

  default: // this is invalid, but happens under win95 device detection
    ne2000_log("asic write invalid address %04x, ignoring\n", (unsigned) offset);
    break;
  }
}

//
// page0_read/page0_write - These routines handle reads/writes to
// the 'zeroth' page of the DS8390 register file
//
uint32_t ne2000_page0_read(ne2000_t *ne2000, uint32_t offset, unsigned int io_len)
{
  uint8_t value = 0;

  if (io_len > 1) {
    ne2000_log("bad length! page 0 read from register 0x%02x, len=%u\n", offset,
              io_len); /* encountered with win98 hardware probe */
    return value;
  }

  switch (offset) {
  case 0x1:  // CLDA0
    value = (ne2000->local_dma & 0xff);
    break;

  case 0x2:  // CLDA1
    value = (ne2000->local_dma >> 8);
    break;

  case 0x3:  // BNRY
    value = ne2000->bound_ptr;
    break;

  case 0x4:  // TSR
    value = ((ne2000->TSR.ow_coll    << 7) |
             (ne2000->TSR.cd_hbeat   << 6) |
             (ne2000->TSR.fifo_ur    << 5) |
             (ne2000->TSR.no_carrier << 4) |
             (ne2000->TSR.aborted    << 3) |
             (ne2000->TSR.collided   << 2) |
             (ne2000->TSR.tx_ok));
    break;

  case 0x5:  // NCR
    value = ne2000->num_coll;
    break;

  case 0x6:  // FIFO
    // reading FIFO is only valid in loopback mode
    ne2000_log("reading FIFO not supported yet\n");
    value = ne2000->fifo;
    break;

  case 0x7:  // ISR
    value = ((ne2000->ISR.reset     << 7) |
             (ne2000->ISR.rdma_done << 6) |
             (ne2000->ISR.cnt_oflow << 5) |
             (ne2000->ISR.overwrite << 4) |
             (ne2000->ISR.tx_err    << 3) |
             (ne2000->ISR.rx_err    << 2) |
             (ne2000->ISR.pkt_tx    << 1) |
             (ne2000->ISR.pkt_rx));
    break;

  case 0x8:  // CRDA0
    value = (ne2000->remote_dma & 0xff);
    break;

  case 0x9:  // CRDA1
    value = (ne2000->remote_dma >> 8);
    break;

  case 0xa:  // reserved / RTL8029ID0
    if (network_card_current == 2) {
      value = 0x50;
    } else {
      ne2000_log("reserved read - page 0, 0xa\n");
      value = 0xff;
    }
    break;

  case 0xb:  // reserved / RTL8029ID1
    if (network_card_current == 2) {
      value = 0x43;
    } else {
      ne2000_log("reserved read - page 0, 0xb\n");
      value = 0xff;
    }
    break;

  case 0xc:  // RSR
    value = ((ne2000->RSR.deferred    << 7) |
             (ne2000->RSR.rx_disabled << 6) |
             (ne2000->RSR.rx_mbit     << 5) |
             (ne2000->RSR.rx_missed   << 4) |
             (ne2000->RSR.fifo_or     << 3) |
             (ne2000->RSR.bad_falign  << 2) |
             (ne2000->RSR.bad_crc     << 1) |
             (ne2000->RSR.rx_ok));
    break;

  case 0xd:  // CNTR0
    value = ne2000->tallycnt_0;
    break;

  case 0xe:  // CNTR1
    value = ne2000->tallycnt_1;
    break;

  case 0xf:  // CNTR2
    value = ne2000->tallycnt_2;
    break;

  default:
    ne2000_log("page 0 register 0x%02x out of range\n", offset);
	break;
  }

  ne2000_log("page 0 read from register 0x%02x, value=0x%02x\n", offset, value);
  return value;
}

void ne2000_page0_write(ne2000_t *ne2000, uint32_t offset, uint32_t value, unsigned io_len)
{
  uint8_t value2;

  // It appears to be a common practice to use outw on page0 regs...

  // break up outw into two outb's
  if (io_len == 2) {
    ne2000_page0_write(ne2000, offset, (value & 0xff), 1);
    if (offset < 0x0f) {
      ne2000_page0_write(ne2000, offset + 1, ((value >> 8) & 0xff), 1);
    }
    return;
  }

  ne2000_log("page 0 write to register 0x%02x, value=0x%02x\n", offset, value);

  switch (offset) {
  case 0x1:  // PSTART
    ne2000->page_start = value;
    break;

  case 0x2:  // PSTOP
    ne2000->page_stop = value;
    break;

  case 0x3:  // BNRY
    ne2000->bound_ptr = value;
    break;

  case 0x4:  // TPSR
    ne2000->tx_page_start = value;
    break;

  case 0x5:  // TBCR0
    // Clear out low byte and re-insert
    ne2000->tx_bytes &= 0xff00;
    ne2000->tx_bytes |= (value & 0xff);
    break;

  case 0x6:  // TBCR1
    // Clear out high byte and re-insert
    ne2000->tx_bytes &= 0x00ff;
    ne2000->tx_bytes |= ((value & 0xff) << 8);
    break;

  case 0x7:  // ISR
    value &= 0x7f;  // clear RST bit - status-only bit
    // All other values are cleared iff the ISR bit is 1
    ne2000->ISR.pkt_rx    &= ~((int)((value & 0x01) == 0x01));
    ne2000->ISR.pkt_tx    &= ~((int)((value & 0x02) == 0x02));
    ne2000->ISR.rx_err    &= ~((int)((value & 0x04) == 0x04));
    ne2000->ISR.tx_err    &= ~((int)((value & 0x08) == 0x08));
    ne2000->ISR.overwrite &= ~((int)((value & 0x10) == 0x10));
    ne2000->ISR.cnt_oflow &= ~((int)((value & 0x20) == 0x20));
    ne2000->ISR.rdma_done &= ~((int)((value & 0x40) == 0x40));
    value = ((ne2000->ISR.rdma_done << 6) |
             (ne2000->ISR.cnt_oflow << 5) |
             (ne2000->ISR.overwrite << 4) |
             (ne2000->ISR.tx_err    << 3) |
             (ne2000->ISR.rx_err    << 2) |
             (ne2000->ISR.pkt_tx    << 1) |
             (ne2000->ISR.pkt_rx));
    value &= ((ne2000->IMR.rdma_inte << 6) |
              (ne2000->IMR.cofl_inte << 5) |
              (ne2000->IMR.overw_inte << 4) |
              (ne2000->IMR.txerr_inte << 3) |
              (ne2000->IMR.rxerr_inte << 2) |
              (ne2000->IMR.tx_inte << 1) |
              (ne2000->IMR.rx_inte));
    if (value == 0)
      picintc(1 << ne2000->base_irq);
    break;

  case 0x8:  // RSAR0
    // Clear out low byte and re-insert
    ne2000->remote_start &= 0xff00;
    ne2000->remote_start |= (value & 0xff);
    ne2000->remote_dma = ne2000->remote_start;
    break;

  case 0x9:  // RSAR1
    // Clear out high byte and re-insert
    ne2000->remote_start &= 0x00ff;
    ne2000->remote_start |= ((value & 0xff) << 8);
    ne2000->remote_dma = ne2000->remote_start;
    break;

  case 0xa:  // RBCR0
    // Clear out low byte and re-insert
    ne2000->remote_bytes &= 0xff00;
    ne2000->remote_bytes |= (value & 0xff);
    break;

  case 0xb:  // RBCR1
    // Clear out high byte and re-insert
    ne2000->remote_bytes &= 0x00ff;
    ne2000->remote_bytes |= ((value & 0xff) << 8);
    break;

  case 0xc:  // RCR
    // Check if the reserved bits are set
    if (value & 0xc0)
      ne2000_log("RCR write, reserved bits set\n");

    // Set all other bit-fields
    ne2000->RCR.errors_ok = ((value & 0x01) == 0x01);
    ne2000->RCR.runts_ok  = ((value & 0x02) == 0x02);
    ne2000->RCR.broadcast = ((value & 0x04) == 0x04);
    ne2000->RCR.multicast = ((value & 0x08) == 0x08);
    ne2000->RCR.promisc   = ((value & 0x10) == 0x10);
    ne2000->RCR.monitor   = ((value & 0x20) == 0x20);

    // Monitor bit is a little suspicious...
    if (value & 0x20)
      ne2000_log("RCR write, monitor bit set!\n");
    break;

  case 0xd:  // TCR
    // Check reserved bits
    if (value & 0xe0)
      ne2000_log("TCR write, reserved bits set\n");

    // Test loop mode (not supported)
    if (value & 0x06) {
      ne2000->TCR.loop_cntl = (value & 0x6) >> 1;
      ne2000_log("TCR write, loop mode %d not supported\n", ne2000->TCR.loop_cntl);
    } else {
      ne2000->TCR.loop_cntl = 0;
    }

    // Inhibit-CRC not supported.
    if (value & 0x01)
      ne2000_log("TCR write, inhibit-CRC not supported\n");

    // Auto-transmit disable very suspicious
    if (value & 0x08)
      ne2000_log("TCR write, auto transmit disable not supported\n");

    // Allow collision-offset to be set, although not used
    ne2000->TCR.coll_prio = ((value & 0x08) == 0x08);
    break;

  case 0xe:  // DCR
    // the loopback mode is not suppported yet
    if (!(value & 0x08)) {
      ne2000_log("DCR write, loopback mode selected\n");
    }
    // It is questionable to set longaddr and auto_rx, since they
    // aren't supported on the ne2000. Print a warning and continue
    if (value & 0x04)
      ne2000_log("DCR write - LAS set ???\n");
    if (value & 0x10)
      ne2000_log("DCR write - AR set ???\n");

    // Set other values.
    ne2000->DCR.wdsize   = ((value & 0x01) == 0x01);
    ne2000->DCR.endian   = ((value & 0x02) == 0x02);
    ne2000->DCR.longaddr = ((value & 0x04) == 0x04); // illegal ?
    ne2000->DCR.loop     = ((value & 0x08) == 0x08);
    ne2000->DCR.auto_rx  = ((value & 0x10) == 0x10); // also illegal ?
    ne2000->DCR.fifo_size = (value & 0x50) >> 5;
    break;

  case 0xf:  // IMR
    // Check for reserved bit
    if (value & 0x80)
      ne2000_log("IMR write, reserved bit set\n");

    // Set other values
    ne2000->IMR.rx_inte    = ((value & 0x01) == 0x01);
    ne2000->IMR.tx_inte    = ((value & 0x02) == 0x02);
    ne2000->IMR.rxerr_inte = ((value & 0x04) == 0x04);
    ne2000->IMR.txerr_inte = ((value & 0x08) == 0x08);
    ne2000->IMR.overw_inte = ((value & 0x10) == 0x10);
    ne2000->IMR.cofl_inte  = ((value & 0x20) == 0x20);
    ne2000->IMR.rdma_inte  = ((value & 0x40) == 0x40);
    value2 = ((ne2000->ISR.rdma_done << 6) |
              (ne2000->ISR.cnt_oflow << 5) |
              (ne2000->ISR.overwrite << 4) |
              (ne2000->ISR.tx_err    << 3) |
              (ne2000->ISR.rx_err    << 2) |
              (ne2000->ISR.pkt_tx    << 1) |
              (ne2000->ISR.pkt_rx));
    if (((value & value2) & 0x7f) == 0) {
      picintc(1 << ne2000->base_irq);
    } else {
      picint(1 << ne2000->base_irq);
    }
    break;

  default:
    ne2000_log("page 0 write, bad register 0x%02x\n", offset);
	break;
  }
}

//
// page1_read/page1_write - These routines handle reads/writes to
// the first page of the DS8390 register file
//
uint32_t ne2000_page1_read(ne2000_t *ne2000, uint32_t offset, unsigned int io_len)
{
  ne2000_log("page 1 read from register 0x%02x, len=%u\n", offset, io_len);

  switch (offset) {
  case 0x1:  // PAR0-5
  case 0x2:
  case 0x3:
  case 0x4:
  case 0x5:
  case 0x6:
    return (ne2000->physaddr[offset - 1]);
    break;

  case 0x7:  // CURR
    ne2000_log("returning current page: 0x%02x\n", (ne2000->curr_page));
    return (ne2000->curr_page);

  case 0x8:  // MAR0-7
  case 0x9:
  case 0xa:
  case 0xb:
  case 0xc:
  case 0xd:
  case 0xe:
  case 0xf:
    return (ne2000->mchash[offset - 8]);
    break;

  default:
    ne2000_log("page 1 read register 0x%02x out of range\n", offset);
	break;
  }

  return (0);
}

void ne2000_page1_write(ne2000_t *ne2000, uint32_t offset, uint32_t value, unsigned io_len)
{
  ne2000_log("page 1 write to register 0x%02x, len=%u, value=0x%04x\n", offset,
            io_len, value);

  switch (offset) {
  case 0x1:  // PAR0-5
  case 0x2:
  case 0x3:
  case 0x4:
  case 0x5:
  case 0x6:
    ne2000->physaddr[offset - 1] = value;
    if (offset == 6) {
      ne2000_log("Physical address set to %02x:%02x:%02x:%02x:%02x:%02x\n",
               ne2000->physaddr[0],
               ne2000->physaddr[1],
               ne2000->physaddr[2],
               ne2000->physaddr[3],
               ne2000->physaddr[4],
               ne2000->physaddr[5]);
    }
    break;

  case 0x7:  // CURR
    ne2000->curr_page = value;
    break;

  case 0x8:  // MAR0-7
  case 0x9:
  case 0xa:
  case 0xb:
  case 0xc:
  case 0xd:
  case 0xe:
  case 0xf:
    ne2000->mchash[offset - 8] = value;
    break;

  default:
    ne2000_log("page 1 write register 0x%02x out of range\n", offset);
	break;
  }
}

//
// page2_read/page2_write - These routines handle reads/writes to
// the second page of the DS8390 register file
//
uint32_t ne2000_page2_read(ne2000_t *ne2000, uint32_t offset, unsigned int io_len)
{
  ne2000_log("page 2 read from register 0x%02x, len=%u\n", offset, io_len);
  
  switch (offset) {
  case 0x1:  // PSTART
    return (ne2000->page_start);

  case 0x2:  // PSTOP
    return (ne2000->page_stop);

  case 0x3:  // Remote Next-packet pointer
    return (ne2000->rempkt_ptr);

  case 0x4:  // TPSR
    return (ne2000->tx_page_start);

  case 0x5:  // Local Next-packet pointer
    return (ne2000->localpkt_ptr);

  case 0x6:  // Address counter (upper)
    return (ne2000->address_cnt >> 8);

  case 0x7:  // Address counter (lower)
    return (ne2000->address_cnt & 0xff);

  case 0x8:  // Reserved
  case 0x9:
  case 0xa:
  case 0xb:
    ne2000_log("reserved read - page 2, register 0x%02x\n", offset);
    return (0xff);

  case 0xc:  // RCR
    return ((ne2000->RCR.monitor   << 5) |
	    (ne2000->RCR.promisc   << 4) |
	    (ne2000->RCR.multicast << 3) |
	    (ne2000->RCR.broadcast << 2) |
	    (ne2000->RCR.runts_ok  << 1) |
	    (ne2000->RCR.errors_ok));

  case 0xd:  // TCR
    return ((ne2000->TCR.coll_prio   << 4) |
	    (ne2000->TCR.ext_stoptx  << 3) |
	    ((ne2000->TCR.loop_cntl & 0x3) << 1) |
	    (ne2000->TCR.crc_disable));

  case 0xe:  // DCR
    return (((ne2000->DCR.fifo_size & 0x3) << 5) |
	    (ne2000->DCR.auto_rx  << 4) |
	    (ne2000->DCR.loop     << 3) |
	    (ne2000->DCR.longaddr << 2) |
	    (ne2000->DCR.endian   << 1) |
	    (ne2000->DCR.wdsize));

  case 0xf:  // IMR
    return ((ne2000->IMR.rdma_inte  << 6) |
	    (ne2000->IMR.cofl_inte  << 5) |
	    (ne2000->IMR.overw_inte << 4) |
	    (ne2000->IMR.txerr_inte << 3) |
	    (ne2000->IMR.rxerr_inte << 2) |
	    (ne2000->IMR.tx_inte    << 1) |
	    (ne2000->IMR.rx_inte));

  default:
    ne2000_log("page 2 register 0x%02x out of range\n", offset);
	break;
  }

  return (0);
}

void ne2000_page2_write(ne2000_t *ne2000, uint32_t offset, uint32_t value, unsigned io_len)
{
  // Maybe all writes here should be BX_PANIC()'d, since they
  // affect internal operation, but let them through for now
  // and print a warning.
  ne2000_log("page 2 write to register 0x%02x, len=%u, value=0x%04x\n", offset,
            io_len, value);

  switch (offset) {
  case 0x1:  // CLDA0
    // Clear out low byte and re-insert
    ne2000->local_dma &= 0xff00;
    ne2000->local_dma |= (value & 0xff);
    break;

  case 0x2:  // CLDA1
    // Clear out high byte and re-insert
    ne2000->local_dma &= 0x00ff;
    ne2000->local_dma |= ((value & 0xff) << 8);
    break;

  case 0x3:  // Remote Next-pkt pointer
    ne2000->rempkt_ptr = value;
    break;

  case 0x4:
    ne2000_log("page 2 write to reserved register 0x04\n");
    break;

  case 0x5:  // Local Next-packet pointer
    ne2000->localpkt_ptr = value;
    break;

  case 0x6:  // Address counter (upper)
    // Clear out high byte and re-insert
    ne2000->address_cnt &= 0x00ff;
    ne2000->address_cnt |= ((value & 0xff) << 8);
    break;

  case 0x7:  // Address counter (lower)
    // Clear out low byte and re-insert
    ne2000->address_cnt &= 0xff00;
    ne2000->address_cnt |= (value & 0xff);
    break;

  case 0x8:
  case 0x9:
  case 0xa:
  case 0xb:
  case 0xc:
  case 0xd:
  case 0xe:
  case 0xf:
    ne2000_log("page 2 write to reserved register 0x%02x\n", offset);
    break;

  default:
    ne2000_log("page 2 write, illegal register 0x%02x\n", offset);
    break;
  }
}

//
// page3_read/page3_write - writes to this page are illegal
//
uint32_t ne2000_page3_read(ne2000_t *ne2000, uint32_t offset, unsigned int io_len)
{
  if (network_card_current == 2) {
    switch (offset) {
      case 0x3:  // CONFIG0
        return (0);
      case 0x5:  // CONFIG2
        return (0x40);
      case 0x6:  // CONFIG3
        return (0x40);
      default:
        ne2000_log("page 3 read register 0x%02x attempted\n", offset);
        return (0);
    }
  } else {
    ne2000_log("page 3 read register 0x%02x attempted\n", offset);
    return (0);
  }
}

void ne2000_page3_write(ne2000_t *ne2000, uint32_t offset, uint32_t value, unsigned io_len)
{
  ne2000_log("page 3 write register 0x%02x attempted\n", offset);
  return;
}

void ne2000_tx_timer(void *p)
{
  ne2000_t *ne2000 = (ne2000_t *)p;
  ne2000_log("tx_timer\n");
  ne2000->CR.tx_packet = 0;
  ne2000->TSR.tx_ok = 1;
  ne2000->ISR.pkt_tx = 1;
  // Generate an interrupt if not masked
  if (ne2000->IMR.tx_inte) {
    picint(1 << ne2000->base_irq);
  }
  ne2000->tx_timer_active = 0;
}

void ne2000_tx_event(void *p, uint32_t val)
{
    ne2000_t *ne2000 = (ne2000_t *)p;
	ne2000_tx_timer(ne2000);
}

//
// read_handler/read - i/o 'catcher' function called from BOCHS
// mainline when the CPU attempts a read in the i/o space registered
// by this ne2000 instance
//
uint32_t ne2000_read(ne2000_t *ne2000, uint32_t address, unsigned io_len)
{
  ne2000_log("%s: read addr %x, len %d\n", (network_card_current == 1) ? "NE2000" : "RTL8029AS", address, io_len);
  uint32_t retval = 0;
  int offset = address - ne2000->base_address;

  if (offset >= 0x10) {
    retval = ne2000_asic_read(ne2000, offset - 0x10, io_len);
  } else if (offset == 0x00) {
    retval = ne2000_read_cr(ne2000);
  } else {
    switch (ne2000->CR.pgsel) {
    case 0x00:
      retval = ne2000_page0_read(ne2000, offset, io_len);
      break;

    case 0x01:
      retval = ne2000_page1_read(ne2000, offset, io_len);
      break;

    case 0x02:
      retval = ne2000_page2_read(ne2000, offset, io_len);
      break;

    case 0x03:
      retval = ne2000_page3_read(ne2000, offset, io_len);
      break;

    default:
      ne2000_log("unknown value of pgsel in read - %d\n", ne2000->CR.pgsel);
	  break;
    }
  }

  return (retval);
}

void ne2000_write(ne2000_t *ne2000, uint32_t address, uint32_t value, unsigned io_len)
{
  ne2000_log("%s: write addr %x, value %x len %d\n", (network_card_current == 1) ? "NE2000" : "RTL8029AS", address, value, io_len);
  int offset = address - ne2000->base_address;

  //
  // The high 16 bytes of i/o space are for the ne2000 asic -
  //  the low 16 bytes are for the DS8390, with the current
  //  page being selected by the PS0,PS1 registers in the
  //  command register
  //
  if (offset >= 0x10) {
    ne2000_asic_write(ne2000, offset - 0x10, value, io_len);
  } else if (offset == 0x00) {
    ne2000_write_cr(ne2000, value);
  } else {
    switch (ne2000->CR.pgsel) {
    case 0x00:
	ne2000_page0_write(ne2000, offset, value, io_len);
	break;

    case 0x01:
      ne2000_page1_write(ne2000, offset, value, io_len);
      break;

    case 0x02:
      ne2000_page2_write(ne2000, offset, value, io_len);
      break;

    case 0x03:
      ne2000_page3_write(ne2000, offset, value, io_len);
      break;

    default:
      ne2000_log("unknown value of pgsel in write - %d\n", ne2000->CR.pgsel);
	  break;
    }
  }
}

/*
 * mcast_index() - return the 6-bit index into the multicast
 * table. Stolen unashamedly from FreeBSD's if_ed.c
 */
static int mcast_index(const void *dst)
{
#define POLYNOMIAL 0x04c11db6
  unsigned long crc = 0xffffffffL;
  int carry, i, j;
  unsigned char b;
  unsigned char *ep = (unsigned char *) dst;

  for (i = 6; --i >= 0;) {
    b = *ep++;
    for (j = 8; --j >= 0;) {
      carry = ((crc & 0x80000000L) ? 1 : 0) ^ (b & 0x01);
      crc <<= 1;
      b >>= 1;
      if (carry)
        crc = ((crc ^ POLYNOMIAL) | carry);
    }
  }
  return (crc >> 26);
#undef POLYNOMIAL
}

/*
 * rx_frame() - called by the platform-specific code when an
 * ethernet frame has been received. The destination address
 * is tested to see if it should be accepted, and if the
 * rx ring has enough room, it is copied into it and
 * the receive process is updated
 */
void ne2000_rx_frame(void *p, const void *buf, int io_len)
{
  ne2000_t *ne2000 = (ne2000_t *)p;

  int pages;
  int avail;
  int idx;
  int wrapped;
  int nextpage;
  uint8_t pkthdr[4];
  uint8_t *pktbuf = (uint8_t *) buf;
  uint8_t *startptr;
  static uint8_t bcast_addr[6] = {0xff,0xff,0xff,0xff,0xff,0xff};

  if(io_len != 60) {
        ne2000_log("rx_frame with length %d\n", io_len);
  }

        //LOG_MSG("stop=%d, pagestart=%x, dcr_loop=%x, tcr_loopcntl=%x",
        //      ne2000->CR.stop, ne2000->page_start,
        //      ne2000->DCR.loop, ne2000->TCR.loop_cntl);
  if ((ne2000->CR.stop != 0) ||
      (ne2000->page_start == 0) /*||
      ((ne2000->DCR.loop == 0) &&
       (ne2000->TCR.loop_cntl != 0))*/) {
    return;
  }

  // Add the pkt header + CRC to the length, and work
  // out how many 256-byte pages the frame would occupy
  pages = (io_len + 4 + 4 + 255)/256;

  if (ne2000->curr_page < ne2000->bound_ptr) {
    avail = ne2000->bound_ptr - ne2000->curr_page;
  } else {
    avail = (ne2000->page_stop - ne2000->page_start) -
      (ne2000->curr_page - ne2000->bound_ptr);
    wrapped = 1;
  }

  // Avoid getting into a buffer overflow condition by not attempting
  // to do partial receives. The emulation to handle this condition
  // seems particularly painful.
  if ((avail < pages)
#if BX_NE2K_NEVER_FULL_RING
      || (avail == pages)
#endif
      ) {
        ne2000_log("no space\n");
    return;
  }

  if ((io_len < 40/*60*/) && !ne2000->RCR.runts_ok) {
    ne2000_log("rejected small packet, length %d\n", io_len);
    return;
  }
  // some computers don't care...
  if (io_len < 60) io_len=60;

  // Do address filtering if not in promiscuous mode
  if (! ne2000->RCR.promisc) {
    if (!memcmp(buf, bcast_addr, 6)) {
      if (!ne2000->RCR.broadcast) {
        return;
      }
    } else if (pktbuf[0] & 0x01) {
        if (! ne2000->RCR.multicast) {
            return;
        }
      idx = mcast_index(buf);
      if (!(ne2000->mchash[idx >> 3] & (1 << (idx & 0x7)))) {
        return;
      }
    } else if (0 != memcmp(buf, ne2000->physaddr, 6)) {
      return;
    }
  } else {
      ne2000_log("rx_frame promiscuous receive\n");
  }

    ne2000_log("rx_frame %d to %x:%x:%x:%x:%x:%x from %x:%x:%x:%x:%x:%x\n",
           io_len,
           pktbuf[0], pktbuf[1], pktbuf[2], pktbuf[3], pktbuf[4], pktbuf[5],
           pktbuf[6], pktbuf[7], pktbuf[8], pktbuf[9], pktbuf[10], pktbuf[11]);

  nextpage = ne2000->curr_page + pages;
  if (nextpage >= ne2000->page_stop) {
    nextpage -= ne2000->page_stop - ne2000->page_start;
  }

  // Setup packet header
  pkthdr[0] = 0;        // rx status - old behavior
  pkthdr[0] = 1;        // Probably better to set it all the time
                        // rather than set it to 0, which is clearly wrong.
  if (pktbuf[0] & 0x01) {
    pkthdr[0] |= 0x20;  // rx status += multicast packet
  }
  pkthdr[1] = nextpage; // ptr to next packet
  pkthdr[2] = (io_len + 4) & 0xff;      // length-low
  pkthdr[3] = (io_len + 4) >> 8;        // length-hi

  // copy into buffer, update curpage, and signal interrupt if config'd
  startptr = & ne2000->mem[ne2000->curr_page * 256 -
                               BX_NE2K_MEMSTART];
  if ((nextpage > ne2000->curr_page) ||
      ((ne2000->curr_page + pages) == ne2000->page_stop)) {
    memcpy(startptr, pkthdr, 4);
    memcpy(startptr + 4, buf, io_len);
    ne2000->curr_page = nextpage;
  } else {
    int endbytes = (ne2000->page_stop - ne2000->curr_page)
      * 256;
    memcpy(startptr, pkthdr, 4);
    memcpy(startptr + 4, buf, endbytes - 4);
    startptr = & ne2000->mem[ne2000->page_start * 256 -
                                 BX_NE2K_MEMSTART];
    memcpy(startptr, (void *)(pktbuf + endbytes - 4),
           io_len - endbytes + 8);
    ne2000->curr_page = nextpage;
  }

  ne2000->RSR.rx_ok = 1;
  if (pktbuf[0] & 0x80) {
    ne2000->RSR.rx_mbit = 1;
  }

  ne2000->ISR.pkt_rx = 1;

  if (ne2000->IMR.rx_inte) {
        //LOG_MSG("packet rx interrupt");
          picint(1 << ne2000->base_irq);
    //DEV_pic_raise_irq(ne2000->base_irq);
  } //else LOG_MSG("no packet rx interrupt");

}

uint8_t ne2000_readb(uint16_t addr, void *p)
{
	ne2000_t *ne2000 = (ne2000_t *)p;
	return ne2000_read(ne2000, addr, 1);
}

uint16_t ne2000_readw(uint16_t addr, void *p)
{
	ne2000_t *ne2000 = (ne2000_t *)p;
	if (ne2000->DCR.wdsize & 1)
		return ne2000_read(ne2000, addr, 2);
	else
		return ne2000_read(ne2000, addr, 1);		
}

uint32_t ne2000_readl(uint16_t addr, void *p)
{
	ne2000_t *ne2000 = (ne2000_t *)p;
	return ne2000_read(ne2000, addr, 4);	
}

void ne2000_writeb(uint16_t addr, uint8_t val, void *p)
{
	ne2000_t *ne2000 = (ne2000_t *)p;
	ne2000_write(ne2000, addr, val, 1);
}

void ne2000_writew(uint16_t addr, uint16_t val, void *p)
{
	ne2000_t *ne2000 = (ne2000_t *)p;
	if (ne2000->DCR.wdsize & 1)
		ne2000_write(ne2000, addr, val, 2);
	else
		ne2000_write(ne2000, addr, val, 1);
}

void ne2000_writel(uint16_t addr, uint32_t val, void *p)
{
	ne2000_t *ne2000 = (ne2000_t *)p;
	ne2000_write(ne2000, addr, val, 4);
}

void ne2000_poller(void *p)
{
    ne2000_t *ne2000 = (ne2000_t *)p;
    struct queuepacket *qp;
    const unsigned char *data;
    struct pcap_pkthdr h;


        int res;
if(net_is_slirp) {
        while(QueuePeek(slirpq)>0)
                {
                qp=QueueDelete(slirpq);
                if((ne2000->DCR.loop == 0) || (ne2000->TCR.loop_cntl != 0))
                        {
                        free(qp);
                        return;
                        }
                ne2000_rx_frame(ne2000,&qp->data,qp->len); 
                ne2000_log("ne2000 inQ:%d  got a %dbyte packet @%d\n",QueuePeek(slirpq),qp->len,qp);
                free(qp);
                }
        fizz++;
        if(fizz>1200){fizz=0;slirp_tic();}
        }//end slirp
if(net_is_pcap  && net_pcap!=NULL)
        {
        data=_pcap_next(net_pcap,&h);
        if(data==0x0){goto WTF;}
        if((memcmp(data+6,maclocal,6))==0)
            ne2000_log("ne2000 we just saw ourselves\n");
        else {
             if((ne2000->DCR.loop == 0) || (ne2000->TCR.loop_cntl != 0))
                {
                return;
                }
                ne2000_log("ne2000 pcap received a frame %d bytes\n",h.caplen);
                ne2000_rx_frame(ne2000,data,h.caplen); 
             }
        WTF:
                {}
        }
}

typedef union
{
	uint32_t addr;
	uint8_t addr_regs[4];
} bar_t;

uint8_t ne2000_pci_regs[256];

bar_t ne2000_pci_bar[2];

uint32_t bios_addr = 0xD0000;
uint32_t old_base_addr = 0;

uint32_t bios_size = 0;
uint32_t bios_mask = 0;

void ne2000_io_set(uint16_t addr, ne2000_t *ne2000)
{	
		old_base_addr = addr;
		io_sethandler(addr, 0x0010, ne2000_readb, ne2000_readw, ne2000_readl, ne2000_writeb, ne2000_writew, ne2000_writel, ne2000);
		io_sethandler(addr+0x10, 0x0010, ne2000_readb, ne2000_readw, ne2000_readl, ne2000_writeb, ne2000_writew, ne2000_writel, ne2000);
		io_sethandler(addr+0x1f, 0x0001, ne2000_readb, ne2000_readw, ne2000_readl, ne2000_writeb, ne2000_writew, ne2000_writel, ne2000);
}

void ne2000_io_remove(int16_t addr, ne2000_t *ne2000)
{
		io_removehandler(addr, 0x0010, ne2000_readb, ne2000_readw, ne2000_readl, ne2000_writeb, ne2000_writew, ne2000_writel, ne2000);
		io_removehandler(addr+0x10, 0x0010, ne2000_readb, ne2000_readw, ne2000_readl, ne2000_writeb, ne2000_writew, ne2000_writel, ne2000);
		io_removehandler(addr+0x1f, 0x0001, ne2000_readb, ne2000_readw, ne2000_readl, ne2000_writeb, ne2000_writew, ne2000_writel, ne2000);
}

uint8_t ne2000_pci_read(int func, int addr, void *p)
{
        ne2000_t *ne2000 = (ne2000_t *) p;

        // ne2000_log("NE2000 PCI read %08X\n", addr);
        switch (addr)
        {
                case 0x00:/* case 0x2C:*/ return 0xec;
                case 0x01:/* case 0x2D:*/ return 0x10;

                case 0x02:/* case 0x2E:*/ return 0x29;
                case 0x03:/* case 0x2F:*/ return 0x80;

                case 0x2C: return 0xF4;
                case 0x2D: return 0x1A;
                case 0x2E: return 0x00;
                case 0x2F: return 0x11;

                case 0x04:
                return ne2000_pci_regs[0x04]; /*Respond to IO and memory accesses*/
                case 0x05:
                return ne2000_pci_regs[0x05];

                case 0x07: return 2;

                case 0x08: return 0; /*Revision ID*/
                case 0x09: return 0; /*Programming interface*/

                case 0x0B: return ne2000_pci_regs[0x0B];

                case 0x10: return 1; /*I/O space*/
                case 0x11: return ne2000_pci_bar[0].addr_regs[1];
                case 0x12: return ne2000_pci_bar[0].addr_regs[2];
                case 0x13: return ne2000_pci_bar[0].addr_regs[3];

                case 0x30: return ne2000_pci_bar[1].addr_regs[0] & 0x01; /*BIOS ROM address*/
                // case 0x31: return (ne2000_pci_bar[1].addr_regs[1] & 0xE0) | 0x18;
                case 0x31: return (ne2000_pci_bar[1].addr_regs[1] & bios_mask) | 0x18;
                case 0x32: return ne2000_pci_bar[1].addr_regs[2];
                case 0x33: return ne2000_pci_bar[1].addr_regs[3];

                case 0x3C: return ne2000_pci_regs[0x3C];
                case 0x3D: return ne2000_pci_regs[0x3D];
        }
        return 0;
}

void ne2000_update_bios(ne2000_t *ne2000)
{
	int reg_bios_enable;
	
	// reg_bios_enable = ne2000_pci_regs[0x30];
	reg_bios_enable = 1;
	
	/* PCI BIOS stuff, just enable_disable. */
	if (!disable_netbios && reg_bios_enable)
	{
		mem_mapping_enable(&ne2000->bios_rom.mapping);
		mem_mapping_set_addr(&ne2000->bios_rom.mapping, bios_addr, 0x10000);
		ne2000_log("Network BIOS now at: %08X\n", bios_addr);
	}
	else
	{
		mem_mapping_disable(&ne2000->bios_rom.mapping);
		if (network_card_current == 2)  ne2000_pci_bar[1].addr = 0;
	}
}

void ne2000_pci_write(int func, int addr, uint8_t val, void *p)
{
        ne2000_t *ne2000 = (ne2000_t *) p;

        // ne2000_log("ne2000_pci_write: addr=%02x val=%02x\n", addr, val);
        switch (addr)
        {
                case 0x04:
                if (val & PCI_COMMAND_IO)
				{
					ne2000_io_remove(ne2000->base_address, ne2000);
					ne2000_io_set(ne2000->base_address, ne2000);
				}
                else
				{
					ne2000_io_remove(ne2000->base_address, ne2000);
				}
				ne2000_pci_regs[addr] = val;
                break;

				case 0x10:
				val &= 0xfc;
				val |= 1;
                case 0x11: case 0x12: case 0x13:
                /* I/O Base set. */
                /* First, remove the old I/O, if old base was >= 0x280. */
                ne2000_io_remove(ne2000->base_address, ne2000);
                /* Then let's set  the PCI regs. */
				ne2000_pci_bar[0].addr_regs[addr & 3] = val;
                /* Then let's calculate the new I/O base. */
                ne2000->base_address = ne2000_pci_bar[0].addr & 0xff00;
                /* If the base is below 0x280, return. */
                /* Log the new base. */
                ne2000_log("NE2000 RTL8029AS PCI: New I/O base is %04X\n" , ne2000->base_address);
                /* We're done, so get out of the here. */
                return;
				
                case 0x30: case 0x31: case 0x32: case 0x33:
                ne2000_pci_bar[1].addr_regs[addr & 3] = val;
				ne2000_pci_bar[1].addr_regs[1] &= bios_mask;
				bios_addr = ne2000_pci_bar[1].addr & 0xffffe000;
				ne2000_pci_bar[1].addr &= 0xffffe000;
				ne2000_pci_bar[1].addr |= 0x1801;
				ne2000_update_bios(ne2000);
                return;

                case 0x3C:
                ne2000_pci_regs[addr] = val;
                if (val != 0xFF)
                {
						ne2000_log("NE2000 IRQ now: %i\n", val);
                        ne2000_setirq(ne2000, val);
                }
                return;
        }
}

void ne2000_rom_init(ne2000_t *ne2000, char *s)
{
	FILE *f = fopen(s, "rb");
	uint32_t temp;
	if(!f)
	{
		disable_netbios = 1;
		ne2000_update_bios(ne2000);
		return;
	}
	fseek(f, 0, SEEK_END);
	temp = ftell(f);
	fclose(f);
	bios_size = 0x10000;
	if (temp <= 0x8000)
	{
		bios_size = 0x8000;
	}
	if (temp <= 0x4000)
	{
		bios_size = 0x4000;
	}
	if (temp <= 0x2000)
	{
		bios_size = 0x2000;
	}
	bios_mask = (bios_size >> 8) & 0xff;
	bios_mask = (0x100 - bios_mask) & 0xff;

	rom_init(&ne2000->bios_rom, s, 0xd0000, bios_size, bios_size - 1, 0, MEM_MAPPING_EXTERNAL);
}

void *ne2000_init()
{
    int rc;
	int config_net_type;
    int net_type;
    ne2000_t *ne2000 = malloc(sizeof(ne2000_t));
    memset(ne2000, 0, sizeof(ne2000_t));

	ne2000->base_address = device_get_config_int("addr");
	disable_netbios = device_get_config_int("disable_netbios");
    ne2000_setirq(ne2000, device_get_config_int("irq"));

    //net_type
    //0 pcap
    //1 slirp
    //
	config_net_type = device_get_config_int("net_type");
    // net_is_slirp = config_get_int(NULL, "net_type", 1);
	/* Network type is now specified in device config. */
	net_is_slirp = config_net_type ? 1 : 0;
    // ne2000_log("ne2000 pcap device %s\n",config_get_string(NULL,"pcap_device","nothing"));
    
    //Check that we have a string setup, otherwise turn pcap off
    if(!strcmp("nothing",config_get_string(NULL,"pcap_device","nothing"))) {
        net_is_pcap = 0;
        }
    else {
    if( net_is_slirp == 0) 
        net_is_pcap = 1;
    }

    ne2000_io_set(ne2000->base_address, ne2000);
	memcpy(ne2000->physaddr, maclocal, 6);

	if (!disable_netbios)
		ne2000_rom_init(ne2000, "roms/ne2000.rom");

    ne2000_reset(ne2000, BX_RESET_HARDWARE);
    vlan_handler(ne2000_poller, ne2000);

    ne2000_log("ne2000 isa init 0x%X %d\tslirp is %d net_is_pcap is %d\n",ne2000->base_address,device_get_config_int("irq"),net_is_slirp,net_is_pcap);

    //need a switch statment for more network types.

    if ( net_is_slirp ) {
    ne2000_log("ne2000 initalizing SLiRP\n");
    net_is_pcap=0;
    rc=slirp_init();
    ne2000_log("ne2000 slirp_init returned: %d\n",rc);
    if ( rc == 0 )
        {
        ne2000_log("ne2000 slirp initalized!\n");

        net_slirp_inited=1;
        slirpq = QueueCreate();
        net_is_slirp=1;
        fizz=0;
        ne2000_log("ne2000 slirpq is %x\n",&slirpq);
        }
    else {
        net_slirp_inited=0;
        net_is_slirp=0;
        }
    }
    if ( net_is_pcap ) {        //pcap
         char errbuf[32768];

        ne2000_log("ne2000 initalizing libpcap\n");
        net_is_slirp=0;
         net_hLib = LoadLibraryA(net_lib_name);
            if(net_hLib==0)
            {
            ne2000_log("ne2000 Failed to load %s\n",net_lib_name);
            net_is_pcap=0;
            //return;
            }
        _pcap_lib_version =(PCAP_LIB_VERSION)GetProcAddress(net_hLib,"pcap_lib_version");
        _pcap_open_live=(PCAP_OPEN_LIVE)GetProcAddress(net_hLib,"pcap_open_live");              
        _pcap_sendpacket=(PCAP_SENDPACKET)GetProcAddress(net_hLib,"pcap_sendpacket");           
        _pcap_setnonblock=(PCAP_SETNONBLOCK)GetProcAddress(net_hLib,"pcap_setnonblock");        
        _pcap_next=(PCAP_NEXT)GetProcAddress(net_hLib,"pcap_next");             
        _pcap_close=(PCAP_CLOSE)GetProcAddress(net_hLib,"pcap_close");
        _pcap_getnonblock=(PCAP_GETNONBLOCK)GetProcAddress(net_hLib,"pcap_getnonblock");
        _pcap_compile=(PCAP_COMPILE)GetProcAddress(net_hLib,"pcap_compile");
        _pcap_setfilter=(PCAP_SETFILTER)GetProcAddress(net_hLib,"pcap_setfilter");   
    
        if(_pcap_lib_version && _pcap_open_live && _pcap_sendpacket && _pcap_setnonblock && _pcap_next && _pcap_close && _pcap_getnonblock)
                {
                ne2000_log("ne2000 Pcap version [%s]\n",_pcap_lib_version());

                //if((net_pcap=_pcap_open_live("\\Device\\NPF_{0CFA803F-F443-4BB9-A83A-657029A98195}",1518,1,15,errbuf))==0)
                if((net_pcap=_pcap_open_live(config_get_string(NULL,"pcap_device","nothing"),1518,1,15,errbuf))==0)
                        {
                        ne2000_log("ne2000 pcap_open_live error on %s!\n",config_get_string(NULL,"pcap_device","whatever the ethernet is"));
                        net_is_pcap=0; return(ne2000);  //YUCK!!!
                        }
                }
                else    {       
                ne2000_log("%d %d %d %d %d %d %d\n",_pcap_lib_version, _pcap_open_live,_pcap_sendpacket,_pcap_setnonblock,_pcap_next,_pcap_close,_pcap_getnonblock);
                        net_is_pcap=1;
                        }

                //Time to check that we are in non-blocking mode.
                rc=_pcap_getnonblock(net_pcap,errbuf);
                ne2000_log("ne2000 pcap is currently in %s mode\n",rc? "non-blocking":"blocking");
                switch(rc)
                {
                case 0:
                        ne2000_log("ne2000 Setting interface to non-blocking mode..");
                        rc=_pcap_setnonblock(net_pcap,1,errbuf);
                        if(rc==0)       {  //no errors!
                                        ne2000_log("..");
                                        rc=_pcap_getnonblock(net_pcap,errbuf);
                                        if(rc==1)       {
                                                ne2000_log("..!",rc);
                                                net_is_pcap=1;
                                                }
                                        else{
                                                ne2000_log("\tunable to set pcap into non-blocking mode!\nContinuining without pcap.\n");
                                                net_is_pcap=0;
                                            }
                                        }//end set nonblock
                        else{ne2000_log("There was an unexpected error of [%s]\n\nexiting.\n",errbuf);net_is_pcap=0;}
                        ne2000_log("\n");
                        break;
                case 1:
                        ne2000_log("non blocking\n");
                        break;
                default:
                        ne2000_log("this isn't right!!!\n");
                        net_is_pcap=0;
                        break;
                }
    if( net_is_pcap ) {
                if(_pcap_compile && _pcap_setfilter) {  //we can do this!
                struct bpf_program fp;
                char filter_exp[255];
                ne2000_log("ne2000 Building packet filter...");
                sprintf(filter_exp,"( ((ether dst ff:ff:ff:ff:ff:ff) or (ether dst %02x:%02x:%02x:%02x:%02x:%02x)) and not (ether src %02x:%02x:%02x:%02x:%02x:%02x) )", \
                maclocal[0], maclocal[1], maclocal[2], maclocal[3], maclocal[4], maclocal[5],\
                maclocal[0], maclocal[1], maclocal[2], maclocal[3], maclocal[4], maclocal[5]);

                //I'm doing a MAC level filter so TCP/IP doesn't matter.
                if (_pcap_compile(net_pcap, &fp, filter_exp, 0, 0xffffffff) == -1) {
                        ne2000_log("\nne2000 Couldn't compile filter\n");
                        }
                        else    {
                        ne2000_log("...");
                        if (_pcap_setfilter(net_pcap, &fp) == -1) {
                        ne2000_log("\nError installing pcap filter.\n");
                                }//end of set_filter failure
                        else    {
                                ne2000_log("...!\n");
                                }
                        }
                ne2000_log("ne2000 Using filter\t[%s]\n",filter_exp);
                //scanf(filter_exp);    //pause
                }
                else
                {
                ne2000_log("ne2000 Your platform lacks pcap_compile & pcap_setfilter\n");
                net_is_pcap=0;
                }
        ne2000_log("ne2000 net_is_pcap is %d and net_pcap is %x\n",net_is_pcap,net_pcap);
        }
    } //end pcap setup

    //timer_add(slirp_tic,&delay,TIMER_ALWAYS_ENABLED,NULL);
    //timer_add(keyboard_amstrad_poll, &keybsenddelay, TIMER_ALWAYS_ENABLED,  NULL);
ne2000_log("ne2000 is_slirp %d is_pcap %d\n",net_is_slirp,net_is_pcap);
//exit(0);
return ne2000;
}

void *rtl8029as_init()
{
    int rc;
	int config_net_type;
    int net_type;
    ne2000_t *ne2000 = malloc(sizeof(ne2000_t));
    memset(ne2000, 0, sizeof(ne2000_t));
	
	disable_netbios = device_get_config_int("disable_netbios");
	ne2000_setirq(ne2000, (ide_ter_enabled ? 11 : 10));

    //net_type
    //0 pcap
    //1 slirp
    //
	config_net_type = device_get_config_int("net_type");
    // net_is_slirp = config_get_int(NULL, "net_type", 1);
	/* Network type is now specified in device config. */
	net_is_slirp = config_net_type ? 1 : 0;
    // ne2000_log("ne2000 pcap device %s\n",config_get_string(NULL,"pcap_device","nothing"));
    
    //Check that we have a string setup, otherwise turn pcap off
    if(!strcmp("nothing",config_get_string(NULL,"pcap_device","nothing"))) {
        net_is_pcap = 0;
        }
    else {
    if( net_is_slirp == 0) 
        net_is_pcap = 1;
    }

        pci_add(ne2000_pci_read, ne2000_pci_write, ne2000);
	
	if (!disable_netbios)
	{
		ne2000_rom_init(ne2000, "roms/rtl8029as.rom");

		if (PCI) mem_mapping_disable(&ne2000->bios_rom.mapping);
	}
		
        ne2000_pci_regs[0x04] = 1;
        ne2000_pci_regs[0x05] = 0;

        ne2000_pci_regs[0x07] = 2;

        /* Network controller. */
        ne2000_pci_regs[0x0B] = 2;
		
		ne2000_pci_bar[0].addr_regs[0] = 1;
		
		if (disable_netbios)
		{
			ne2000_pci_bar[1].addr = 0;
			bios_addr = 0;
		}
		else
		{
			ne2000_pci_bar[1].addr = 0x000F8000;
			ne2000_pci_bar[1].addr_regs[1] = bios_mask;
			ne2000_pci_bar[1].addr |= 0x1801;
			bios_addr = 0xD0000;
		}

        ne2000_pci_regs[0x3C] = ide_ter_enabled ? 11 : 10;
        ne2000_pci_regs[0x3D] = 1;

        memset(rtl8029as_eeprom, 0, 128);
        rtl8029as_eeprom[0x76] = rtl8029as_eeprom[0x7A] = rtl8029as_eeprom[0x7E] = 0x29;
        rtl8029as_eeprom[0x77] = rtl8029as_eeprom[0x7B] = rtl8029as_eeprom[0x7F] = 0x80;
        rtl8029as_eeprom[0x78] = rtl8029as_eeprom[0x7C] = 0x10;
        rtl8029as_eeprom[0x79] = rtl8029as_eeprom[0x7D] = 0xEC;
	
		ne2000->base_address = 0x340;
		ne2000_io_set(ne2000->base_address, ne2000);	
	
	memcpy(ne2000->physaddr, maclocal, 6);

    ne2000_reset(ne2000, BX_RESET_HARDWARE);
    vlan_handler(ne2000_poller, ne2000);

    ne2000_log("ne2000 pci init 0x%X\tslirp is %d net_is_pcap is %d\n",ne2000->base_address,net_is_slirp,net_is_pcap);

    //need a switch statment for more network types.

    if ( net_is_slirp ) {
    ne2000_log("ne2000 initalizing SLiRP\n");
    net_is_pcap=0;
    rc=slirp_init();
    ne2000_log("ne2000 slirp_init returned: %d\n",rc);
    if ( rc == 0 )
        {
        ne2000_log("ne2000 slirp initalized!\n");

        net_slirp_inited=1;
        slirpq = QueueCreate();
        net_is_slirp=1;
        fizz=0;
        ne2000_log("ne2000 slirpq is %x\n",&slirpq);
        }
    else {
        net_slirp_inited=0;
        net_is_slirp=0;
        }
    }
    if ( net_is_pcap ) {        //pcap
         char errbuf[32768];

        ne2000_log("ne2000 initalizing libpcap\n");
        net_is_slirp=0;
         net_hLib = LoadLibraryA(net_lib_name);
            if(net_hLib==0)
            {
            ne2000_log("ne2000 Failed to load %s\n",net_lib_name);
            net_is_pcap=0;
            //return;
            }
        _pcap_lib_version =(PCAP_LIB_VERSION)GetProcAddress(net_hLib,"pcap_lib_version");
        _pcap_open_live=(PCAP_OPEN_LIVE)GetProcAddress(net_hLib,"pcap_open_live");              
        _pcap_sendpacket=(PCAP_SENDPACKET)GetProcAddress(net_hLib,"pcap_sendpacket");           
        _pcap_setnonblock=(PCAP_SETNONBLOCK)GetProcAddress(net_hLib,"pcap_setnonblock");        
        _pcap_next=(PCAP_NEXT)GetProcAddress(net_hLib,"pcap_next");             
        _pcap_close=(PCAP_CLOSE)GetProcAddress(net_hLib,"pcap_close");
        _pcap_getnonblock=(PCAP_GETNONBLOCK)GetProcAddress(net_hLib,"pcap_getnonblock");
        _pcap_compile=(PCAP_COMPILE)GetProcAddress(net_hLib,"pcap_compile");
        _pcap_setfilter=(PCAP_SETFILTER)GetProcAddress(net_hLib,"pcap_setfilter");   
    
        if(_pcap_lib_version && _pcap_open_live && _pcap_sendpacket && _pcap_setnonblock && _pcap_next && _pcap_close && _pcap_getnonblock)
                {
                ne2000_log("ne2000 Pcap version [%s]\n",_pcap_lib_version());

                //if((net_pcap=_pcap_open_live("\\Device\\NPF_{0CFA803F-F443-4BB9-A83A-657029A98195}",1518,1,15,errbuf))==0)
                if((net_pcap=_pcap_open_live(config_get_string(NULL,"pcap_device","nothing"),1518,1,15,errbuf))==0)
                        {
                        ne2000_log("ne2000 pcap_open_live error on %s!\n",config_get_string(NULL,"pcap_device","whatever the ethernet is"));
                        net_is_pcap=0; return(ne2000);  //YUCK!!!
                        }
                }
                else    {       
                ne2000_log("%d %d %d %d %d %d %d\n",_pcap_lib_version, _pcap_open_live,_pcap_sendpacket,_pcap_setnonblock,_pcap_next,_pcap_close,_pcap_getnonblock);
                        net_is_pcap=1;
                        }

                //Time to check that we are in non-blocking mode.
                rc=_pcap_getnonblock(net_pcap,errbuf);
                ne2000_log("ne2000 pcap is currently in %s mode\n",rc? "non-blocking":"blocking");
                switch(rc)
                {
                case 0:
                        ne2000_log("ne2000 Setting interface to non-blocking mode..");
                        rc=_pcap_setnonblock(net_pcap,1,errbuf);
                        if(rc==0)       {  //no errors!
                                        ne2000_log("..");
                                        rc=_pcap_getnonblock(net_pcap,errbuf);
                                        if(rc==1)       {
                                                ne2000_log("..!",rc);
                                                net_is_pcap=1;
                                                }
                                        else{
                                                ne2000_log("\tunable to set pcap into non-blocking mode!\nContinuining without pcap.\n");
                                                net_is_pcap=0;
                                            }
                                        }//end set nonblock
                        else{ne2000_log("There was an unexpected error of [%s]\n\nexiting.\n",errbuf);net_is_pcap=0;}
                        ne2000_log("\n");
                        break;
                case 1:
                        ne2000_log("non blocking\n");
                        break;
                default:
                        ne2000_log("this isn't right!!!\n");
                        net_is_pcap=0;
                        break;
                }
    if( net_is_pcap ) {
                if(_pcap_compile && _pcap_setfilter) {  //we can do this!
                struct bpf_program fp;
                char filter_exp[255];
                ne2000_log("ne2000 Building packet filter...");
                sprintf(filter_exp,"( ((ether dst ff:ff:ff:ff:ff:ff) or (ether dst %02x:%02x:%02x:%02x:%02x:%02x)) and not (ether src %02x:%02x:%02x:%02x:%02x:%02x) )", \
                maclocal[0], maclocal[1], maclocal[2], maclocal[3], maclocal[4], maclocal[5],\
                maclocal[0], maclocal[1], maclocal[2], maclocal[3], maclocal[4], maclocal[5]);

                //I'm doing a MAC level filter so TCP/IP doesn't matter.
                if (_pcap_compile(net_pcap, &fp, filter_exp, 0, 0xffffffff) == -1) {
                        ne2000_log("\nne2000 Couldn't compile filter\n");
                        }
                        else    {
                        ne2000_log("...");
                        if (_pcap_setfilter(net_pcap, &fp) == -1) {
                        ne2000_log("\nError installing pcap filter.\n");
                                }//end of set_filter failure
                        else    {
                                ne2000_log("...!\n");
                                }
                        }
                ne2000_log("ne2000 Using filter\t[%s]\n",filter_exp);
                //scanf(filter_exp);    //pause
                }
                else
                {
                ne2000_log("ne2000 Your platform lacks pcap_compile & pcap_setfilter\n");
                net_is_pcap=0;
                }
        ne2000_log("ne2000 net_is_pcap is %d and net_pcap is %x\n",net_is_pcap,net_pcap);
        }
    } //end pcap setup

    //timer_add(slirp_tic,&delay,TIMER_ALWAYS_ENABLED,NULL);
    //timer_add(keyboard_amstrad_poll, &keybsenddelay, TIMER_ALWAYS_ENABLED,  NULL);
ne2000_log("ne2000 is_slirp %d is_pcap %d\n",net_is_slirp,net_is_pcap);
//exit(0);
return ne2000;
}


void ne2000_close(void *p)
{
        ne2000_t *ne2000 = (ne2000_t *)p;
        ne2000_io_remove(ne2000->base_address, ne2000);
        free(ne2000);
		
		if(net_is_slirp) {
				QueueDestroy(slirpq);
				slirp_exit(0);
				net_slirp_inited=0;
				ne2000_log("ne2000 exiting slirp\n");
				}
		if(net_is_pcap && net_pcap!=NULL)
				{
				_pcap_close(net_pcap);
				FreeLibrary(net_hLib);
				ne2000_log("ne2000 closing pcap\n");
				}
		ne2000_log("ne2000 close\n");
}

static device_config_t ne2000_config[] =
{
        {
                .name = "addr",
                .description = "Address",
                .type = CONFIG_BINARY,
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "0x280",
                                .value = 0x280
                        },
                        {
                                .description = "0x300",
                                .value = 0x300
                        },
                        {
                                .description = "0x320",
                                .value = 0x320
                        },
                        {
                                .description = "0x340",
                                .value = 0x340
                        },
                        {
                                .description = "0x360",
                                .value = 0x360
                        },
                        {
                                .description = "0x380",
                                .value = 0x380
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 0x300
        },
        {
                .name = "irq",
                .description = "IRQ",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "IRQ 3",
                                .value = 3
                        },
                        {
                                .description = "IRQ 5",
                                .value = 5
                        },
                        {
                                .description = "IRQ 7",
                                .value = 7
                        },
                        {
                                .description = "IRQ 10",
                                .value = 10
                        },
                        {
                                .description = "IRQ 11",
                                .value = 11
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 10
        },
        {
                .name = "net_type",
                .description = "Network type",
                .type = CONFIG_BINARY,
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "PCap",
                                .value = 0
                        },
                        {
                                .description = "SLiRP",
                                .value = 1
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 0
        },
        {
                .name = "disable_netbios",
                .description = "Network bios",
                .type = CONFIG_BINARY,
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "Enabled",
                                .value = 0
                        },
                        {
                                .description = "Disabled",
                                .value = 1
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 0
        },
        {
                .type = -1
        }
};

static device_config_t rtl8029as_config[] =
{
        {
                .name = "net_type",
                .description = "Network type",
                .type = CONFIG_BINARY,
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "PCap",
                                .value = 0
                        },
                        {
                                .description = "SLiRP",
                                .value = 1
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 0
        },
        {
                .name = "disable_netbios",
                .description = "Network bios",
                .type = CONFIG_BINARY,
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "Enabled",
                                .value = 0
                        },
                        {
                                .description = "Disabled",
                                .value = 1
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 0
        },
        {
                .type = -1
        }
};

device_t ne2000_device =
{
        "Novell NE2000",
        0,
        ne2000_init,
        ne2000_close,
        NULL,
        NULL,
        NULL,
        NULL,
        ne2000_config
};

device_t rtl8029as_device =
{
        "Realtek RTL8029AS",
        0,
        rtl8029as_init,
        ne2000_close,
        NULL,
        NULL,
        NULL,
        NULL,
        rtl8029as_config
};


//SLIRP stuff
int slirp_can_output(void)
{
return net_slirp_inited;
}

void slirp_output (const unsigned char *pkt, int pkt_len)
{
struct queuepacket *p;
p=(struct queuepacket *)malloc(sizeof(struct queuepacket));
p->len=pkt_len;
memcpy(p->data,pkt,pkt_len);
QueueEnter(slirpq,p);
ne2000_log("ne2000 slirp_output %d @%d\n",pkt_len,p);
}

// Instead of calling this and crashing some times
// or experencing jitter, this is called by the 
// 60Hz clock which seems to do the job.
void slirp_tic()
{
       int ret2,nfds;
        struct timeval tv;
        fd_set rfds, wfds, xfds;
        int timeout;
        nfds=-1;

      if(net_slirp_inited)
              {
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        FD_ZERO(&xfds);
       timeout=slirp_select_fill(&nfds,&rfds,&wfds,&xfds); //this can crash

        if(timeout<0)
                timeout=500;
       tv.tv_sec=0;
       tv.tv_usec = timeout;    //basilisk default 10000

       ret2 = select(nfds + 1, &rfds, &wfds, &xfds, &tv);
        if(ret2>=0){
       slirp_select_poll(&rfds, &wfds, &xfds);
          }
        //ne2000_log("ne2000 slirp_tic()\n");
       }//end if slirp inited
}
