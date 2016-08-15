/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
void pic_init();
void pic2_init();
void pic_reset();

void picint(uint16_t num);
void picintlevel(uint16_t num);
void picintc(uint16_t num);
uint8_t picinterrupt();
void picclear(int num);
