/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include "ibm.h"
#include "mouse.h"

void (*mouse_poll)(int x, int y, int b);
