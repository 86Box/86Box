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

typedef struct usb_mouse_t
{
    int dx, dy, dz, buttons_state;
    uint8_t idle;
    uint16_t port;
    uint16_t protocol;
    int changed;

    usb_device_t device_instance;
} usb_mouse_t;

static const usb_desc_string_t str_manufacturer = {
    .base = {
        .bDescriptorType = 0x03,
        .bLength = 0x02 + ((sizeof("QEMU") - 1) * 2),
    },
    .bString = { 'Q', 'E', 'M', 'U' }
};

static const usb_desc_string_t str_product = {
    .base = {
        .bDescriptorType = 0x03,
        .bLength = sizeof(usb_desc_base_t) + ((sizeof("USB Mouse") - 1) * 2),
    },
    .bString = { 'U', 'S', 'B', ' ', 'M', 'o', 'u', 's', 'e' }
};

static const usb_desc_string_t str_serialnumber = {
    .base = {
        .bDescriptorType = 0x03,
        .bLength = 0x04,
    },
    .bString = { '1' }
};

static const usb_desc_string_t str_langid = {
    .base = {
        .bDescriptorType = 0x03,
        .bLength = 0x04,
    },
    .bString = { 0x0409 }
};

static const uint8_t qemu_mouse_hid_report_descriptor[] = {
    0x05, 0x01,		/* Usage Page (Generic Desktop) */
    0x09, 0x02,		/* Usage (Mouse) */
    0xa1, 0x01,		/* Collection (Application) */
    0x09, 0x01,		/*   Usage (Pointer) */
    0xa1, 0x00,		/*   Collection (Physical) */
    0x05, 0x09,		/*     Usage Page (Button) */
    0x19, 0x01,		/*     Usage Minimum (1) */
    0x29, 0x05,		/*     Usage Maximum (5) */
    0x15, 0x00,		/*     Logical Minimum (0) */
    0x25, 0x01,		/*     Logical Maximum (1) */
    0x95, 0x05,		/*     Report Count (5) */
    0x75, 0x01,		/*     Report Size (1) */
    0x81, 0x02,		/*     Input (Data, Variable, Absolute) */
    0x95, 0x01,		/*     Report Count (1) */
    0x75, 0x03,		/*     Report Size (3) */
    0x81, 0x01,		/*     Input (Constant) */
    0x05, 0x01,		/*     Usage Page (Generic Desktop) */
    0x09, 0x30,		/*     Usage (X) */
    0x09, 0x31,		/*     Usage (Y) */
    0x09, 0x38,		/*     Usage (Wheel) */
    0x15, 0x81,		/*     Logical Minimum (-0x7f) */
    0x25, 0x7f,		/*     Logical Maximum (0x7f) */
    0x75, 0x08,		/*     Report Size (8) */
    0x95, 0x03,		/*     Report Count (3) */
    0x81, 0x06,		/*     Input (Data, Variable, Relative) */
    0xc0,		/*   End Collection */
    0xc0,		/* End Collection */
};

static const usb_desc_endpoint_t usb_mouse_endpoint_desc =
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

static const usb_desc_hid_t usb_mouse_hid_desc =
{
    .base = {
        .bLength = sizeof(usb_desc_hid_t),
        .bDescriptorType = 0x22
    },
    .bcdHID = 0x110,
    .bCountryCode = 0x00,
    .bNumDescriptors = 1,
    .bDescriptorType = 0x22,
    .wDescriptorLength = sizeof(qemu_mouse_hid_report_descriptor)
};

static const usb_desc_interface_t usb_mouse_interface_desc = 
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

static const usb_desc_conf_t usb_mouse_conf_desc = 
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

enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT,
    STR_SERIALNUMBER,
};

