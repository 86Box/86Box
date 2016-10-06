// ID
#define CIRRUS_ID_CLGD6225  (0x02<<2)
#define CIRRUS_ID_CLGD5402  (0x06<<2)		/* Also Acumos AVGA2 */
#define CIRRUS_ID_CLGD7542  (0x0B<<2)		/* Nordic */
#define CIRRUS_ID_CLGD7543  (0x0C<<2)		/* Viking - guess */
#define CIRRUS_ID_CLGD7541  (0x0D<<2)		/* Nordic Lite */
#define CIRRUS_ID_CLGD6215  (0x12<<2)
#define CIRRUS_ID_CLGD6235  (0x22<<2)		/* Also 5402 */
#define CIRRUS_ID_CLGD5422  (0x23<<2)
#define CIRRUS_ID_CLGD5426  (0x24<<2)
#define CIRRUS_ID_CLGD5424  (0x25<<2)		/* Also 5422-80 */
#define CIRRUS_ID_CLGD5428  (0x26<<2)
#define CIRRUS_ID_CLGD5429  (0x27<<2)
#define CIRRUS_ID_CLGD5430  (0x28<<2)
#define CIRRUS_ID_CLGD5432  0xA2
#define CIRRUS_ID_CLGD5434O (0x29<<2)		/* O = obsolete, never used? */
#define CIRRUS_ID_CLGD5434  (0x2A<<2)
#define CIRRUS_ID_CLGD5436  (0x2B<<2)
#define CIRRUS_ID_CLGD5436U (0x3A<<2)
#define CIRRUS_ID_CLGD5440  (0x2C<<2)
#define CIRRUS_ID_CLGD5442  (0x2D<<2)
#define CIRRUS_ID_CLGD5446  (0x2E<<2)
#define CIRRUS_ID_CLGD5455  (0x2F<<2)		/* Laguna 3D */
#define CIRRUS_ID_CLGD6205  (0x32<<2)		/* Laguna 3D */
#define CIRRUS_ID_CLGD5462  (0x34<<2)		/* Laguna */
#define CIRRUS_ID_CLGD5464  (0xD5)		/* Laguna BD */
#define CIRRUS_ID_CLGD5465  (0xD6)		/* Laguna 3D */

// sequencer 0x07
#define CIRRUS_SR7_BPP_VGA            0x00
#define CIRRUS_SR7_BPP_SVGA           0x01
#define CIRRUS_SR7_BPP_MASK           0x0e
#define CIRRUS_SR7_BPP_8              0x00
#define CIRRUS_SR7_BPP_16_DOUBLEVCLK  0x02
#define CIRRUS_SR7_BPP_24             0x04
#define CIRRUS_SR7_BPP_16             0x06
#define CIRRUS_SR7_BPP_32             0x08
#define CIRRUS_SR7_ISAADDR_MASK       0xe0

// sequencer 0x0f
#define CIRRUS_MEMSIZE_512k        0x08
#define CIRRUS_MEMSIZE_1M          0x10
#define CIRRUS_MEMSIZE_2M          0x18
#define CIRRUS_MEMFLAGS_BANKSWITCH 0x80	// bank switching is enabled.

// sequencer 0x12
#define CIRRUS_CURSOR_SHOW         0x01
#define CIRRUS_CURSOR_HIDDENPEL    0x02
#define CIRRUS_CURSOR_LARGE        0x04	// 64x64 if set, 32x32 if clear

// sequencer 0x17
#define CIRRUS_BUSTYPE_VLBFAST   0x10
#define CIRRUS_BUSTYPE_PCI       0x20
#define CIRRUS_BUSTYPE_VLBSLOW   0x30
#define CIRRUS_BUSTYPE_ISA       0x38
#define CIRRUS_MMIO_ENABLE       0x04
#define CIRRUS_MMIO_USE_PCIADDR  0x40	// 0xb8000 if cleared.
#define CIRRUS_MEMSIZEEXT_DOUBLE 0x80

