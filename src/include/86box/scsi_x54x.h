/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Header of the code common to the AHA-154x series of SCSI
 *		Host Adapters made by Adaptec, Inc. and the BusLogic series
 *		of SCSI Host Adapters made by Mylex.
 *		These controllers were designed for various buses.
 *
 *
 *
 * Authors:	TheCollector1995, <mariogplayer@gmail.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#ifndef SCSI_X54X_H

#define SCSI_X54X_H

#define SCSI_DELAY_TM		1			/* was 50 */


#define ROM_SIZE	16384				/* one ROM is 16K */
#define NVR_SIZE	256				/* size of NVR */


/* EEPROM map and bit definitions. */
#define EE0_HOSTID	0x07	/* EE(0) [2:0]				*/
#define EE0_ALTFLOP	0x80	/* EE(0) [7] FDC at 370h		*/
#define EE1_IRQCH	0x07	/* EE(1) [3:0]				*/
#define EE1_DMACH	0x70	/* EE(1) [7:4]				*/
#define EE2_RMVOK	0x01	/* EE(2) [0] Support removable disks	*/
#define EE2_HABIOS	0x02	/* EE(2) [1] HA Bios Space Reserved	*/
#define EE2_INT19	0x04	/* EE(2) [2] HA Bios Controls INT19	*/
#define EE2_DYNSCAN	0x08	/* EE(2) [3] Dynamically scan bus	*/
#define EE2_TWODRV	0x10	/* EE(2) [4] Allow more than 2 drives	*/
#define EE2_SEEKRET	0x20	/* EE(2) [5] Immediate return on seek	*/
#define EE2_EXT1G	0x80	/* EE(2) [7] Extended Translation >1GB	*/
#define EE3_SPEED	0x00	/* EE(3) [7:0] DMA Speed		*/
#define  SPEED_33	0xFF
#define  SPEED_50	0x00
#define  SPEED_56	0x04
#define  SPEED_67	0x01
#define  SPEED_80	0x02
#define  SPEED_10	0x03
#define EE4_FLOPTOK	0x80	/* EE(4) [7] Support Flopticals		*/
#define EE6_PARITY	0x01	/* EE(6) [0] parity check enable	*/
#define EE6_TERM	0x02	/* EE(6) [1] host term enable		*/
#define EE6_RSTBUS	0x04	/* EE(6) [2] reset SCSI bus on boot	*/
#define EEE_SYNC	0x01	/* EE(E) [0] Enable Sync Negotiation	*/
#define EEE_DISCON	0x02	/* EE(E) [1] Enable Disconnection	*/
#define EEE_FAST	0x04	/* EE(E) [2] Enable FAST SCSI		*/
#define EEE_START	0x08	/* EE(E) [3] Enable Start Unit		*/


/*
 * Host Adapter I/O ports.
 *
 * READ  Port x+0: STATUS
 * WRITE Port x+0: CONTROL
 *
 * READ  Port x+1: DATA
 * WRITE Port x+1: COMMAND
 *
 * READ  Port x+2: INTERRUPT STATUS
 * WRITE Port x+2: (undefined?)
 *
 * R/W   Port x+3: (undefined)
 */

/* WRITE CONTROL commands. */
#define CTRL_HRST	0x80		/* Hard reset */
#define CTRL_SRST	0x40		/* Soft reset */
#define CTRL_IRST	0x20		/* interrupt reset */
#define CTRL_SCRST	0x10		/* SCSI bus reset */

/* READ STATUS. */
#define STAT_STST	0x80		/* self-test in progress */
#define STAT_DFAIL	0x40		/* internal diagnostic failure */
#define STAT_INIT	0x20		/* mailbox initialization required */
#define STAT_IDLE	0x10		/* HBA is idle */
#define STAT_CDFULL	0x08		/* Command/Data output port is full */
#define STAT_DFULL	0x04		/* Data input port is full */
#define STAT_INVCMD	0x01		/* Invalid command */