static const usb_desc_device_t usb_mouse_device_desc = 
{
    .base.bLength = sizeof(usb_desc_device_t),
    .base.bDescriptorType = 0x01, /* Device descriptor. */

    .bcdUSB = 0x0110, /* USB 1.1 */
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize = 8,
    .idVendor = 0x0627,
    .idProduct = 0x0001,
    .bcdDevice = 0,
    .iManufacturer = STR_MANUFACTURER,
    .iProduct = STR_PRODUCT,
    .iSerialNumber = STR_SERIALNUMBER,
    .bNumConfigurations = 1,
};

static inline int int_clamp(int val, int vmin, int vmax)
{
    if (val < vmin)
        return vmin;
    else if (val > vmax)
        return vmax;
    else
        return val;
}

static int usb_mouse_poll_hid(usb_mouse_t *s, uint8_t *buf, int len)
{
    int dx, dy, dz, b, l;

    dx = int_clamp(s->dx, -128, 127);
    dy = int_clamp(s->dy, -128, 127);
    dz = int_clamp(s->dz, -128, 127);

    s->dx -= dx;
    s->dy -= dy;
    s->dz -= dz;

    b = s->buttons_state;

    buf[0] = b;
    buf[1] = dx;
    buf[2] = dy;
    l = 3;
    if (len >= 4) {
        buf[3] = dz;
        l = 4;
    }
    return l;
}

uint8_t
usb_mouse_process_transfer(void* priv, uint8_t* data, uint32_t *len, uint8_t pid_token, uint8_t endpoint, uint8_t underrun_not_allowed)
{
    usb_mouse_t* usb_mouse = (usb_mouse_t*)priv;
    pclog("USB Mouse: Transfer (PID = 0x%X, len = %d, endpoint = %d)\n", pid_token, *len, endpoint);
    if (endpoint == 0) {
        if (pid_token == USB_PID_SETUP) {
            usb_desc_setup_t* setup_packet = (usb_desc_setup_t*)data;
            if (*len != 8) {
                return *len > 8 ? USB_ERROR_OVERRUN : USB_ERROR_UNDERRUN;
            }
            usb_mouse->device_instance.setup_desc = *setup_packet;
            if (setup_packet->bmRequestType == (USB_SETUP_TYPE_INTERFACE | 0x80)
                && setup_packet->bRequest == USB_SETUP_GET_DESCRIPTOR) {
                switch (setup_packet->wValue >> 8) {
                    case 0x22: {
                        fifo8_push_all(&usb_mouse->device_instance.fifo, qemu_mouse_hid_report_descriptor, sizeof(qemu_mouse_hid_report_descriptor));
                        break;
                    }
                    default:
                        return USB_ERROR_STALL;
                }
            }
            if (setup_packet->bmRequestType == 0xA1) {
                fifo8_reset(&usb_mouse->device_instance.fifo);
                switch(setup_packet->bRequest) {
                    case USB_SETUP_HID_GET_IDLE:
                        fifo8_push(&usb_mouse->device_instance.fifo, 0);
                        break;
                    case USB_SETUP_HID_GET_REPORT:
                    {
                        uint8_t buf[4];
                        usb_mouse_poll_hid(usb_mouse, buf, 4);
                        fifo8_push_all(&usb_mouse->device_instance.fifo, buf, 4);
                        break;
                    }
                    case USB_SETUP_HID_GET_PROTOCOL:
                    {
                        fifo8_push(&usb_mouse->device_instance.fifo, usb_mouse->protocol);
                        break;
                    }
                    default:
                        return usb_parse_control_endpoint(&usb_mouse->device_instance, data, len, pid_token, endpoint, underrun_not_allowed);
                }
            } else if (setup_packet->bmRequestType == 0x21) {
                switch (setup_packet->bRequest) {
                    case USB_SETUP_HID_SET_REPORT:
                        return USB_ERROR_STALL;
                    case USB_SETUP_HID_SET_PROTOCOL:
                        {
                            usb_mouse->protocol = setup_packet->wValue;
                            break;
                        }
                    case USB_SETUP_HID_SET_IDLE:
                        {
                            usb_mouse->idle = setup_packet->wValue >> 8;
                            break;
                        }
                }
            } else {
                return usb_parse_control_endpoint(&usb_mouse->device_instance, data, len, pid_token, endpoint, underrun_not_allowed);
            }
            return USB_ERROR_NO_ERROR;
        }
        return usb_parse_control_endpoint(&usb_mouse->device_instance, data, len, pid_token, endpoint, underrun_not_allowed);
    } else if (endpoint == 1 && pid_token == USB_PID_IN) {
        usb_mouse_poll_hid(usb_mouse, data, *len);
        return USB_ERROR_NO_ERROR;
    }
    return USB_ERROR_STALL;
}

