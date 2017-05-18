/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
/*IBM 5150 cassette nonsense
  Calls F979 twice
  Expects CX to be nonzero, BX >$410 and <$540
    CX is loops between bit 4 of $62 changing
    BX is timer difference between calls
  */

#include "ibm.h"
#include "pit.h"
#include "plat_keyboard.h"
#include "plat_mouse.h"

void ppi_reset()
{
        ppi.pa=0x0;
        ppi.pb=0x40;
}

