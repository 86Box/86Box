/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the Universal Serial Bus host controllers and
 *          devices.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020 Miran Grca.
 */

#ifndef USB_H
#define USB_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct uhci_t uhci_t;
typedef struct ohci_t ohci_t;

typedef struct usb_t {
    uhci_t       *uhci;
    ohci_t       *ohci;
    uint16_t      uhci_io_base;
    int           uhci_enable;
    int           ohci_enable;
    uint32_t      ohci_mem_base;
    mem_mapping_t ohci_mmio_mapping;
    uint8_t       card; /* on a PCI USB card, not on the board */
} usb_t;

/* Global variables. */
extern const device_t usb_device;
/* A USB mass storage bridge to a SCSI bus. */
extern const device_t usb_msd_device;
/* USB mass storage, listed on the Ports page; usb_msd_type 0 is none. */
extern int            usb_msd_type;

extern int             usb_msd_get_ndev(void);
extern const device_t *usb_msd_get_device(int msd);
extern const char     *usb_msd_get_internal_name(int msd);
extern int             usb_msd_get_from_internal_name(const char *s);
extern void            usb_msd_hard_reset(void);
/* USB ports, as the status bar shows them: the root ports of every
   controller the chipset has wired up, numbered "1", "2", ... in
   controller order, then the ports of a hub, "<its root port>.<n>". The
   GUI reads a copy, which the emulation thread keeps current. */
#define USB_PORTS_MAX     24
/* Room for "%i.%i" with any two ints, so the compiler can see that a
   label always fits; the labels in use are much shorter. */
#define USB_PORT_LABEL    24

enum {
    USB_PORT_EMPTY = 0,
    USB_PORT_FLASH,  /* a flash drive */
    USB_PORT_DEVICE, /* any other device */
    USB_PORT_OFF     /* the controller or hub port is not working */
};

/* Where a port is, for its name in the GUI. */
enum {
    USB_PORT_ON_BOARD = 0,
    USB_PORT_ON_CARD,
    USB_PORT_ON_HUB
};

typedef struct usb_port_info_t {
    char    label[USB_PORT_LABEL];
    int     where;
    int     state;
    int     slot;          /* USB_PORT_DEVICE: its hot-plug slot, -1 for none */
    char    name[64];      /* USB_PORT_DEVICE: the device name */
    char    image[MAX_IMAGE_PATH_LEN]; /* USB_PORT_FLASH: its image file */
    uint8_t wp;
    uint8_t online;        /* USB_PORT_FLASH: the stick can reach the host */
} usb_port_info_t;

/* Copies the ports into ports (up to max); returns how many there are. */
extern int usb_ports_get(usb_port_info_t *ports, int max);
/* The image of the flash drive in port label as the ports were last listed;
   returns 0 when there is none. */
extern int usb_port_flash_image(const char *label, char *fn, int n);

/* USB flash drives: a stick with a raw image, plugged into a port. The
   user plugs, swaps and unplugs them from the GUI by port; the config
   keeps which image is in which port, and the sticks go back there on the
   next start. */
#define USB_FLASH_MIN_SIZE (8ULL << 20)
#define USB_FLASH_MAX_SIZE (32ULL << 30)
enum {
    USB_FLASH_FS_BLANK = 0,
    USB_FLASH_FS_FAT16,
    USB_FLASH_FS_FAT32
};
extern const device_t usb_flash_device;

/* What the config holds: entry i, image fn in port label. */
extern void usb_flash_cfg_clear(void);
extern int  usb_flash_cfg_get(int i, char *label, char *fn, uint8_t *wp);
extern void usb_flash_cfg_set(int i, const char *label, const char *fn, uint8_t wp);
/* The hub moved from root port from to root port to: its sticks keep their
   hub port numbers. */
extern void usb_flash_cfg_rehome(const char *from, const char *to);
/* Images used before, newest first, for the GUI's recent image lists:
   entry i into fn (returns 0 when it is empty), set it, clear them all. */
extern int  usb_flash_history_get(int i, char *fn, int n);
extern void usb_flash_history_set(int i, const char *fn);
extern void usb_flash_history_clear(void);
/* GUI thread: put a stick with fn into port label (swapping out any stick
   there), or with fn NULL or empty, unplug it. Done on the emulation
   thread. */
extern void usb_port_flash_want(const char *label, const char *fn, uint8_t wp);

