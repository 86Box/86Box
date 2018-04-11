/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "io.h"
#include "nmi.h"


int nmi_mask;


void nmi_write(uint16_t port, uint8_t val, void *p)
{
        nmi_mask = val & 0x80;
}


void nmi_init(void)
{
        io_sethandler(0x00a0, 0x0001, NULL, NULL, NULL, nmi_write, NULL, NULL,  NULL);
        nmi_mask = 0;
}
