#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/mouse.h>
#include <86box/serial.h>
#include <86box/plat.h>
#include <86box/fifo8.h>
#include <86box/mem.h>
#include <86box/usb.h>

typedef struct wacom_tablet_penp_t
{
    int dx, dy, dz, buttons_state;
    int x, y;
    int mouse_grabbed;
    enum {
        WACOM_MODE_HID = 1,
        WACOM_MODE_WACOM = 2,
    } mode;
    uint8_t idle;
    int changed;

    Fifo8 fifo; /* Device-to-host FIFO */
} wacom_tablet_penp_t;

enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT,
    STR_SERIALNUMBER,
};

static const uint8_t qemu_wacom_hid_report_descriptor[] = {
    0x05, 0x01,      /* Usage Page (Desktop) */
    0x09, 0x02,      /* Usage (Mouse) */
    0xa1, 0x01,      /* Collection (Application) */
    0x85, 0x01,      /*    Report ID (1) */
    0x09, 0x01,      /*    Usage (Pointer) */
    0xa1, 0x00,      /*    Collection (Physical) */
    0x05, 0x09,      /*       Usage Page (Button) */
    0x19, 0x01,      /*       Usage Minimum (01h) */
    0x29, 0x03,      /*       Usage Maximum (03h) */
    0x15, 0x00,      /*       Logical Minimum (0) */
    0x25, 0x01,      /*       Logical Maximum (1) */
    0x95, 0x03,      /*       Report Count (3) */
    0x75, 0x01,      /*       Report Size (1) */
    0x81, 0x02,      /*       Input (Data, Variable, Absolute) */
    0x95, 0x01,      /*       Report Count (1) */
    0x75, 0x05,      /*       Report Size (5) */
    0x81, 0x01,      /*       Input (Constant) */
    0x05, 0x01,      /*       Usage Page (Desktop) */
    0x09, 0x30,      /*       Usage (X) */
    0x09, 0x31,      /*       Usage (Y) */
    0x09, 0x38,      /*       Usage (Wheel) */
    0x15, 0x81,      /*       Logical Minimum (-127) */
    0x25, 0x7f,      /*       Logical Maximum (127) */
    0x75, 0x08,      /*       Report Size (8) */
    0x95, 0x03,      /*       Report Count (3) */
    0x81, 0x06,      /*       Input (Data, Variable, Relative) */
    0x95, 0x03,      /*       Report Count (3) */
    0x81, 0x01,      /*       Input (Constant) */
    0xc0,            /*    End Collection */
    0xc0,            /* End Collection */
    0x05, 0x0d,      /* Usage Page (Digitizer) */
    0x09, 0x01,      /* Usage (Digitizer) */
    0xa1, 0x01,      /* Collection (Application) */
    0x85, 0x02,      /*    Report ID (2) */
    0xa1, 0x00,      /*    Collection (Physical) */
    0x06, 0x00, 0xff,/*       Usage Page (ff00h), vendor-defined */
    0x09, 0x01,      /*       Usage (01h) */
    0x15, 0x00,      /*       Logical Minimum (0) */
    0x26, 0xff, 0x00,/*       Logical Maximum (255) */
    0x75, 0x08,      /*       Report Size (8) */
    0x95, 0x07,      /*       Report Count (7) */
    0x81, 0x02,      /*       Input (Data, Variable, Absolute) */
    0xc0,            /*    End Collection */
    0x09, 0x01,      /*    Usage (01h) */
    0x85, 0x63,      /*    Report ID (99) */
    0x95, 0x07,      /*    Report Count (7) */
    0x81, 0x02,      /*    Input (Data, Variable, Absolute) */
    0x09, 0x01,      /*    Usage (01h) */
    0x85, 0x02,      /*    Report ID (2) */
    0x95, 0x01,      /*    Report Count (1) */
    0xb1, 0x02,      /*    Feature (Variable) */
    0x09, 0x01,      /*    Usage (01h) */
    0x85, 0x03,      /*    Report ID (3) */
    0x95, 0x01,      /*    Report Count (1) */
    0xb1, 0x02,      /*    Feature (Variable) */
    0xc0             /* End Collection */
};

static const usb_desc_endpoint_t wacom_tablet_penp_endpoint_desc =
{
    .base = {
        .bLength = sizeof(usb_desc_endpoint_t),
        .bDescriptorType = 0x05
    },

    .bEndpointAddress = 0x81,
    .bmAttributes = 0x80,
    .wMaxPacketSize = 8,
    .bInterval = 10
};

static const usb_desc_hid_t wacom_tablet_penp_hid_desc =
{
    .base = {
        .bLength = sizeof(usb_desc_hid_t),
        .bDescriptorType = 0x22
    },
    .bcdHID = 0x1001, /* Strange value but whatever. */
    .bCountryCode = 0x00,
    .bNumDescriptors = 1,
    .bDescriptorType = 0x22,
    .wDescriptorLength = sizeof(qemu_wacom_hid_report_descriptor)
};

static const usb_desc_interface_t wacom_tablet_penp_interface_desc = 
{
    .base = {
        .bLength = sizeof(usb_desc_interface_t),
        .bDescriptorType = 0x04
    },
    .bInterfaceNumber = 1,
    .bAlternateSetting = 0,
    .bNumEndpoints = 1,
    .bInterfaceClass = 0x03,
    .bInterfaceSubClass = 0x01,
    .bInterfaceProtocol = 0x02,
    .iInterface = 0
};

static const usb_desc_conf_t wacom_tablet_penp_conf_desc = 
{
    .base.bLength = sizeof(usb_desc_conf_t),
    .base.bDescriptorType = 0x02,
    .wTotalLength = sizeof(usb_desc_conf_t) + sizeof(usb_desc_interface_t) + sizeof(usb_desc_endpoint_t) + sizeof(usb_desc_hid_t),

    .bNumInterfaces = 1,
    .bConfigurationValue = 1,
    .iConfiguration = 0,
    .bmAttributes = 0x80,
    .bMaxPower = 40
};

static const usb_desc_device_t wacom_tablet_penp_device_desc = 
{
    .base.bLength = 18,
    .base.bDescriptorType = 0x01, /* Device descriptor. */

    .bcdUSB = 0x0110, /* USB 1.1 */
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize = 8,
    .idVendor = 0x056a,
    .idProduct = 0x0000,
    .bcdDevice = 0x4210,
    .iManufacturer = STR_MANUFACTURER,
    .iProduct = STR_PRODUCT,
    .iSerialNumber = STR_SERIALNUMBER,
    .bNumConfigurations = 1,
};