extern void usb_flash_hard_reset(void);
/* Creates a raw image of bytes (a multiple of 512, USB_FLASH_MIN_SIZE to
   USB_FLASH_MAX_SIZE): partitioned and formatted with fs, or all zeros for
   USB_FLASH_FS_BLANK. Returns 1 on success. */
extern int  usb_flash_create(const char *fn, uint64_t bytes, int fs);
/* Whether a volume of that many bytes can be formatted with fs. */
extern int  usb_flash_fs_ok(uint64_t bytes, int fs);
extern int  usb_flash_fs_default(uint64_t bytes);
/* A USB speaker, listed with the sound cards. */
extern const device_t usb_speaker_device;
/* The VIA VT83C572 PCI to USB controller on an add-in card. */
extern const device_t usb_via_vt83c572_device;
/* USB hubs, listed on the Ports page; usb_hub_type 0 is none. */
extern const device_t usb_hub_4port_device;
extern int            usb_hub_type;

extern int             usb_hub_get_ndev(void);
extern const device_t *usb_hub_get_device(int hub);
extern const char     *usb_hub_get_internal_name(int hub);
extern int             usb_hub_get_from_internal_name(const char *s);
extern void            usb_hub_reset(void);
/* PCI USB host controller cards, listed on the Ports page; usb_card_type 0
   is none. */
extern const device_t usb_opti_82c861_device;
extern int            usb_card_type;
/* The settings dialog's card choice before it is saved, -1 outside it. */
extern int            usb_card_pending;

extern int             usb_card_get_ndev(void);
extern const device_t *usb_card_get_device(int card);
extern const char     *usb_card_get_internal_name(int card);
extern int             usb_card_get_from_internal_name(const char *s);
/* Whether the selected card gives machine mch a USB bus. */
extern int             usb_card_has_bus(int mch);
extern void            usb_card_reset(void);

/* Hot plug. Each USB device kind the machine can have (hub, mouse, tablet,
   keyboard, speaker, mass storage) has one slot. The user plugs and
   unplugs a device from the GUI thread with usb_slot_want(); the change is
   made on the emulation thread by usb_hotplug_process(), which runs
   between CPU slices, so no controller or device state is ever touched
   from the GUI. What the user wants is kept in the config. */
#define USB_SLOTS 6

extern const char     *usb_slot_internal_name(int slot);
/* The device that holds the slot, NULL when the machine has none. */
extern const device_t *usb_slot_device(int slot);
/* The device's own name, as the port titles show it; NULL without one. */
extern const char     *usb_slot_name(int slot);
/* Whether the device is attached to a port right now. */
extern int             usb_slot_connected(int slot);
/* Whether it reaches the host: attached, and so is every hub above it. */
extern int             usb_slot_online(int slot);
/* 1 unless the user unplugged the slot's device; the config value. */
extern int             usb_slot_wanted(int slot);
extern void            usb_slot_want(int slot, int connected);
/* What the Connected item shows: the request while it is pending, else
   usb_slot_connected(). */
extern int             usb_slot_requested(int slot);
/* The port the user put the slot's device in, "" for the first free one;
   the config value. label holds USB_PORT_LABEL bytes. */
extern void            usb_slot_port(int slot, char *label);
extern void            usb_slot_set_port(int slot, const char *label);
extern int             usb_slot_is_hub(int slot);
/* GUI thread: move the slot's device to port label, plugging it in if it
   was unplugged, and keep that port for the next start. Moving the hub
   takes the devices on it along, with the same hub port numbers. */
extern void            usb_slot_move(int slot, const char *label);
extern void            usb_hotplug_process(void);
/* A flash drive request is waiting for usb_hotplug_process(). */
extern void            usb_hotplug_kick(void);

/* Functions. */
extern void uhci_update_io_mapping(usb_t *dev, uint8_t base_l, uint8_t base_h, int enable);
/* The chipset routes the UHCI interrupt; level is the controller's IRQ output. */
extern void uhci_set_irq_handler(usb_t *dev, void (*set_irq)(void *priv, int level), void *priv);
/* PCICMD Bus Master Enable: with it clear the UHCI makes no memory accesses
   (PIIX4 datasheet s6.1.3, p. 103 / PDF 103). */
extern void uhci_set_bus_master(usb_t *dev, int enable);
/* A root port whose device is still sending at EOF2 is disabled, unless the
   chipset keeps it enabled (VIA USB function Rx40 bit 6, Babble Option;
   VT82C686B datasheet, p. 76 / PDF 82). */
