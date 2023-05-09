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
    void (*update_interrupt)(usb_t*, void*);
    /* Handle (but do not raise) SMI. Returns 1 if SMI can be raised, 0 otherwise. */
    uint8_t (*smi_handle)(usb_t*, void*);
    void* parent_priv;
} usb_params_t;

typedef union
{
    uint32_t l;
    uint16_t w[2];
    uint8_t  b[4];
} ohci_mmio_t;

/* USB Host Controller device struct */
typedef struct usb_t
{
    uint8_t       uhci_io[32];
    ohci_mmio_t   ohci_mmio[1024];
    uint16_t      uhci_io_base;
    int           uhci_enable, ohci_enable;
    uint32_t      ohci_mem_base, irq_level;
    mem_mapping_t ohci_mmio_mapping;
    pc_timer_t    ohci_frame_timer;
    pc_timer_t    ohci_port_reset_timer[2];
    uint8_t       ohci_interrupt_counter : 3;

    usb_params_t* usb_params;
} usb_t;

#pragma pack(push, 1)

/* Base USB descriptor struct. */
typedef struct
{
    uint8_t bLength;
    uint8_t bDescriptorType;
} usb_desc_base_t;

typedef struct
{
    usb_desc_base_t base;

    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} usb_desc_conf_t;

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
    /* Device reset */
    void (*device_reset)(void* priv);

    void* priv;
} usb_device_t;

enum usb_bus_types
{
    USB_BUS_OHCI = 0,
    USB_BUS_UHCI = 1
};

/* Global variables. */
extern const device_t usb_device;

/* Functions. */
extern void uhci_update_io_mapping(usb_t *dev, uint8_t base_l, uint8_t base_h, int enable);
extern void ohci_update_mem_mapping(usb_t *dev, uint8_t base1, uint8_t base2, uint8_t base3, int enable);
/* Attach USB device to a port of a USB bus. Returns the port to which it got attached to. */
extern uint8_t usb_attach_device(usb_t *dev, usb_device_t* device, uint8_t bus_type);
/* Detach USB device from a port. */
extern void usb_detach_device(usb_t *dev, uint8_t port, uint8_t bus_type);

#ifdef __cplusplus
}
#endif

#endif /*USB_H*/