static int
usb_mouse_poll(int x, int y, int z, int b, double abs_x, double abs_y, void *priv)
{
    usb_mouse_t *usb_mouse = priv;

    usb_mouse->dx += x;
    usb_mouse->dy -= y;
    usb_mouse->dz -= z;
    usb_mouse->buttons_state = b;

    return 0;
}

static void usb_mouse_handle_reset(void *priv)
{
    usb_mouse_t *usb_mouse = (usb_mouse_t *) priv;

    usb_mouse->dx = 0;
    usb_mouse->dy = 0;
    usb_mouse->dz = 0;
    usb_mouse->buttons_state = 0;
    usb_mouse->device_instance.address = 0;
    usb_mouse->device_instance.current_configuration = 0;
}


void*
usb_mouse_init(const device_t* info)
{
    usb_mouse_t* usb_mouse = (usb_mouse_t*)calloc(1, sizeof(usb_mouse_t));

    usb_mouse->device_instance.string_desc[0] = &str_langid;
    usb_mouse->device_instance.string_desc[STR_MANUFACTURER] = &str_manufacturer;
    usb_mouse->device_instance.string_desc[STR_PRODUCT] = &str_product;
    usb_mouse->device_instance.string_desc[STR_SERIALNUMBER] = &str_serialnumber;
    usb_mouse->device_instance.string_desc[STR_SERIALNUMBER + 1] = NULL;

    usb_mouse->device_instance.conf_desc_items.conf_desc = usb_mouse_conf_desc;
    usb_mouse->device_instance.conf_desc_items.other_descs[0] = &usb_mouse_interface_desc.base;
    usb_mouse->device_instance.conf_desc_items.other_descs[1] = &usb_mouse_endpoint_desc.base;
    usb_mouse->device_instance.conf_desc_items.other_descs[2] = &usb_mouse_hid_desc.base;
    usb_mouse->device_instance.conf_desc_items.other_descs[3] = NULL;

    usb_mouse->device_instance.device_desc = usb_mouse_device_desc;
    usb_mouse->device_instance.priv = usb_mouse;
    usb_mouse->device_instance.device_reset = usb_mouse_handle_reset;
    usb_mouse->device_instance.device_process = usb_mouse_process_transfer;

    fifo8_create(&usb_mouse->device_instance.fifo, 4096);
    
    usb_mouse->port = usb_attach_device(usb_device_inst, &usb_mouse->device_instance, USB_BUS_OHCI);
    if (usb_mouse->port == (uint16_t)-1) {
        fifo8_destroy(&usb_mouse->device_instance.fifo);
        free(usb_mouse);
        return NULL;
    }
    mouse_set_buttons(3);
    return usb_mouse;
}

void
usb_mouse_close(void* priv)
{
    usb_mouse_t* usb_mouse = priv;

    fifo8_destroy(&usb_mouse->device_instance.fifo);
    free(usb_mouse);
}

const device_t mouse_usb_device = {
    .name          = "USB Mouse",
    .internal_name = "usb_mouse",
    .flags         = DEVICE_USB,
    .local         = 0,
    .init          = usb_mouse_init,
    .close         = usb_mouse_close,
    .reset         = NULL,
    { .poll = usb_mouse_poll },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
