#include "../ibm.h"

#include "../dma.h"
#include "../disc.h"
#include "../fdd.h"
#include "../fdc.h"
#include "../lpt.h"
#include "../pic.h"
#include "../pit.h"
#include "../serial.h"

#include "machine_common.h"

void machine_common_init(void)
{
	/* System devices first. */
        dma_init();
        pic_init();
        pit_init();

        fdc_add();

	if (lpt_enabled)
	{
		lpt_init();
	}

	if (serial_enabled[0])
	{
		serial_setup(1, SERIAL1_ADDR, SERIAL1_IRQ);
	}

	if (serial_enabled[1])
	{
		serial_setup(2, SERIAL2_ADDR, SERIAL2_IRQ);
	}
}
