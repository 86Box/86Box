/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
/*IBM 5150 cassette nonsense
  Calls F979 twice
  Expects CX to be nonzero, BX >$410 and <$540
    CX is loops between bit 4 of $62 changing
    BX is timer difference between calls
  */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "ibm.h"
#include "pit.h"
#include "ppi.h"


PPI ppi;
int ppispeakon;


void ppi_reset(void)
{
        ppi.pa=0x0;
        ppi.pb=0x40;
}
