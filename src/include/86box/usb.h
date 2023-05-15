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
typedef struct usb_device_t usb_device_t;

enum usb_pid
{
    USB_PID_OUT = 0xE1,
    USB_PID_IN = 0x69,
    USB_PID_SETUP = 0x2D
};

enum usb_errors
{
    USB_ERROR_NO_ERROR = 0,
    USB_ERROR_NAK = 1,
    USB_ERROR_OVERRUN = 2,
    USB_ERROR_UNDERRUN = 3
};

enum usb_bus_types
{
    USB_BUS_OHCI = 0,
    USB_BUS_UHCI,
    USB_BUS_MAX
};

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
    usb_device_t* ohci_devices[2];
    usb_device_t* uhci_devices[2];
    uint8_t       ohci_usb_buf[4096];
    uint8_t       ohci_initial_start;

    usb_params_t* usb_params;
} usb_t;

#pragma pack(push, 1)

/* Base USB descriptor struct. */
typedef struct
{
    uint8_t bLength;
    uint8_t bDescriptorType;
} usb_desc_base_t;

enum usb_desc_setup_req_types
{
    USB_SETUP_TYPE_DEVICE = 0x0,
    USB_SETUP_TYPE_INTERFACE = 0x1,
    USB_SETUP_TYPE_ENDPOING = 0x2,
    USB_SETUP_TYPE_OTHER = 0x3,
};

#define USB_SETUP_TYPE_MAX 0x1F

#define USB_SETUP_DEV_TO_HOST 0x80

typedef struct
{
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_desc_setup_t;

typedef struct
{
    usb_desc_base_t base;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
} usb_desc_endpoint_t;

typedef struct
{
    usb_desc_base_t base;

    uint16_t bcdHID;
    uint8_t bCountryCode;
    uint8_t bNumDescriptors;
    uint8_t bDescriptorType;
    uint16_t wDescriptorLength;
} usb_desc_hid_t;

typedef struct
{
    usb_desc_base_t base;

    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} usb_desc_interface_t;

typedef struct
{
    usb_desc_base_t base;
    uint16_t bString[];
} usb_desc_string_t;

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

typedef struct
{
    usb_desc_base_t base;

    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} usb_desc_device_t;

#pragma pack(pop)

/* USB endpoint device struct. Incomplete and unused. */
typedef struct usb_device_t
{
    usb_desc_device_t device_desc;
    struct {
        usb_desc_conf_t conf_desc;
        usb_desc_base_t* other_descs[16];
    } conf_desc_items;

    /* General-purpose function for I/O. Non-zero value indicates error. */
    uint8_t (*device_process)(void* priv, uint8_t* data, uint32_t *len, uint8_t pid_token, uint8_t endpoint, uint8_t underrun_not_allowed);
    /* Device reset. */
    void (*device_reset)(void* priv);
    /* Get address. */
    uint8_t (*device_get_address)(void* priv);
    
    void* priv;
} usb_device_t;

/* Global variables. */
extern const device_t usb_device;
extern usb_t* usb_device_inst;

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