/* READ/WRITE DATA. */
#define CMD_NOP		0x00		/* No operation */
#define CMD_MBINIT	0x01		/* mailbox initialization */
#define CMD_START_SCSI	0x02		/* Start SCSI command */
#define CMD_BIOSCMD	0x03		/* Execute ROM BIOS command */
#define CMD_INQUIRY	0x04		/* Adapter inquiry */
#define CMD_EMBOI	0x05		/* enable Mailbox Out Interrupt */
#define CMD_SELTIMEOUT	0x06		/* Set SEL timeout */
#define CMD_BUSON_TIME	0x07		/* set bus-On time */
#define CMD_BUSOFF_TIME	0x08		/* set bus-off time */
#define CMD_DMASPEED	0x09		/* set ISA DMA speed */
#define CMD_RETDEVS	0x0A		/* return installed devices */
#define CMD_RETCONF	0x0B		/* return configuration data */
#define CMD_TARGET	0x0C		/* set HBA to target mode */
#define CMD_RETSETUP	0x0D		/* return setup data */
#define CMD_WRITE_CH2	0x1A		/* write channel 2 buffer */
#define CMD_READ_CH2	0x1B		/* read channel 2 buffer */
#define CMD_ECHO	0x1F		/* ECHO command data */
#define CMD_OPTIONS	0x21		/* set adapter options */

/* READ INTERRUPT STATUS. */
#define INTR_ANY	0x80		/* any interrupt */
#define INTR_SRCD	0x08		/* SCSI reset detected */
#define INTR_HACC	0x04		/* HA command complete */
#define INTR_MBOA	0x02		/* MBO empty */
#define INTR_MBIF	0x01		/* MBI full */


#define ADDR_TO_U32(x)	(((x).hi<<16)|((x).mid<<8)|((x).lo&0xFF))
#define U32_TO_ADDR(a,x) do {(a).hi=(x)>>16;(a).mid=(x)>>8;(a).lo=(x)&0xFF;}while(0)

/*
 * Mailbox Definitions.
 *
 * Mailbox Out (MBO) command values.
 */
#define MBO_FREE                  0x00
#define MBO_START                 0x01
#define MBO_ABORT                 0x02

/* Mailbox In (MBI) status values. */
#define MBI_FREE                  0x00
#define MBI_SUCCESS               0x01
#define MBI_ABORT                 0x02
#define MBI_NOT_FOUND             0x03
#define MBI_ERROR                 0x04

/*
 *
 * CCB - SCSI Command Control Block
 *
 *    The CCB is a superset of the CDB (Command Descriptor Block)
 *    and specifies detailed information about a SCSI command.
 *
 */
/*    Byte 0    Command Control Block Operation Code */
#define SCSI_INITIATOR_COMMAND		0x00
#define TARGET_MODE_COMMAND		0x01
#define SCATTER_GATHER_COMMAND		0x02
#define SCSI_INITIATOR_COMMAND_RES	0x03
#define SCATTER_GATHER_COMMAND_RES	0x04
#define BUS_RESET			0x81

/*    Byte 1    Address and Direction Control */
#define CCB_TARGET_ID_SHIFT		0x06	/* CCB Op Code = 00, 02 */
#define CCB_INITIATOR_ID_SHIFT		0x06	/* CCB Op Code = 01 */
#define CCB_DATA_XFER_IN		0x01
#define CCB_DATA_XFER_OUT		0x02
#define CCB_LUN_MASK			0x07	/* Logical Unit Number */

/*    Byte 2    SCSI_Command_Length - Length of SCSI CDB
      Byte 3    Request Sense Allocation Length */
#define FOURTEEN_BYTES			0x00	/* Request Sense Buffer size */
#define NO_AUTO_REQUEST_SENSE		0x01	/* No Request Sense Buffer */

