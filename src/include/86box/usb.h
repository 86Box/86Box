/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the Distributed DMA emulation.
 *
 *
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

typedef struct usb_t usb_t;

/* USB device creation parameters struct */
typedef struct
{
    void (*raise_interrupt)(usb_t*, void*);
    /* Handle (but do not raise) SMI. Returns 1 if SMI can be raised, 0 otherwise. */
    uint8_t (*smi_handle)(usb_t*, void*);
    void* parent_priv;
} usb_params_t;

/* USB Host Controller device struct */
typedef struct usb_t
{
    uint8_t       uhci_io[32], ohci_mmio[4096];
    uint16_t      uhci_io_base;
    int           uhci_enable, ohci_enable;
    uint32_t      ohci_mem_base;
    mem_mapping_t ohci_mmio_mapping;
    pc_timer_t    ohci_frame_timer;
    pc_timer_t    ohci_interrupt_desc_poll_timer;
    pc_timer_t    ohci_port_reset_timer[2];
    uint8_t       ohci_interrupt_counter : 5;

    usb_params_t* usb_params;
} usb_t;

#pragma pack(push, 1)
/* Base USB descriptor struct. */
typedef struct
{
    uint8_t bLength;
    uint8_t bDescriptorType;
} usb_desc_base_t;
#pragma pack(pop)

/* USB endpoint device struct. Incomplete and unused. */
typedef struct
{
    uint16_t vendor_id;
    uint16_t device_id;

    /* Reads from endpoint. Non-zero value indicates error. */
    uint8_t (*device_in)(void* priv, uint8_t* data, uint32_t len);
    /* Writes to endpoint. Non-zero value indicates error. */
    uint8_t (*device_out)(void* priv, uint8_t* data, uint32_t len);
    /* Process setup packets. */
    uint8_t (*device_setup)(void* priv, uint8_t* data);

    void* priv;
} usb_device_t;

/* Global variables. */
extern const device_t usb_device;

/* Functions. */
extern void uhci_update_io_mapping(usb_t *dev, uint8_t base_l, uint8_t base_h, int enable);
extern void ohci_update_mem_mapping(usb_t *dev, uint8_t base1, uint8_t base2, uint8_t base3, int enable);

#ifdef __cplusplus
}
#endif

#endif /*USB_H*/