// control 0x0b
#define CIRRUS_BANKING_DUAL             0x01
#define CIRRUS_BANKING_GRANULARITY_16K  0x20	// set:16k, clear:4k

// control 0x30
#define CIRRUS_BLTMODE_BACKWARDS        0x01
#define CIRRUS_BLTMODE_MEMSYSDEST       0x02
#define CIRRUS_BLTMODE_MEMSYSSRC        0x04
#define CIRRUS_BLTMODE_TRANSPARENTCOMP  0x08
#define CIRRUS_BLTMODE_PATTERNCOPY      0x40
#define CIRRUS_BLTMODE_COLOREXPAND      0x80
#define CIRRUS_BLTMODE_PIXELWIDTHMASK   0x30
#define CIRRUS_BLTMODE_PIXELWIDTH8      0x00
#define CIRRUS_BLTMODE_PIXELWIDTH16     0x10
#define CIRRUS_BLTMODE_PIXELWIDTH24     0x20
#define CIRRUS_BLTMODE_PIXELWIDTH32     0x30

// control 0x31
#define CIRRUS_BLT_BUSY                 0x01
#define CIRRUS_BLT_START                0x02
#define CIRRUS_BLT_RESET                0x04
#define CIRRUS_BLT_FIFOUSED             0x10
#define CIRRUS_BLT_AUTOSTART            0x80

// control 0x32
#define CIRRUS_ROP_0                    0x00
#define CIRRUS_ROP_SRC_AND_DST          0x05
#define CIRRUS_ROP_NOP                  0x06
#define CIRRUS_ROP_SRC_AND_NOTDST       0x09
#define CIRRUS_ROP_NOTDST               0x0b
#define CIRRUS_ROP_SRC                  0x0d
#define CIRRUS_ROP_1                    0x0e
#define CIRRUS_ROP_NOTSRC_AND_DST       0x50
#define CIRRUS_ROP_SRC_XOR_DST          0x59
#define CIRRUS_ROP_SRC_OR_DST           0x6d
#define CIRRUS_ROP_NOTSRC_OR_NOTDST     0x90
#define CIRRUS_ROP_SRC_NOTXOR_DST       0x95
#define CIRRUS_ROP_SRC_OR_NOTDST        0xad
#define CIRRUS_ROP_NOTSRC               0xd0
#define CIRRUS_ROP_NOTSRC_OR_DST        0xd6
#define CIRRUS_ROP_NOTSRC_AND_NOTDST    0xda

#define CIRRUS_ROP_NOP_INDEX 2
#define CIRRUS_ROP_SRC_INDEX 5

// control 0x33
#define CIRRUS_BLTMODEEXT_SOLIDFILL        0x04
#define CIRRUS_BLTMODEEXT_COLOREXPINV      0x02
#define CIRRUS_BLTMODEEXT_DWORDGRANULARITY 0x01