/*    Bytes 4, 5 and 6    Data Length		 - Data transfer byte count */
/*    Bytes 7, 8 and 9    Data Pointer		 - SGD List or Data Buffer */
/*    Bytes 10, 11 and 12 Link Pointer		 - Next CCB in Linked List */
/*    Byte 13   Command Link ID			 - TBD (I don't know yet) */
/*    Byte 14   Host Status			 - Host Adapter status */
#define CCB_COMPLETE			0x00	/* CCB completed without error */
#define CCB_LINKED_COMPLETE		0x0A	/* Linked command completed */
#define CCB_LINKED_COMPLETE_INT		0x0B	/* Linked complete with intr */
#define CCB_SELECTION_TIMEOUT		0x11	/* Set SCSI selection timed out */
#define CCB_DATA_OVER_UNDER_RUN		0x12
#define CCB_UNEXPECTED_BUS_FREE		0x13	/* Trg dropped SCSI BSY */
#define CCB_PHASE_SEQUENCE_FAIL		0x14	/* Trg bus phase sequence fail */
#define CCB_BAD_MBO_COMMAND		0x15	/* MBO command not 0, 1 or 2 */
#define CCB_INVALID_OP_CODE		0x16	/* CCB invalid operation code */
#define CCB_BAD_LINKED_LUN		0x17	/* Linked CCB LUN diff from 1st */
#define CCB_INVALID_DIRECTION		0x18	/* Invalid target direction */
#define CCB_DUPLICATE_CCB		0x19	/* Duplicate CCB */
#define CCB_INVALID_CCB			0x1A	/* Invalid CCB - bad parameter */

#define lba32_blk(p)	((uint32_t)(p->u.lba.lba0<<24) | (p->u.lba.lba1<<16) | \
				   (p->u.lba.lba2<<8) | p->u.lba.lba3)

/*
 *
 * Scatter/Gather Segment List Definitions
 *
 * Adapter limits
 */
#define MAX_SG_DESCRIPTORS 32	/* Always make the array 32 elements long, if less are used, that's not an issue. */


#pragma pack(push,1)
typedef struct {
    uint8_t hi;
    uint8_t mid;
    uint8_t lo;
} addr24;

/* Structure for the INQUIRE_SETUP_INFORMATION reply. */
typedef struct {
    uint8_t	uOffset		:4,
		uTransferPeriod :3,
		fSynchronous	:1;
} ReplyInquireSetupInformationSynchronousValue;

typedef struct {
    uint8_t	fSynchronousInitiationEnabled	:1,
		fParityCheckingEnabled		:1,
		uReserved1			:6;
    uint8_t	uBusTransferRate;
    uint8_t	uPreemptTimeOnBus;
    uint8_t	uTimeOffBus;
    uint8_t	cMailbox;
    addr24	MailboxAddress;
    ReplyInquireSetupInformationSynchronousValue SynchronousValuesId0To7[8];
    uint8_t	uDisconnectPermittedId0To7;
    uint8_t	VendorSpecificData[28];
} ReplyInquireSetupInformation;

typedef struct {
    uint8_t	Count;
    addr24	Address;
} MailboxInit_t;

typedef struct {
    uint8_t	CmdStatus;
    addr24	CCBPointer;
} Mailbox_t;

typedef struct {
    uint32_t	CCBPointer;
    union {
	struct {
		uint8_t Reserved[3];
		uint8_t ActionCode;
	} out;
	struct {
		uint8_t HostStatus;
		uint8_t TargetStatus;
		uint8_t Reserved;
		uint8_t CompletionCode;
	} in;
    }		u;
} Mailbox32_t;

/*    Byte 15   Target Status

      See scsi.h files for these statuses.
      Bytes 16 and 17   Reserved (must be 0)
      Bytes 18 through 18+n-1, where n=size of CDB  Command Descriptor Block */

typedef struct {
    uint8_t	Opcode;
    uint8_t	Reserved1	:3,
		ControlByte	:2,
		TagQueued	:1,
		QueueTag	:2;
    uint8_t	CdbLength;
    uint8_t	RequestSenseLength;
    uint32_t	DataLength;
    uint32_t	DataPointer;
    uint8_t	Reserved2[2];
    uint8_t	HostStatus;
    uint8_t	TargetStatus;
    uint8_t	Id;
    uint8_t	Lun		:5,
		LegacyTagEnable	:1,
		LegacyQueueTag	:2;
    uint8_t	Cdb[12];
    uint8_t	Reserved3[6];
    uint32_t	SensePointer;
} CCB32;

