#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../ibm.h"
#include "../nmi.h"
#include "../pic.h"
#include "../pit.h"
#include "../mem.h"
#include "../device.h"
#include "../serial.h"
#include "../keyboard_pcjr.h"
#include "../floppy/floppy.h"
#include "../floppy/fdc.h"
#include "../floppy/fdd.h"
#include "../sound/snd_sn76489.h"
#include "machine.h"


void
machine_pcjr_init(machine_t *model)
{
        fdc_add_pcjr();
        pic_init();
        pit_init();
        pit_set_out_func(&pit, 0, pit_irq0_timer_pcjr);
	if (serial_enabled[0])
	        serial_setup(1, 0x2f8, 3);
        keyboard_pcjr_init();
        device_add(&sn76489_device);
	nmi_mask = 0x80;
}
