#include "ibm.h"
#include "io.h"
#include "floppy/floppy.h"
#include "floppy/fdc.h"
#include "sio.h"


static uint8_t detect_regs[2];


static void superio_detect_write(uint16_t port, uint8_t val, void *priv)
{
        pclog("superio_detect_write : port=%04x = %02X\n", port, val);

	detect_regs[port & 1] = val;

	return;
}


static uint8_t superio_detect_read(uint16_t port, void *priv)
{
        pclog("superio_detect_read : port=%04x = %02X\n", port, detect_regs[port & 1]);

	return detect_regs[port & 1];
}


void superio_detect_init(void)
{
	fdc_remove();
	fdc_add_for_superio();

        io_sethandler(0x24, 0x0002, superio_detect_read, NULL, NULL, superio_detect_write, NULL, NULL,  NULL);
        io_sethandler(0x26, 0x0002, superio_detect_read, NULL, NULL, superio_detect_write, NULL, NULL,  NULL);
        io_sethandler(0x2e, 0x0002, superio_detect_read, NULL, NULL, superio_detect_write, NULL, NULL,  NULL);
        io_sethandler(0x44, 0x0002, superio_detect_read, NULL, NULL, superio_detect_write, NULL, NULL,  NULL);
        io_sethandler(0x46, 0x0002, superio_detect_read, NULL, NULL, superio_detect_write, NULL, NULL,  NULL);
        io_sethandler(0x4e, 0x0002, superio_detect_read, NULL, NULL, superio_detect_write, NULL, NULL,  NULL);
        io_sethandler(0x108, 0x0002, superio_detect_read, NULL, NULL, superio_detect_write, NULL, NULL,  NULL);
        io_sethandler(0x250, 0x0002, superio_detect_read, NULL, NULL, superio_detect_write, NULL, NULL,  NULL);
        io_sethandler(0x370, 0x0002, superio_detect_read, NULL, NULL, superio_detect_write, NULL, NULL,  NULL);
        io_sethandler(0x3f0, 0x0002, superio_detect_read, NULL, NULL, superio_detect_write, NULL, NULL,  NULL);
}
