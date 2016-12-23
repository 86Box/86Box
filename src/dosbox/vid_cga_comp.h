#define Bit8u uint8_t
#define Bit32u uint32_t
#define Bitu unsigned int
#define bool uint8_t

void update_cga16_color(uint8_t cgamode);
void cga_comp_init(int revision);
Bit8u * Composite_Process(uint8_t cgamode, Bit8u border, Bit32u blocks/*, bool doublewidth*/, Bit8u *TempLine);