extern void uhci_set_babble_keep_port(usb_t *dev, int keep);
/* The chipset turns the whole UHCI function on or off; the devices on the
   root ports of a function turned off move to another controller. */
extern void uhci_set_present(usb_t *dev, int present);
extern void ohci_update_mem_mapping(usb_t *dev, uint8_t base1, uint8_t base2, uint8_t base3, int enable);
/* The chipset routes the OHCI interrupt; level is the controller's INT output. */
extern void ohci_set_irq_handler(usb_t *dev, void (*set_irq)(void *priv, int level), void *priv);
/* The chipset takes the OHCI's SMI output; level is the SMI pin. */
extern void ohci_set_smi_handler(usb_t *dev, void (*set_smi)(void *priv, int level), void *priv);
/* PCI COMMAND Bus Master: with it clear the OHCI reads no descriptors and
   does not write the HCCA (OpenHCI 1.0a Appendix A, p. 134 / PDF 148). */
extern void ohci_set_bus_master(usb_t *dev, int enable);
/* For a chipset whose Bus Master bit is hardwired to 1: on now and after
   every reset. */
extern void ohci_hardwire_bus_master(usb_t *dev);
/* Where an OHCI differs from flags 0, which model the ALi M5237; the SMSC
   SLC90E66 uses flags 0 as well. Set once at init and kept through every
   reset. */
#define OHCI_CHIP_LST_0628H 0x01 /* HcLSThreshold resets to 0628h, not 0 */
#define OHCI_CHIP_RWC       0x02 /* RemoteWakeupConnected is R/W, not hard 0 */
#define OHCI_CHIP_NO_SMI    0x04 /* no SMI pin: OwnershipChange is tied to 0 */
#define OHCI_CHIP_POTPGT_RW 0x08 /* POTPGT is a full R/W byte, not bits 25:24 */
#define OHCI_CHIP_RWE_RO    0x10 /* RemoteWakeupEnable is read-only 0 */
#define OHCI_CHIP_UE_RW     0x20 /* the UnrecoverableError enable is R/W */
#define OHCI_CHIP_PTRS_RO   0x40 /* HcPeriodCurrentED, HcDoneHead ignore writes */
extern void ohci_set_chip_flags(usb_t *dev, uint32_t flags);
/* The root hub has ports connectors (2 by default), of which the first ndp
   are implemented and reported in NumberDownstreamPorts. A chipset strap
   may change ndp at any time. */
extern void ohci_set_ndp(usb_t *dev, int ndp, int ports);

/* UHCI legacy keyboard support (UHCI 1.1 s5.2, p. 39-41 / PDF 45-47) on a
   function's LEGSUP bytes, legsup[0] = C0h and legsup[1] = C1h. The opts
   bits follow VIA USB function Rx41 (VT82C686B datasheet, p. 76 / PDF 82). */
#define UHCI_LEGSUP_STATUS_ALWAYS  0x01 /* trap status bits set without the enables (VIA Rx41 bit 2) */
#define UHCI_LEGSUP_A20_FF_TRAPPED 0x02 /* the sequence's final 64h <- FFh is not passed (VIA Rx41 bit 1) */
/* A cycle at 60h or 64h; returns 1 if it is trapped and must not reach the
   8042. a20 keeps the pass-through sequence position. */
extern int  uhci_legsup_kbc_cycle(uint8_t *legsup, uint8_t *a20, uint16_t port, int write, uint8_t val, int opts);
/* Whether keyboard controller cycles need to be seen at all. */
extern int  uhci_legsup_kbc_armed(const uint8_t *legsup, int opts);
/* The legacy USB event sources that are active, one bit each, at the bit
   of their LEGSUP enable: the enabled trap statuses in bits 3-0, the IRQ
   with USBSMIEN in bit 4, A20PTS with SMIEPTE in bit 7 (UHCI 1.1 s5.2.1,
   p. 39-40 / PDF 45-46). A source going active is an event (SMI#). irq is
   the UHCI IRQ. */
extern uint8_t uhci_legsup_event(const uint8_t *legsup, int irq);
extern void uhci_legsup_write_c0(uint8_t *legsup, uint8_t val);

#ifdef __cplusplus
}
#endif

#endif /*USB_H*/
