/* Copyright holders: Melissa Goad
   see COPYING for more details
*/

typedef struct
{
    uint8_t pid; //low 4 bits are the real pid, top 4 bits are just ~pid
    uint8_t dev_addr;
    uint8_t dev_endpoint;
    uint8_t* data;
    int len;
    void* device;
} usb_packet_t;

typedef enum
{
    USB_DEV_TYPE_NONE = 0,
    USB_DEV_TYPE_MOUSE,
    USB_DEV_TYPE_TABLET,
    USB_DEV_TYPE_KEYPAD,
    USB_DEV_TYPE_DISK,
    USB_DEV_TYPE_CDROM,
    USB_DEV_TYPE_HUB,
    USB_DEV_TYPE_PRINTER
} usb_device_type_t;

typedef enum
{
    USB_PID_TOKEN_SETUP = 0x2d,
    USB_PID_TOKEN_IN = 0x69,
    USB_PID_TOKEN_OUT = 0xe1
} usb_pid_type_t;