typedef struct {
    uint8_t	Opcode;
    uint8_t	Lun		:3,
		ControlByte	:2,
		Id		:3;
    uint8_t	CdbLength;
    uint8_t	RequestSenseLength;
    addr24	DataLength;
    addr24	DataPointer;
    addr24	LinkPointer;
    uint8_t	LinkId;
    uint8_t	HostStatus;
    uint8_t	TargetStatus;
    uint8_t	Reserved[2];
    uint8_t	Cdb[12];
} CCB;

typedef struct {
    uint8_t	Opcode;
    uint8_t	Pad1		:3,
		ControlByte	:2,
		Pad2		:3;
    uint8_t	CdbLength;
    uint8_t	RequestSenseLength;
    uint8_t	Pad3[9];
    uint8_t	CompletionCode;	/* Only used by the 1542C/CF(/CP?) BIOS mailboxes */
    uint8_t	HostStatus;
    uint8_t	TargetStatus;
    uint8_t	Pad4[2];
    uint8_t	Cdb[12];
} CCBC;

typedef union {
    CCB32	new;
    CCB		old;
    CCBC	common;
} CCBU;

typedef struct {
    CCBU	CmdBlock;
    uint8_t	*RequestSenseBuffer;
    uint32_t	CCBPointer;
    int		Is24bit;
    uint8_t	TargetID,
		LUN,
		HostStatus,
		TargetStatus,
		MailboxCompletionCode;
} Req_t;

typedef struct
{
	uint8_t	command;
	uint8_t	lun:3,
		reserved:2,
		id:3;
	union {
	    struct {
		uint16_t cyl;
		uint8_t	head;
		uint8_t	sec;
	    } chs;
	    struct {
		uint8_t lba0;	/* MSB */
		uint8_t lba1;
		uint8_t lba2;
		uint8_t lba3;	/* LSB */
	    } lba;
	}	u;
	uint8_t	secount;
	addr24	dma_address;
} BIOSCMD;

typedef struct {
    uint32_t	Segment;
    uint32_t	SegmentPointer;
} SGE32;

typedef struct {
    addr24	Segment;
    addr24	SegmentPointer;
} SGE;
#pragma pack(pop)

#define		X54X_CDROM_BOOT		 1
#define		X54X_32BIT		 2
#define		X54X_LBA_BIOS		 4
#define		X54X_INT_GEOM_WRITABLE	 8
#define		X54X_MBX_24BIT		16
#define		X54X_ISAPNP		32

