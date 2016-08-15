/* Authors: Andrew Jenner
   Extra contributors: Tenshi
   Distributed as is with the Unlicense (PD), see "UNLICENSE"
*/
#define Bit8u uint8_t
#define Bit32u uint32_t
#define Bitu unsigned int
#define bool uint8_t

void update_cga16_color(cga_t *cga);
void cga_comp_init(cga_t *cga);
Bit8u * Composite_Process(cga_t *cga, Bit8u border, Bit32u blocks/*, bool doublewidth*/, Bit8u *TempLine);
