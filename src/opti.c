/*OPTi 82C495 emulation
  This is the chipset used in the AMI386 model*/
#include "ibm.h"

uint8_t optiregs[0x10];
int optireg;

void writeopti(uint16_t addr, uint8_t val)
{
        switch (addr)
        {
                case 0x22:
                optireg=val;
                break;
                case 0x24:
                printf("Writing OPTI reg %02X %02X\n",optireg,val);
                if (optireg>=0x20 && optireg<=0x2C) optiregs[optireg-0x20]=val;
                break;
        }
}

uint8_t readopti(uint16_t addr)
{
        switch (addr)
        {
                case 0x24:
                printf("Read OPTI reg %02X\n",optireg);
                if (optireg>=0x20 && optireg<=0x2C) return optiregs[optireg-0x20];
                break;
        }
        return 0xFF;
}

/*Details for the chipset from Ralph Brown's interrupt list
  This describes the OPTi 82C493, the 82C495 seems similar except there is one
  more register (2C)
  
----------P00220024--------------------------
PORT 0022-0024 - OPTi 82C493 System Controller (SYSC) - CONFIGURATION REGISTERS
Desc:	The OPTi 486SXWB contains three chips and is designed for systems
	  running at 20, 25 and 33MHz.	The chipset includes an 82C493 System
	  Controller (SYSC), the 82C392 Data Buffer Controller, and the
	  82C206 Integrated peripheral Controller (IPC).
Note:	every access to PORT 0024h must be preceded by a write to PORT 0022h,
	  even if the same register is being accessed a second time
SeeAlso: PORT 0022h"82C206"

0022  ?W  configuration register index (see #P0178)
0024  RW  configuration register data

(Table P0178)
Values for OPTi 82C493 System Controller configuration register index:
 20h	Control Register 1 (see #P0179)
 21h	Control Register 2 (see #P0180)
 22h	Shadow RAM Control Register 1 (see #P0181)
 23h	Shadow RAM Control Register 2 (see #P0182)
 24h	DRAM Control Register 1 (see #P0183)
 25h	DRAM Control Register 2 (see #P0184)
 26h	Shadow RAM Control Register 3 (see #P0185)
 27h	Control Register 3 (see #P0186)
 28h	Non-cachable Block 1 Register 1 (see #P0187)
 29h	Non-cachable Block 1 Register 2 (see #P0188)
 2Ah	Non-cachable Block 2 Register 1 (see #P0187)
 2Bh	Non-cachable Block 2 Register 2 (see #P0188)

Bitfields for OPTi-82C493 Control Register 1:
Bit(s)	Description	(Table P0179)
 7-6	Revision of 82C493 (readonly) (default=01)
 5	Burst wait state control
	1 = Secondary cache read hit cycle is 3-2-2-2 or 2-2-2-2
	0 = Secondary cache read hit cycle is 3-1-1-1 or 2-1-1-1 (default)
	(if bit 5 is set to 1, bit 4 must be set to 0)
 4	Cache memory data buffer output enable control
	0 = disable (default)
	1 = enable
	(must be disabled for frequency <= 33Mhz)
 3	Single Address Latch Enable (ALE)
	0 = disable (default)
	1 = enable
	(if enabled, SYSC will activate single ALE rather than multiples
	  during bus conversion cycles)
 2	enable Extra AT Cycle Wait State (default is 0 = disabled)
 1	Emulation keyboard Reset Control
	0 = disable (default)
	1 = enable
	Note:	This bit must be enabled in BIOS default value; enabling this
		  bit requires HALT instruction to be executed before SYSC
		  generates processor reset (CPURST)
 0	enable Alternative Fast Reset (default is 0 = disabled)
SeeAlso: #P0180,#P0186

Bitfields for OPTi-82C493 Control Register 2:
Bit(s)	Description	(Table P0180)
 7	Master Mode Byte Swap Enable
	0 = disable (default)
	1 = enable
 6	Emulation Keyboard Reset Delay Control
	0 = Generate reset pulse 2us later (default)
	1 = Generate reset pulse immediately
 5	disable Parity Check (default is 0 = enabled)
 4	Cache Enable
	0 = Cache disabled and DRAM burst mode enabled (default)
	1 = Cache enabled and DRAM burst mode disabled
 3-2	Cache Size
	00  64KB (default)
	01  128KB
	10  256KB
	11  512KB
 1	Secondary Cache Read Burst Cycles Control
	0 = 3-1-1-1 cycle (default)
	1 = 2-1-1-1 cycle
 0	Cache Write Wait State Control
	0 = 1 wait state (default)
	1 = 0 wait state
SeeAlso: #P0179,#P0186

Bitfields for OPTi-82C493 Shadow RAM Control Register 1:
Bit(s)	Description	(Table P0181)
 7	ROM(F0000h - FFFFFh) Enable
	0 = read/write on write-protected DRAM
	1 = read from ROM, write to DRAM (default)
 6	Shadow RAM at D0000h - EFFFFh Area
	0 = disable (default)
	1 = enable
 5	Shadow RAM at E0000h - EFFFFh Area
	0 = disable shadow RAM (default)
	    E0000h - EFFFFh ROM is defaulted to reside on XD bus
	1 = enable shadow RAM
 4	enable write-protect for Shadow RAM at D0000h - DFFFFh Area
	0 = disable (default)
	1 = enable
 3	enable write-protect for Shadow RAM at E0000h - EFFFFh Area
	0 = disable (default)
	1 = enable
 2	Hidden refresh enable (with holding CPU)
	(Hidden refresh must be disabled if 4Mx1 or 1M x4 bit DRAM are used)
	1 = disable (default)
	0 = enable
 1	unused
 0	enable Slow Refresh (four times slower than normal refresh)
	(default is 0 = disable)
SeeAlso: #P0182

Bitfields for OPTi-82C493 Shadow RAM Control Register 2:
Bit(s)	Description	(Table P0182)
 7	enable Shadow RAM at EC000h - EFFFFh area
 6	enable Shadow RAM at E8000h - EBFFFh area
 5	enable Shadow RAM at E4000h - E7FFFh area
 4	enable Shadow RAM at E0000h - E3FFFh area
 3	enable Shadow RAM at DC000h - DFFFFh area
 2	enable Shadow RAM at D8000h - DBFFFh area
 1	enable Shadow RAM at D4000h - D7FFFh area
 0	enable Shadow RAM at D0000h - D3FFFh area
Note:	the default is disabled (0) for all areas

Bitfields for OPTi-82C493 DRAM Control Register 1:
Bit(s)	Description	(Table P0183)
 7	DRAM size
	0 = 256K DRAM mode
	1 = 1M and 4M DRAM mode
 6-4	DRAM types used for bank0 and bank1
	bits 7-4  Bank0	  Bank1
	0000	  256K	     x
	0001	  256K	  256K
	0010	  256K	    1M
	0011	     x	     x
	01xx	     x	     x
	1000	    1M	     x	(default)
	1001	    1M	    1M
	1010	    1M	    4M
	1011	    4M	    1M
	1100	    4M	     x
	1101	    4M	    4M
	111x	     x	     x
 3	unused
 2-0	DRAM types used for bank2 and bank3
	bits 7,2-0  Bank2  Bank3
	x000	   1M	    x
	x001	   1M	   1M
	x010	    x	    x
	x011	   4M	   1M
	x100	   4M	    x
	x101	   4M	   4M
	x11x	    x	    x  (default)
SeeAlso: #P0184

Bitfields for OPTi-82C493 DRAM Control Register 2:
Bit(s)	Description	(Table P0184)
 7-6	Read cycle additional wait states
	00 not used
	01 = 0
	10 = 1
	11 = 2 (default)
 5-4	Write cycle additional wait states
	00 = 0
	01 = 1
	10 = 2
	11 = 3 (default)
 3	Fast decode enable
	0 = disable fast decode. DRAM base wait states not changed (default)
	1 = enable fast decode. DRAM base wait state is decreased by 1
	Note:	This function may be enabled in 20/25Mhz operation to speed up
		  DRAM access.	If bit 4 of index register 21h (cache enable
		  bit) is enabled, this bit is automatically disabled--even if
		  set to 1
 2	unused
 1-0	ATCLK selection
	00  ATCLK = CLKI/6 (default)
	01  ATCLK = CLKI/4 (default)
	10  ATCLK = CLKI/3
	11  ATCLK = CLK2I/5  (CLKI * 2 /5)
	Note:	bit 0 will reflect the BCLKS (pin 142) status and bit 1 will be
		  set to 0 when 82C493 is reset.
SeeAlso: #P0183,#P0185

Bitfields for OPTi-82C493 Shadow RAM Control Register 3:
Bit(s)	Description	(Table P0185)
 7	unused
 6	Shadow RAM copy enable for address C0000h - CFFFFh
	0 = Read/write at AT bus (default)
	1 = Read from AT bus and write into shadow RAM
 5	Shadow write protect at address C0000h - CFFFFh
	0 = Write protect disable (default)
	1 = Write protect enable
 4	enable Shadow RAM at C0000h - CFFFFh
 3	enable Shadow RAM at CC000h - CFFFFh
 2	enable Shadow RAM at C8000h - CBFFFh
 1	enable Shadow RAM at C4000h - C7FFFh
 0	enable Shadow RAM at C0000h - C3FFFh
Note:	the default is disabled (0) for bits 4-0
SeeAlso: #P0183,#P0184

Bitfields for OPTi-82C493 Control Register 3:
Bit(s)	Description	(Table P0186)
 7	enable NCA# pin to low state (default is 1 = enabled)
 6-5	unused
 4	Video BIOS at C0000h - C8000h non-cacheable
	0 = cacheable
	1 = non-cacheable (default)
 3-0	Cacheable address range for local memory
	0000  0 - 64MB
	0001  0 - 4MB (default)
	0010  0 - 8MB
	0011  0 - 12MB
	0100  0 - 16MB
	0101  0 - 20MB
	0110  0 - 24MB
	0111  0 - 28MB
	1000  0 - 32MB
	1001  0 - 36MB
	1010  0 - 40MB
	1011  0 - 44MB
	1100  0 - 48MB
	1101  0 - 52MB
	1110  0 - 56MB
	1111  0 - 60MB
	Note:	If total memory is 1MB or 2MB the cacheable range is 0-1 MB or
		  0-2 MB and independent of the value of bits 3-0
SeeAlso: #P0179,#P0180

Bitfields for OPTi-82C493 Non-cacheable Block Register 1:
Bit(s)	Description	(Table P0187)
 7-5	Size of non-cachable memory block
	000  64K
	001  128K
	010  256K
	011  512K
	1xx  disabled (default)
 4-2	unused
 1-0	Address bits 25 and 24 of non-cachable memory block (default = 00)
Note:	this register is used together with configuration register 29h
	  (non-cacheable block 1) or register 2Bh (block 2) (see #P0188) to
	  define a non-cacheable block.	 The starting address must be a
	  multiple of the block size
SeeAlso: #P0178,#P0188

Bitfields for OPTi-82C493 Non-cacheable Block Register 2:
Bit(s)	Description	(Table P0188)
 7-0	Address bits 23-16 of non-cachable memory block (default = 0001xxxx)
Note:	the block address is forced to be a multiple of the block size by
	  ignoring the appropriate number of the least-significant bits
SeeAlso: #P0178,#P0187

*/