typedef struct {
    /* 32 bytes */
    char	vendor[16],			/* name of device vendor */
		name[16];			/* name of device */

    /* 24 bytes */
    int8_t	type,				/* type of device */
		IrqEnabled, Irq,
		DmaChannel,
		HostID;

    uint8_t	callback_phase		:4,
		callback_sub_phase	:4,
		scsi_cmd_phase, bus,
		sync,
		parity, shram_mode,
		Geometry, Control,
		Command, CmdParam,
		BusOnTime, BusOffTime,
		ATBusSpeed, setup_info_len,
		max_id, pci_slot,
		temp_cdb[12];

    volatile uint8_t				/* for multi-threading, keep */
		Status, Interrupt,		/* these volatile */
		MailboxIsBIOS, ToRaise,
		flags;

    /* 65928 bytes */
    uint8_t	pos_regs[8],			/* MCA */
		CmdBuf[128],
		DataBuf[65536],
		shadow_ram[128],
		dma_buffer[128],
		cmd_33_buf[4096];

    /* 16 bytes */
    char	*fw_rev;			/* The 4 bytes of the revision command information + 2 extra bytes for BusLogic */

    uint8_t	*rom1,				/* main BIOS image */
		*rom2,				/* SCSI-Select image */
		*nvr;				/* EEPROM buffer */

    /* 6 words = 12 bytes */
    uint16_t	DataReply, DataReplyLeft,
		rom_ioaddr,			/* offset in BIOS of I/O addr */
		rom_shram,			/* index to shared RAM */
		rom_shramsz,			/* size of shared RAM */
		rom_fwhigh,			/* offset in BIOS of ver ID */
		pnp_len,			/* length of the PnP ROM */
		pnp_offset,			/* offset in the microcode ROM of the PnP ROM */
		cmd_33_len,			/* length of the SCSISelect code decompressor program */
		cmd_33_offset;			/* offset in the microcode ROM of the SCSISelect code decompressor program */

    /* 16 + 20 + 52 = 88 bytes */
    volatile int
		MailboxOutInterrupts,
		PendingInterrupt, Lock,
		target_data_len, pad0;

    uint32_t	Base, fdc_address, rom_addr,	/* address of BIOS ROM */
		CmdParamLeft, Outgoing,
		transfer_size;

    volatile uint32_t
		MailboxInit, MailboxCount,
		MailboxOutAddr, MailboxOutPosCur,
		MailboxInAddr, MailboxInPosCur,
		MailboxReq,
		BIOSMailboxInit, BIOSMailboxCount,
		BIOSMailboxOutAddr, BIOSMailboxOutPosCur,
		BIOSMailboxReq,
		Residue, card_bus;		/* Basically a copy of device flags */

    /* 8 bytes */
    uint64_t	temp_period;

    /* 16 bytes */
    double	media_period, ha_bps;		/* bytes per second */

    /* 8 bytes */
    char	*bios_path,			/* path to BIOS image file */
		*mcode_path,			/* path to microcode image file, needed by the AHA-1542CP */
		*nvr_path;			/* path to NVR image file */

    /* 56 bytes */
    /* Pointer to a structure of vendor-specific data that only the vendor-specific code can understand */
    void	*ven_data;

    /* Pointer to a function that performs vendor-specific operation during the timer callback */
    void	(*ven_callback)(void *p);
    /* Pointer to a function that executes the second parameter phase of the vendor-specific command */
    void	(*ven_cmd_phase1)(void *p);
    /* Pointer to a function that gets the host adapter ID in case it has to be read from a non-standard location */
    uint8_t	(*ven_get_host_id)(void *p);
    /* Pointer to a function that updates the IRQ in the vendor-specific space */
    uint8_t	(*ven_get_irq)(void *p);
    /* Pointer to a function that updates the DMA channel in the vendor-specific space */
    uint8_t	(*ven_get_dma)(void *p);
    /* Pointer to a function that returns whether command is fast */
    uint8_t	(*ven_cmd_is_fast)(void *p);
    /* Pointer to a function that executes vendor-specific fast path commands */
    uint8_t	(*ven_fast_cmds)(void *p, uint8_t cmd);
    /* Pointer to a function that gets the parameter length for vendor-specific commands */
    uint8_t	(*get_ven_param_len)(void *p);
    /* Pointer to a function that executes vendor-specific commands and returns whether or not to suppress the IRQ */
    uint8_t	(*ven_cmds)(void *p);
    /* Pointer to a function that fills in the vendor-specific setup data */
    void	(*get_ven_data)(void *p);
    /* Pointer to a function that determines if the mode is aggressive */
    uint8_t	(*is_aggressive_mode)(void *p);
    /* Pointer to a function that returns interrupt type (0 = edge, 1 = level) */
    uint8_t	(*interrupt_type)(void *p);
    /* Pointer to a function that resets vendor-specific data */
    void	(*ven_reset)(void *p);

    rom_t	bios,				/* BIOS memory descriptor */
		uppersck;			/* BIOS memory descriptor */

    mem_mapping_t mmio_mapping;

    pc_timer_t	timer, ResetCB;

    Req_t	Req;
    
    fdc_t	*fdc;
} x54x_t;


extern void	x54x_reset_ctrl(x54x_t *dev, uint8_t Reset);
extern uint8_t	x54x_mbo_process(x54x_t *dev);
extern void	x54x_wait_for_poll(void);
extern void	x54x_io_set(x54x_t *dev, uint32_t base, uint8_t len);
extern void	x54x_io_remove(x54x_t *dev, uint32_t base, uint8_t len);
extern void	x54x_mem_init(x54x_t *dev, uint32_t addr);
extern void	x54x_mem_enable(x54x_t *dev);
extern void	x54x_mem_set_addr(x54x_t *dev, uint32_t base);
extern void	x54x_mem_disable(x54x_t *dev);
extern void	*x54x_init(const device_t *info);
extern void	x54x_close(void *priv);
extern void	x54x_device_reset(void *priv);


#endif