// memory-mapped IO
#define CIRRUS_MMIO_BLTBGCOLOR        0x00	// dword
#define CIRRUS_MMIO_BLTFGCOLOR        0x04	// dword
#define CIRRUS_MMIO_BLTWIDTH          0x08	// word
#define CIRRUS_MMIO_BLTHEIGHT         0x0a	// word
#define CIRRUS_MMIO_BLTDESTPITCH      0x0c	// word
#define CIRRUS_MMIO_BLTSRCPITCH       0x0e	// word
#define CIRRUS_MMIO_BLTDESTADDR       0x10	// dword
#define CIRRUS_MMIO_BLTSRCADDR        0x14	// dword
#define CIRRUS_MMIO_BLTWRITEMASK      0x17	// byte
#define CIRRUS_MMIO_BLTMODE           0x18	// byte
#define CIRRUS_MMIO_BLTROP            0x1a	// byte
#define CIRRUS_MMIO_BLTMODEEXT        0x1b	// byte
#define CIRRUS_MMIO_BLTTRANSPARENTCOLOR 0x1c	// word?
#define CIRRUS_MMIO_BLTTRANSPARENTCOLORMASK 0x20	// word?
#define CIRRUS_MMIO_LINEARDRAW_START_X 0x24	// word
#define CIRRUS_MMIO_LINEARDRAW_START_Y 0x26	// word
#define CIRRUS_MMIO_LINEARDRAW_END_X  0x28	// word
#define CIRRUS_MMIO_LINEARDRAW_END_Y  0x2a	// word
#define CIRRUS_MMIO_LINEARDRAW_LINESTYLE_INC 0x2c	// byte
#define CIRRUS_MMIO_LINEARDRAW_LINESTYLE_ROLLOVER 0x2d	// byte
#define CIRRUS_MMIO_LINEARDRAW_LINESTYLE_MASK 0x2e	// byte
#define CIRRUS_MMIO_LINEARDRAW_LINESTYLE_ACCUM 0x2f	// byte
#define CIRRUS_MMIO_BRESENHAM_K1      0x30	// word
#define CIRRUS_MMIO_BRESENHAM_K3      0x32	// word
#define CIRRUS_MMIO_BRESENHAM_ERROR   0x34	// word
#define CIRRUS_MMIO_BRESENHAM_DELTA_MAJOR 0x36	// word
#define CIRRUS_MMIO_BRESENHAM_DIRECTION 0x38	// byte
#define CIRRUS_MMIO_LINEDRAW_MODE     0x39	// byte
#define CIRRUS_MMIO_BLTSTATUS         0x40	// byte

#define CIRRUS_PNPMMIO_SIZE         0x1000

#define CIRRUS_BLTBUFSIZE (2048 * 4) /* one line width */

typedef struct clgd_t
{
        mem_mapping_t mmio_mapping;
        
        svga_t svga;
        cl_ramdac_t ramdac;
        
        rom_t bios_rom;

        PALETTE hiddenpal;

		uint32_t vram_size;
		uint8_t vram_code;

		uint32_t linear_mmio_mask;
       
        uint32_t bank[2];
		uint32_t limit[2];

		uint32_t ramptr;
		int src_counter;
		uint8_t *src_ptr;
		uint8_t *src_ptr_end;

        struct
        {
			uint8_t bpp;
			uint32_t bg_col, fg_col;                
			uint16_t width, height;
			uint16_t dst_pitch, src_pitch;               
			int32_t dst_addr, src_addr;
			uint8_t mask, mode, rop, modeext;
			uint32_t blttc, blttcmask;
			uint16_t ld_start_x, ld_start_y, ld_end_x, ld_end_y;
			uint8_t ld_ls_inc, ld_ls_ro, ld_ls_mask, ld_ls_ac;
			uint16_t bres_k1, bres_k3, bres_err, bres_dm;
			uint8_t bres_dir, ld_mode, blt_status;
                
			uint32_t dst_addr_backup, src_addr_backup;
			uint16_t width_backup, height_internal;
			int x_count;
			uint8_t buf[CIRRUS_BLTBUFSIZE];

			uint16_t pixel_width, pixel_height;
        } blt;
} clgd_t;

typedef void (*cirrus_bitblt_rop_t) (clgd_t *clgd, svga_t *svga,
					uint8_t * dst, const uint8_t * src,
					int dstpitch, int srcpitch,
					int bltwidth, int bltheight);

typedef void (*cirrus_fill_t)(clgd_t *clgd, svga_t *svga,
				uint8_t *dst, int dst_pitch, int width, int height);

cirrus_bitblt_rop_t cirrus_rop;

extern device_t gd5422_device;
extern device_t gd5429_device;
extern device_t gd5430_device;
extern device_t dia5430_device;
extern device_t gd5434_device;
extern device_t gd5436_device;
extern device_t gd5440_device;
extern device_t gd5446_device;
extern device_t gd6235_device;

clgd_t *clgd;

void clgd_recalctimings(svga_t *svga);

void cirrus_update_memory_access(clgd_t *clgd);

void cirrus_write(uint32_t addr, uint8_t val, void *p);
uint8_t cirrus_read(uint32_t addr, void *p);
