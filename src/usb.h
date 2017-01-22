/* Copyright holders: Melissa Goad
   see COPYING for more details
*/

typedef struct
{
    uint8_t pid; //low 4 bits are the real pid, top 4 bits are just ~pid
    uint8_t dev_addr;
    uint8_t dev_endpoint;
    int crc5;
    uint16_t crc16;
    uint8_t data[1024];
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
    USB_PID_TOKEN_STALL = 0x1e,
    USB_PID_TOKEN_SETUP = 0x2d,
    USB_PID_TOKEN_PRE = 0x3c,
    USB_PID_TOKEN_DATA1 = 0x4b,
    USB_PID_TOKEN_NAK = 0x5a,
    USB_PID_TOKEN_IN = 0x69,
    USB_PID_TOKEN_SOF = 0xa5,
    USB_PID_TOKEN_DATA0 = 0xc3,
    USB_PID_TOKEN_ACK = 0xd2,
    USB_PID_TOKEN_OUT = 0xe1
} usb_pid_type_t;