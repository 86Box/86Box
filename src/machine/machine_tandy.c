#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../ibm.h"
#include "../nmi.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "../game/gameport.h"
#include "../keyboard_xt.h"
#include "../tandy_eeprom.h"
#include "../tandy_rom.h"
#include "../sound/snd_pssj.h"
#include "../sound/snd_sn76489.h"
#include "machine.h"


void
machine_tandy1k_init(machine_t *model)
{
        TANDY = 1;

        machine_common_init(model);

	mem_add_bios();
        keyboard_tandy_init();
        if (romset == ROM_TANDY)
                device_add(&sn76489_device);
        else
                device_add(&ncr8496_device);
	nmi_init();
	if (romset != ROM_TANDY)
                device_add(&tandy_eeprom_device);
	if (joystick_type != 7)
		device_add(&gameport_device);
}


void
machine_tandy1ksl2_init(machine_t *model)
{
        machine_common_init(model);

	mem_add_bios();
        keyboard_tandy_init();
        device_add(&pssj_device);
	nmi_init();
        device_add(&tandy_rom_device);
        device_add(&tandy_eeprom_device);
	if (joystick_type != 7)  device_add(&gameport_device);
}
