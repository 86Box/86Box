#include "../ibm.h"

#include "../device.h"
#include "../disc.h"
#include "../fdc.h"
#include "../fdd.h"
#include "../keyboard_pcjr.h"
#include "../mem.h"
#include "../nmi.h"
#include "../pic.h"
#include "../pit.h"
#include "../serial.h"
#include "../sound/snd_sn76489.h"

#include "machine_pcjr.h"

void machine_pcjr_init(void)
{
	mem_add_bios();
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
