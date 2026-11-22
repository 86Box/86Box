#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include "m68kcpu.h"

extern void exit(int);

static void fatalerror(const char *format, ...) {
      va_list ap;
      va_start(ap,format);
      vfprintf(stderr,format,ap);  // JFF: fixed. Was using fprintf and arguments were wrong
      va_end(ap);
      exit(1);
}

#define FPCC_N			0x08000000
#define FPCC_Z			0x04000000
#define FPCC_I			0x02000000
#define FPCC_NAN		0x01000000

#define DOUBLE_INFINITY					(unsigned long long)(0x7ff0000000000000)
#define DOUBLE_EXPONENT					(unsigned long long)(0x7ff0000000000000)
#define DOUBLE_MANTISSA					(unsigned long long)(0x000fffffffffffff)

extern flag floatx80_is_nan( floatx80 a );

// masks for packed dwords, positive k-factor
static uint32 pkmask2[18] =
{
	0xffffffff, 0, 0xf0000000, 0xff000000, 0xfff00000, 0xffff0000,
	0xfffff000, 0xffffff00, 0xfffffff0, 0xffffffff,
	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
	0xffffffff, 0xffffffff, 0xffffffff
};

static uint32 pkmask3[18] =
{
	0xffffffff, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0xf0000000, 0xff000000, 0xfff00000, 0xffff0000,
	0xfffff000, 0xffffff00, 0xfffffff0, 0xffffffff,
};

static inline double fx80_to_double(floatx80 fx)
{
	uint64 d;
	double *foo;

	foo = (double *)&d;

	d = floatx80_to_float64(fx);

	return *foo;
}

static inline floatx80 double_to_fx80(double in)
{
	uint64 *d;

	d = (uint64 *)&in;

	return float64_to_floatx80(*d);
}

static inline floatx80 load_extended_float80(uint32 ea)
{
	uint32 d1,d2;
	uint16 d3;
	floatx80 fp;

	d3 = m68ki_read_16(ea);
	d1 = m68ki_read_32(ea+4);
	d2 = m68ki_read_32(ea+8);

	fp.high = d3;
	fp.low = ((uint64)d1<<32) | (d2 & 0xffffffff);

	return fp;
}

static inline void store_extended_float80(uint32 ea, floatx80 fpr)
{
	m68ki_write_16(ea+0, fpr.high);
	m68ki_write_16(ea+2, 0);
	m68ki_write_32(ea+4, (fpr.low>>32)&0xffffffff);
	m68ki_write_32(ea+8, fpr.low&0xffffffff);
}

static inline floatx80 load_pack_float80(uint32 ea)
{
	uint32 dw1, dw2, dw3;
	floatx80 result;
	double tmp;
	char str[128], *ch;

	dw1 = m68ki_read_32(ea);
	dw2 = m68ki_read_32(ea+4);
	dw3 = m68ki_read_32(ea+8);

	ch = &str[0];
	if (dw1 & 0x80000000)	// mantissa sign
	{
		*ch++ = '-';
	}
	*ch++ = (char)((dw1 & 0xf) + '0');
	*ch++ = '.';
	*ch++ = (char)(((dw2 >> 28) & 0xf) + '0');
	*ch++ = (char)(((dw2 >> 24) & 0xf) + '0');
	*ch++ = (char)(((dw2 >> 20) & 0xf) + '0');
	*ch++ = (char)(((dw2 >> 16) & 0xf) + '0');
	*ch++ = (char)(((dw2 >> 12) & 0xf) + '0');
	*ch++ = (char)(((dw2 >> 8)  & 0xf) + '0');
	*ch++ = (char)(((dw2 >> 4)  & 0xf) + '0');
	*ch++ = (char)(((dw2 >> 0)  & 0xf) + '0');
	*ch++ = (char)(((dw3 >> 28) & 0xf) + '0');
	*ch++ = (char)(((dw3 >> 24) & 0xf) + '0');
	*ch++ = (char)(((dw3 >> 20) & 0xf) + '0');
	*ch++ = (char)(((dw3 >> 16) & 0xf) + '0');
	*ch++ = (char)(((dw3 >> 12) & 0xf) + '0');
	*ch++ = (char)(((dw3 >> 8)  & 0xf) + '0');
	*ch++ = (char)(((dw3 >> 4)  & 0xf) + '0');
	*ch++ = (char)(((dw3 >> 0)  & 0xf) + '0');
	*ch++ = 'E';
	if (dw1 & 0x40000000)	// exponent sign
	{
		*ch++ = '-';
	}
	*ch++ = (char)(((dw1 >> 24) & 0xf) + '0');
	*ch++ = (char)(((dw1 >> 20) & 0xf) + '0');
	*ch++ = (char)(((dw1 >> 16) & 0xf) + '0');
	*ch = '\0';

	sscanf(str, "%le", &tmp);

	result = double_to_fx80(tmp);

	return result;
}

static inline void store_pack_float80(uint32 ea, int k, floatx80 fpr)
{
	uint32 dw1, dw2, dw3;
	char str[128], *ch;
	int i, j, exp;

	dw1 = dw2 = dw3 = 0;
	ch = &str[0];

	sprintf(str, "%.16e", fx80_to_double(fpr));

	if (*ch == '-')
	{
		ch++;
		dw1 = 0x80000000;
	}

	if (*ch == '+')
	{
		ch++;
	}

	dw1 |= (*ch++ - '0');

	if (*ch == '.')
	{
		ch++;
	}

	// handle negative k-factor here
	if ((k <= 0) && (k >= -13))
	{
		exp = 0;
		for (i = 0; i < 3; i++)
		{
			if (ch[18+i] >= '0' && ch[18+i] <= '9')
			{
				exp = (exp << 4) | (ch[18+i] - '0');
			}
		}

		if (ch[17] == '-')
		{
			exp = -exp;
		}

		k = -k;
		// last digit is (k + exponent - 1)
		k += (exp - 1);

		// round up the last significant mantissa digit
		if (ch[k+1] >= '5')
		{
			ch[k]++;
		}

		// zero out the rest of the mantissa digits
		for (j = (k+1); j < 16; j++)
		{
			ch[j] = '0';
		}

		// now zero out K to avoid tripping the positive K detection below
		k = 0;
	}

	// crack 8 digits of the mantissa
	for (i = 0; i < 8; i++)
	{
		dw2 <<= 4;
		if (*ch >= '0' && *ch <= '9')
		{
			dw2 |= *ch++ - '0';
		}
	}

	// next 8 digits of the mantissa
	for (i = 0; i < 8; i++)
	{
		dw3 <<= 4;
		if (*ch >= '0' && *ch <= '9')
		dw3 |= *ch++ - '0';
	}

	// handle masking if k is positive
	if (k >= 1)
	{
		if (k <= 17)
		{
			dw2 &= pkmask2[k];
			dw3 &= pkmask3[k];
		}
		else
		{
			dw2 &= pkmask2[17];
			dw3 &= pkmask3[17];
//			m68ki_cpu.fpcr |=  (need to set OPERR bit)
		}
	}

	// finally, crack the exponent
	if (*ch == 'e' || *ch == 'E')
	{
		ch++;
		if (*ch == '-')
		{
			ch++;
			dw1 |= 0x40000000;
		}

		if (*ch == '+')
		{
			ch++;
		}

		j = 0;
		for (i = 0; i < 3; i++)
		{
			if (*ch >= '0' && *ch <= '9')
			{
				j = (j << 4) | (*ch++ - '0');
			}
		}

		dw1 |= (j << 16);
	}

	m68ki_write_32(ea, dw1);
	m68ki_write_32(ea+4, dw2);
	m68ki_write_32(ea+8, dw3);
}

static inline void SET_CONDITION_CODES(floatx80 reg)
{
	REG_FPSR &= ~(FPCC_N|FPCC_Z|FPCC_I|FPCC_NAN);

	// sign flag
	if (reg.high & 0x8000)
	{
		REG_FPSR |= FPCC_N;
	}

	// zero flag
	if (((reg.high & 0x7fff) == 0) && ((reg.low<<1) == 0))
	{
		REG_FPSR |= FPCC_Z;
	}

	// infinity flag
	if (((reg.high & 0x7fff) == 0x7fff) && ((reg.low<<1) == 0))
	{
		REG_FPSR |= FPCC_I;
	}

	// NaN flag
	if (floatx80_is_nan(reg))
	{
		REG_FPSR |= FPCC_NAN;
	}
}

static inline int TEST_CONDITION(int condition)
{
	int n = (REG_FPSR & FPCC_N) != 0;
	int z = (REG_FPSR & FPCC_Z) != 0;
	int nan = (REG_FPSR & FPCC_NAN) != 0;
	int r = 0;
	switch (condition)
	{
		case 0x10:
		case 0x00:		return 0;					// False

		case 0x11:
		case 0x01:		return (z);					// Equal

		case 0x12:
		case 0x02:		return (!(nan || z || n));			// Greater Than

		case 0x13:
		case 0x03:		return (z || !(nan || n));			// Greater or Equal

		case 0x14:
		case 0x04:		return (n && !(nan || z));			// Less Than

		case 0x15:
		case 0x05:		return (z || (n && !nan));			// Less Than or Equal

		case 0x16:
		case 0x06:		return !nan && !z;

		case 0x17:
		case 0x07:		return !nan;

		case 0x18:
		case 0x08:		return nan;

		case 0x19:
		case 0x09:		return nan || z;

		case 0x1a:
		case 0x0a:		return (nan || !(n || z));			// Not Less Than or Equal

		case 0x1b:
		case 0x0b:		return (nan || z || !n);			// Not Less Than

		case 0x1c:
		case 0x0c:		return (nan || (n && !z));			// Not Greater or Equal Than

		case 0x1d:
		case 0x0d:		return (nan || z || n);				// Not Greater Than

		case 0x1e:
		case 0x0e:		return (!z);					// Not Equal

		case 0x1f:
		case 0x0f:		return 1;					// True

		default:		fatalerror("M68kFPU: test_condition: unhandled condition %02X\n", condition);
	}

	return r;
}

static uint8 READ_EA_8(int ea)
{
	int mode = (ea >> 3) & 0x7;
	int reg = (ea & 0x7);

	switch (mode)
	{
		case 0:		// Dn
		{
			return REG_D[reg];
		}
		case 2: 	// (An)
		{
			uint32 ea = REG_A[reg];
			return m68ki_read_8(ea);
		}
		case 3:		// (An)+
		{
			uint32 ea = EA_AY_PI_8();
			return m68ki_read_8(ea);
		}
		case 4:		// -(An)
		{
			uint32 ea = EA_AY_PD_8();
			return m68ki_read_8(ea);
		}
		case 5:		// (d16, An)
		{
			uint32 ea = EA_AY_DI_8();
			return m68ki_read_8(ea);
		}
		case 6:		// (An) + (Xn) + d8
		{
			uint32 ea = EA_AY_IX_8();
			return m68ki_read_8(ea);
		}
		case 7:
		{
			switch (reg)
			{
				case 0:		// (xxx).W
				{
					uint32 ea = (uint32)OPER_I_16();
					return m68ki_read_8(ea);
				}
				case 1:		// (xxx).L
				{
					uint32 d1 = OPER_I_16();
					uint32 d2 = OPER_I_16();
					uint32 ea = (d1 << 16) | d2;
					return m68ki_read_8(ea);
				}
				case 4:		// #<data>
				{
					return  OPER_I_8();
				}
				default:	fatalerror("M68kFPU: READ_EA_8: unhandled mode %d, reg %d at %08X\n", mode, reg, REG_PC);
			}
			break;
		}
		default:	fatalerror("M68kFPU: READ_EA_8: unhandled mode %d, reg %d at %08X\n", mode, reg, REG_PC);
	}

	return 0;
}

static uint16 READ_EA_16(int ea)
{
	int mode = (ea >> 3) & 0x7;
	int reg = (ea & 0x7);

	switch (mode)
	{
		case 0:		// Dn
		{
			return (uint16)(REG_D[reg]);
		}
		case 2:		// (An)
		{
			uint32 ea = REG_A[reg];
			return m68ki_read_16(ea);
		}
		case 3:		// (An)+
		{
			uint32 ea = EA_AY_PI_16();
			return m68ki_read_16(ea);
		}
		case 4:		// -(An)
		{
			uint32 ea = EA_AY_PD_16();
			return m68ki_read_16(ea);
		}
		case 5:		// (d16, An)
		{
			uint32 ea = EA_AY_DI_16();
			return m68ki_read_16(ea);
		}
		case 6:		// (An) + (Xn) + d8
		{
			uint32 ea = EA_AY_IX_16();
			return m68ki_read_16(ea);
		}
		case 7:
		{
			switch (reg)
			{
				case 0:		// (xxx).W
				{
					uint32 ea = (uint32)OPER_I_16();
					return m68ki_read_16(ea);
				}
				case 1:		// (xxx).L
				{
					uint32 d1 = OPER_I_16();
					uint32 d2 = OPER_I_16();
					uint32 ea = (d1 << 16) | d2;
					return m68ki_read_16(ea);
				}
				case 4:		// #<data>
				{
					return OPER_I_16();
				}

				default:	fatalerror("M68kFPU: READ_EA_16: unhandled mode %d, reg %d at %08X\n", mode, reg, REG_PC);
			}
			break;
		}
		default:	fatalerror("M68kFPU: READ_EA_16: unhandled mode %d, reg %d at %08X\n", mode, reg, REG_PC);
	}

	return 0;
}

static uint32 READ_EA_32(int ea)
{
	int mode = (ea >> 3) & 0x7;
	int reg = (ea & 0x7);

	switch (mode)
	{
		case 0:		// Dn
		{
			return REG_D[reg];
		}
		case 2:		// (An)
		{
			uint32 ea = REG_A[reg];
			return m68ki_read_32(ea);
		}
		case 3:		// (An)+
		{
			uint32 ea = EA_AY_PI_32();
			return m68ki_read_32(ea);
		}
		case 4:		// -(An)
		{
			uint32 ea = EA_AY_PD_32();
			return m68ki_read_32(ea);
		}
		case 5:		// (d16, An)
		{
			uint32 ea = EA_AY_DI_32();
			return m68ki_read_32(ea);
		}
		case 6:		// (An) + (Xn) + d8
		{
			uint32 ea = EA_AY_IX_32();
			return m68ki_read_32(ea);
		}
		case 7:
		{
			switch (reg)
			{
				case 0:		// (xxx).W
				{
					uint32 ea = (uint32)OPER_I_16();
					return m68ki_read_32(ea);
				}
				case 1:		// (xxx).L
				{
					uint32 d1 = OPER_I_16();
					uint32 d2 = OPER_I_16();
					uint32 ea = (d1 << 16) | d2;
					return m68ki_read_32(ea);
				}
				case 2:		// (d16, PC)
				{
					uint32 ea = EA_PCDI_32();
					return m68ki_read_32(ea);
				}
				case 4:		// #<data>
				{
					return  OPER_I_32();
				}
				default:	fatalerror("M68kFPU: READ_EA_32: unhandled mode %d, reg %d at %08X\n", mode, reg, REG_PC);
			}
			break;
		}
		default:	fatalerror("M68kFPU: READ_EA_32: unhandled mode %d, reg %d at %08X\n", mode, reg, REG_PC);
	}
	return 0;
}

static uint64 READ_EA_64(int ea)
{
	int mode = (ea >> 3) & 0x7;
	int reg = (ea & 0x7);
	uint32 h1, h2;

	switch (mode)
	{
		case 2:		// (An)
		{
			uint32 ea = REG_A[reg];
			h1 = m68ki_read_32(ea+0);
			h2 = m68ki_read_32(ea+4);
			return  (uint64)(h1) << 32 | (uint64)(h2);
		}
		case 3:		// (An)+
		{
			uint32 ea = REG_A[reg];
			REG_A[reg] += 8;
			h1 = m68ki_read_32(ea+0);
			h2 = m68ki_read_32(ea+4);
			return  (uint64)(h1) << 32 | (uint64)(h2);
		}
		case 4:		// -(An)
		{
			REG_A[reg] -= 8;
			uint32 ea = REG_A[reg];
			h1 = m68ki_read_32(ea+0);
			h2 = m68ki_read_32(ea+4);
			return  (uint64)(h1) << 32 | (uint64)(h2);
		}
		case 5:		// (d16, An)
		{
			uint32 ea = EA_AY_DI_32();
			h1 = m68ki_read_32(ea+0);
			h2 = m68ki_read_32(ea+4);
			return  (uint64)(h1) << 32 | (uint64)(h2);
		}
		case 6:		// (An) + (Xn) + d8
		{
			uint32 ea = EA_AY_IX_16();
			h1 = m68ki_read_32(ea+0);
			h2 = m68ki_read_32(ea+4);
			return  (uint64)(h1) << 32 | (uint64)(h2);
		}
		case 7:
		{
			switch (reg)
			{
				case 1:		// (xxx).L
				{
					uint32 d1 = OPER_I_16();
					uint32 d2 = OPER_I_16();
					uint32 ea = (d1 << 16) | d2;
					h1 = m68ki_read_32(ea+0);
					h2 = m68ki_read_32(ea+4);
					return  (uint64)(h1) << 32 | (uint64)(h2);
				}
				case 4:		// #<data>
				{
					h1 = OPER_I_32();
					h2 = OPER_I_32();
					return  (uint64)(h1) << 32 | (uint64)(h2);
				}
				case 2:		// (d16, PC)
				{
					uint32 ea = EA_PCDI_32();
					h1 = m68ki_read_32(ea+0);
					h2 = m68ki_read_32(ea+4);
					return  (uint64)(h1) << 32 | (uint64)(h2);
				}
				default:	fatalerror("M68kFPU: READ_EA_64: unhandled mode %d, reg %d at %08X\n", mode, reg, REG_PC);
			}
			break;
		}
		default:	fatalerror("M68kFPU: READ_EA_64: unhandled mode %d, reg %d at %08X\n", mode, reg, REG_PC);
	}

	return 0;
}


static floatx80 READ_EA_FPE(int mode, int reg, uint32 di_mode_ea)
{
	floatx80 fpr = {0};

	switch (mode)
	{
		case 2:		// (An)
		{
			uint32 ea = REG_A[reg];
			fpr = load_extended_float80(ea);
			break;
		}
		case 3:		// (An)+
		{
			uint32 ea = REG_A[reg];
			REG_A[reg] += 12;
			fpr = load_extended_float80(ea);
			break;
		}
		case 4:		// -(An)
		{
			REG_A[reg] -= 12;
			uint32 ea = REG_A[reg];
			fpr = load_extended_float80(ea);
			break;
		}
      case 5:		// (d16, An)  (added by JFF)
		{
		  fpr = load_extended_float80(di_mode_ea);
	  	break;
		}
	  case 6:		// (An) + (Xn) + d8
		{
		  uint32 ea = EA_AY_IX_16();
		  fpr = load_extended_float80(ea);
		  break;
		}
		case 7:	// extended modes
		{
			switch (reg)
			{
				case 1:		// (xxx).L
					{
						uint32 d1 = OPER_I_16();
						uint32 d2 = OPER_I_16();
						fpr = load_extended_float80((d1 << 16) | d2);
					}
					break;
				case 2:	// (d16, PC)
					{
						uint32 ea = EA_PCDI_32();
					 	fpr = load_extended_float80(ea);
					}
					break;

				case 3:	// (d16,PC,Dx.w)
					{
						uint32 ea = EA_PCIX_32();
						fpr = load_extended_float80(ea);
					}
					break;
	      		case 4: // immediate (JFF)
				{
				  uint32 ea = REG_PC;
				  fpr = load_extended_float80(ea);
				  REG_PC += 12;
				}
				break;
				default:
					fatalerror("M68kFPU: READ_EA_FPE: unhandled mode %d, reg %d, at %08X\n", mode, reg, REG_PC);
					break;
			}
		}
		break;

		default:	fatalerror("M68kFPU: READ_EA_FPE: unhandled mode %d, reg %d, at %08X\n", mode, reg, REG_PC); break;
	}

	return fpr;
}

static floatx80 READ_EA_PACK(int ea)
{
	floatx80 fpr = {0};
	int mode = (ea >> 3) & 0x7;
	int reg = (ea & 0x7);

	switch (mode)
	{
		case 2:		// (An)
		{
			uint32 ea = REG_A[reg];
			fpr = load_pack_float80(ea);
			break;
		}

		case 3:		// (An)+
		{
			uint32 ea = REG_A[reg];
			REG_A[reg] += 12;
			fpr = load_pack_float80(ea);
			break;
		}
		case 4:		// -(An)
		{
			REG_A[reg] -= 12;
			uint32 ea = REG_A[reg];
			fpr = load_pack_float80(ea);
			break;
		}

		case 7:	// extended modes
		{
			switch (reg)
			{
				case 3:	// (d16,PC,Dx.w)
					{
						uint32 ea = EA_PCIX_32();
						fpr = load_pack_float80(ea);
					}
					break;

				default:
					fatalerror("M68kFPU: READ_EA_PACK: unhandled mode %d, reg %d, at %08X\n", mode, reg, REG_PC);
					break;
			}
		}
		break;

		default:	fatalerror("M68kFPU: READ_EA_PACK: unhandled mode %d, reg %d, at %08X\n", mode, reg, REG_PC); break;
	}

	return fpr;
}

static void WRITE_EA_8(int ea, uint8 data)
{
	int mode = (ea >> 3) & 0x7;
	int reg = (ea & 0x7);

	switch (mode)
	{
		case 0:		// Dn
		{
			REG_D[reg] = data;
			break;
		}
		case 2:		// (An)
		{
			uint32 ea = REG_A[reg];
			m68ki_write_8(ea, data);
			break;
		}
		case 3:		// (An)+
		{
			uint32 ea = EA_AY_PI_8();
			m68ki_write_8(ea, data);
			break;
		}
		case 4:		// -(An)
		{
			uint32 ea = EA_AY_PD_8();
			m68ki_write_8(ea, data);
			break;
		}
		case 5:		// (d16, An)
		{
			uint32 ea = EA_AY_DI_8();
			m68ki_write_8(ea, data);
			break;
		}
		case 6:		// (An) + (Xn) + d8
		{
			uint32 ea = EA_AY_IX_8();
			m68ki_write_8(ea, data);
			break;
		}
		case 7:
		{
			switch (reg)
			{
				case 1:		// (xxx).B
				{
					uint32 d1 = OPER_I_16();
					uint32 d2 = OPER_I_16();
					uint32 ea = (d1 << 16) | d2;
					m68ki_write_8(ea, data);
					break;
				}
				case 2:		// (d16, PC)
				{
					uint32 ea = EA_PCDI_16();
					m68ki_write_8(ea, data);
					break;
				}
				default:	fatalerror("M68kFPU: WRITE_EA_8: unhandled mode %d, reg %d at %08X\n", mode, reg, REG_PC);
			}
			break;
		}
		default:	fatalerror("M68kFPU: WRITE_EA_8: unhandled mode %d, reg %d, data %08X at %08X\n", mode, reg, data, REG_PC);
	}
}

static void WRITE_EA_16(int ea, uint16 data)
{
	int mode = (ea >> 3) & 0x7;
	int reg = (ea & 0x7);

	switch (mode)
	{
		case 0:		// Dn
		{
			REG_D[reg] = data;
			break;
		}
		case 2:		// (An)
		{
			uint32 ea = REG_A[reg];
			m68ki_write_16(ea, data);
			break;
		}
		case 3:		// (An)+
		{
			uint32 ea = EA_AY_PI_16();
			m68ki_write_16(ea, data);
			break;
		}
		case 4:		// -(An)
		{
			uint32 ea = EA_AY_PD_16();
			m68ki_write_16(ea, data);
			break;
		}
		case 5:		// (d16, An)
		{
			uint32 ea = EA_AY_DI_16();
			m68ki_write_16(ea, data);
			break;
		}
		case 6:		// (An) + (Xn) + d8
		{
			uint32 ea = EA_AY_IX_16();
			m68ki_write_16(ea, data);
			break;
		}
		case 7:
		{
			switch (reg)
			{
				case 1:		// (xxx).W
				{
					uint32 d1 = OPER_I_16();
					uint32 d2 = OPER_I_16();
					uint32 ea = (d1 << 16) | d2;
					m68ki_write_16(ea, data);
					break;
				}
				case 2:		// (d16, PC)
				{
					uint32 ea = EA_PCDI_16();
					m68ki_write_16(ea, data);
					break;
				}
				default:	fatalerror("M68kFPU: WRITE_EA_16: unhandled mode %d, reg %d at %08X\n", mode, reg, REG_PC);
			}
			break;
		}
		default:	fatalerror("M68kFPU: WRITE_EA_16: unhandled mode %d, reg %d, data %08X at %08X\n", mode, reg, data, REG_PC);
	}
}

static void WRITE_EA_32(int ea, uint32 data)
{
	int mode = (ea >> 3) & 0x7;
	int reg = (ea & 0x7);

	switch (mode)
	{
		case 0:		// Dn
		{
			REG_D[reg] = data;
			break;
		}
		case 1:		// An
		{
			REG_A[reg] = data;
			break;
		}
		case 2:		// (An)
		{
			uint32 ea = REG_A[reg];
			m68ki_write_32(ea, data);
			break;
		}
		case 3:		// (An)+
		{
			uint32 ea = EA_AY_PI_32();
			m68ki_write_32(ea, data);
			break;
		}
		case 4:		// -(An)
		{
			uint32 ea = EA_AY_PD_32();
			m68ki_write_32(ea, data);
			break;
		}
		case 5:		// (d16, An)
		{
			uint32 ea = EA_AY_DI_32();
			m68ki_write_32(ea, data);
			break;
		}
		case 6:		// (An) + (Xn) + d8
		{
			uint32 ea = EA_AY_IX_32();
			m68ki_write_32(ea, data);
			break;
		}
		case 7:
		{
			switch (reg)
			{
				case 1:		// (xxx).L
				{
					uint32 d1 = OPER_I_16();
					uint32 d2 = OPER_I_16();
					uint32 ea = (d1 << 16) | d2;
					m68ki_write_32(ea, data);
					break;
				}
				case 2:		// (d16, PC)
				{
					uint32 ea = EA_PCDI_32();
					m68ki_write_32(ea, data);
					break;
				}
				default:	fatalerror("M68kFPU: WRITE_EA_32: unhandled mode %d, reg %d at %08X\n", mode, reg, REG_PC);
			}
			break;
		}
		default:	fatalerror("M68kFPU: WRITE_EA_32: unhandled mode %d, reg %d, data %08X at %08X\n", mode, reg, data, REG_PC);
	}
}

static void WRITE_EA_64(int ea, uint64 data)
{
	int mode = (ea >> 3) & 0x7;
	int reg = (ea & 0x7);

	switch (mode)
	{
		case 2:		// (An)
		{
			uint32 ea = REG_A[reg];
			m68ki_write_32(ea, (uint32)(data >> 32));
			m68ki_write_32(ea+4, (uint32)(data));
			break;
		}
		case 3:		// (An)+
		{
			uint32 ea;
			ea = REG_A[reg];
			REG_A[reg] += 8;
			m68ki_write_32(ea+0, (uint32)(data >> 32));
			m68ki_write_32(ea+4, (uint32)(data));
			break;
		}
		case 4:		// -(An)
		{
			uint32 ea;
			REG_A[reg] -= 8;
			ea = REG_A[reg];
			m68ki_write_32(ea+0, (uint32)(data >> 32));
			m68ki_write_32(ea+4, (uint32)(data));
			break;
		}
		case 5:		// (d16, An)
		{
			uint32 ea = EA_AY_DI_32();
			m68ki_write_32(ea+0, (uint32)(data >> 32));
			m68ki_write_32(ea+4, (uint32)(data));
			break;
		}

		case 6:		// (An) + (Xn) + d8
		{
			uint32 ea = EA_AY_IX_16();
			m68ki_write_32(ea+0, (uint32)(data >> 32));
			m68ki_write_32(ea+4, (uint32)(data));
			break;
		}
		case 7:
		{
			switch (reg)
			{
				case 1:		// (xxx).L
				{
					uint32 d1 = OPER_I_16();
					uint32 d2 = OPER_I_16();
					uint32 ea = (d1 << 16) | d2;
					m68ki_write_32(ea+0, (uint32)(data >> 32));
					m68ki_write_32(ea+4, (uint32)(data));
					break;
				}
				case 2:		// (d16, PC)
				{
					uint32 ea = EA_PCDI_32();
					m68ki_write_32(ea+0, (uint32)(data >> 32));
					m68ki_write_32(ea+4, (uint32)(data));
					break;
				}
				default:	fatalerror("M68kFPU: WRITE_EA_64: unhandled mode %d, data %08X%08X at %08X\n", mode, reg, (uint32)(data >> 32), (uint32)(data), REG_PC);
			}
			break;
		}
		default:	fatalerror("M68kFPU: WRITE_EA_64: unhandled mode %d, reg %d, data %08X%08X at %08X\n", mode, reg, (uint32)(data >> 32), (uint32)(data), REG_PC);
	}
}

static void WRITE_EA_FPE(int mode, int reg, floatx80 fpr, uint32 di_mode_ea)
{


	switch (mode)
	{
		case 2:		// (An)
		{
			uint32 ea;
			ea = REG_A[reg];
			store_extended_float80(ea, fpr);
			break;
		}

		case 3:		// (An)+
		{
			uint32 ea;
			ea = REG_A[reg];
			store_extended_float80(ea, fpr);
			REG_A[reg] += 12;
			break;
		}

		case 4:		// -(An)
		{
			uint32 ea;
			REG_A[reg] -= 12;
			ea = REG_A[reg];
			store_extended_float80(ea, fpr);
			break;
		}
    	  case 5:		// (d16, An)  (added by JFF)
		{
		  // EA_AY_DI_32() should not be done here because fmovem would increase
		  // PC each time, reading incorrect displacement & advancing PC too much
		  // uint32 ea = EA_AY_DI_32();
		  store_extended_float80(di_mode_ea, fpr);
	 	 break;

		}
		case 7:
		{
			switch (reg)
			{
				case 1:		// (xxx).L
				{
					uint32 d1 = OPER_I_16();
					uint32 d2 = OPER_I_16();
					uint32 ea = (d1 << 16) | d2;
					store_extended_float80(ea, fpr);
					break;
				}

				default:	fatalerror("M68kFPU: WRITE_EA_FPE: unhandled mode %d, reg %d, at %08X\n", mode, reg, REG_PC);
			}
			break;
		}
		default:	fatalerror("M68kFPU: WRITE_EA_FPE: unhandled mode %d, reg %d, at %08X\n", mode, reg, REG_PC);
	}
}

static void WRITE_EA_PACK(int ea, int k, floatx80 fpr)
{
	int mode = (ea >> 3) & 0x7;
	int reg = (ea & 0x7);

	switch (mode)
	{
		case 2:		// (An)
		{
			uint32 ea;
			ea = REG_A[reg];
			store_pack_float80(ea, k, fpr);
			break;
		}

		case 3:		// (An)+
		{
			uint32 ea;
			ea = REG_A[reg];
			store_pack_float80(ea, k, fpr);
			REG_A[reg] += 12;
			break;
		}

		case 4:		// -(An)
		{
			uint32 ea;
			REG_A[reg] -= 12;
			ea = REG_A[reg];
			store_pack_float80(ea, k, fpr);
			break;
		}

		case 7:
		{
			switch (reg)
			{
				default:	fatalerror("M68kFPU: WRITE_EA_PACK: unhandled mode %d, reg %d, at %08X\n", mode, reg, REG_PC);
			}
		}
		break;
		default:	fatalerror("M68kFPU: WRITE_EA_PACK: unhandled mode %d, reg %d, at %08X\n", mode, reg, REG_PC);
	}
}

static inline int is_inf(floatx80 reg) {
	if (((reg.high & 0x7fff) == 0x7fff) && ((reg.low<<1) == 0))
		return reg.high & 0x8000 ? -1 : 1;
	return 0;
}

static void fpgen_rm_reg(uint16 w2)
{
	int ea = REG_IR & 0x3f;
	int rm = (w2 >> 14) & 0x1;
	int src = (w2 >> 10) & 0x7;
	int dst = (w2 >>  7) & 0x7;
	int opmode = w2 & 0x7f;
	floatx80 source;
	int round;

	// fmovecr #$f, fp0	f200 5c0f

	if (rm)
	{
		switch (src)
		{
			case 0:		// Long-Word Integer
			{
				sint32 d = READ_EA_32(ea);
				source = int32_to_floatx80(d);
				break;
			}
			case 1:		// Single-precision Real
			{
				uint32 d = READ_EA_32(ea);
				source = float32_to_floatx80(d);
				break;
			}
			case 2:		// Extended-precision Real
			{
	  	    	int imode = (ea >> 3) & 0x7;
	  	    	int reg = (ea & 0x7);
		      	uint32 di_mode_ea = imode == 5 ? (REG_A[reg]+MAKE_INT_16(m68ki_read_imm_16())) : 0;
		      	source = READ_EA_FPE(imode,reg,di_mode_ea);
			  	break;
			}
			case 3:		// Packed-decimal Real
			{
				source = READ_EA_PACK(ea);
				break;
			}
			case 4:		// Word Integer
			{
				sint16 d = READ_EA_16(ea);
				source = int32_to_floatx80((sint32)d);
				break;
			}
			case 5:		// Double-precision Real
			{
				uint64 d = READ_EA_64(ea);

				source = float64_to_floatx80(d);
				break;
			}
			case 6:		// Byte Integer
			{
				sint8 d = READ_EA_8(ea);
				source = int32_to_floatx80((sint32)d);
				break;
			}
			case 7:		// FMOVECR load from constant ROM
			{
				switch (w2 & 0x7f)
				{
					case 0x0:	// Pi
						source.high = 0x4000;
						source.low = U64(0xc90fdaa22168c235);
						break;

					case 0xb:	// log10(2)
						source.high = 0x3ffd;
						source.low = U64(0x9a209a84fbcff798);
						break;

					case 0xc:	// e
						source.high = 0x4000;
						source.low = U64(0xadf85458a2bb4a9b);
						break;

					case 0xd:	// log2(e)
						source.high = 0x3fff;
						source.low = U64(0xb8aa3b295c17f0bc);
						break;

					case 0xe:	// log10(e)
						source.high = 0x3ffd;
						source.low = U64(0xde5bd8a937287195);
						break;

					case 0xf:	// 0.0
						source = int32_to_floatx80((sint32)0);
						break;

					case 0x30:	// ln(2)
						source.high = 0x3ffe;
						source.low = U64(0xb17217f7d1cf79ac);
						break;

					case 0x31:	// ln(10)
						source.high = 0x4000;
						source.low = U64(0x935d8dddaaa8ac17);
						break;

					case 0x32:	// 1 (or 100?  manuals are unclear, but 1 would make more sense)
						source = int32_to_floatx80((sint32)1);
						break;

					case 0x33:	// 10^1
						source = int32_to_floatx80((sint32)10);
						break;

					case 0x34:	// 10^2
						source = int32_to_floatx80((sint32)10*10);
						break;

					case 0x35:	// 10^4
						source = int32_to_floatx80((sint32)10000);
						break;

					case 0x36:  // 10^8
						source = double_to_fx80(1e8);
						break;

					case 0x37:  // 10^16
						source = double_to_fx80(1e16);
						break;

					case 0x38:  // 10^32
						source = double_to_fx80(1e32);
						break;

					case 0x39:  // 10^64
						source = double_to_fx80(1e64);
						break;

					case 0x3a:  // 10^128
						source = double_to_fx80(1e128);
						break;

					case 0x3b:  // 10^256
						source = double_to_fx80(1e256);
						break;

					case 0x3c:  // 10^512
						source = double_to_fx80(1e256);
						source = floatx80_mul(source, source);
						break;

					case 0x3d:  // 10^1024
						source = double_to_fx80(1e256);
						source = floatx80_mul(source, source);
						source = floatx80_mul(source, source);
						break;

					case 0x3e:  // 10^2048
						source = double_to_fx80(1e256);
						source = floatx80_mul(source, source);
						source = floatx80_mul(source, source);
						source = floatx80_mul(source, source);
						break;

					case 0x3f:  // 10^4096
						source = double_to_fx80(1e256);
						source = floatx80_mul(source, source);
						source = floatx80_mul(source, source);
						source = floatx80_mul(source, source);
						source = floatx80_mul(source, source);
						break;

					default:
						source = int32_to_floatx80((sint32)0);
						break;
				}

				// handle it right here, the usual opmode bits aren't valid in the FMOVECR case
				REG_FP[dst] = source;
	     		SET_CONDITION_CODES(REG_FP[dst]); // JFF when destination is a register, we HAVE to update FPCR
				USE_CYCLES(4);
				return;
			}
			default:	fatalerror("fmove_rm_reg: invalid source specifier %x at %08X\n", src, REG_PC-4);
		}
	}
	else
	{
		source = REG_FP[src];
	}

	if ((opmode & 0x44) == 0x44)
	{
		round = 2;
		opmode &= ~0x44;
	} else if (opmode & 0x40)
	{
		round = 1;
		opmode &= ~0x40;
	} else
		round = 0;

	switch (opmode)
	{
		case 0x00:		// FMOVE
		{
			REG_FP[dst] = source;
		    SET_CONDITION_CODES(REG_FP[dst]);  // JFF needs update condition codes
			USE_CYCLES(4);
			break;
		}
		case 0x01:		// Fsint
		{
			sint32 temp;
			temp = floatx80_to_int32(source);
			REG_FP[dst] = int32_to_floatx80(temp);
	  		SET_CONDITION_CODES(REG_FP[dst]);  // JFF needs update condition codes
			break;
		}
		case 0x03:		// FsintRZ
		{
			sint32 temp;
			temp = floatx80_to_int32_round_to_zero(source);
			REG_FP[dst] = int32_to_floatx80(temp);
			SET_CONDITION_CODES(REG_FP[dst]);  // JFF needs update condition codes
			break;
		}
		case 0x04:		// FSQRT
		{
			REG_FP[dst] = floatx80_sqrt(source);
			SET_CONDITION_CODES(REG_FP[dst]);
			USE_CYCLES(109);
			break;
		}
		case 0x18:		// FABS
		{
			REG_FP[dst] = source;
			REG_FP[dst].high &= 0x7fff;
			SET_CONDITION_CODES(REG_FP[dst]);
			USE_CYCLES(3);
			break;
		}
		case 0x1a:		// FNEG
		{
			REG_FP[dst] = source;
			REG_FP[dst].high ^= 0x8000;
			SET_CONDITION_CODES(REG_FP[dst]);
			USE_CYCLES(3);
			break;
		}
		case 0xe:		// SIN
			REG_FP[dst] = double_to_fx80(sin(fx80_to_double(source)));
	    	SET_CONDITION_CODES(REG_FP[dst]); // JFF
			USE_CYCLES(400);
			break;
		case 0x1d:		// COS
			REG_FP[dst] = double_to_fx80(cos(fx80_to_double(source)));
	    	SET_CONDITION_CODES(REG_FP[dst]); // JFF
			USE_CYCLES(400);
			break;
		case 0x30:		// SINCOS
		case 0x31:		// SINCOS
		case 0x32:		// SINCOS
		case 0x33:		// SINCOS
		case 0x34:		// SINCOS
		case 0x35:		// SINCOS
		case 0x36:		// SINCOS
		case 0x37:		// SINCOS
		{
			double ds = fx80_to_double(source);
			REG_FP[dst] = double_to_fx80(sin(ds));
			REG_FP[opmode&7] = double_to_fx80(cos(ds));
	    	SET_CONDITION_CODES(REG_FP[dst]); // JFF
			USE_CYCLES(400);
			break;
		}
		case 0x1e:		// FGETEXP
		{
			sint16 temp;
			temp = source.high;	// get the exponent
			temp -= 0x3fff;	// take off the bias
			REG_FP[dst] = double_to_fx80((double)temp);
			SET_CONDITION_CODES(REG_FP[dst]);
			USE_CYCLES(6);
			break;
		}
		case 0x20:		// FDIV
		{
			REG_FP[dst] = floatx80_div(REG_FP[dst], source);
		    SET_CONDITION_CODES(REG_FP[dst]); // JFF
			USE_CYCLES(43);
			break;
		}
		case 0x21:		// FMOD
		{
			REG_FP[dst] = floatx80_rem(REG_FP[dst], source);
		    	SET_CONDITION_CODES(REG_FP[dst]);
			USE_CYCLES(43);
			break;
		}
		case 0x24:		// FSGLDIV
		{
			REG_FP[dst] = double_to_fx80((float)fx80_to_double(floatx80_div(REG_FP[dst], source)));
		    	SET_CONDITION_CODES(REG_FP[dst]); // JFF
			USE_CYCLES(43);
			break;
		}
		case 0x22:		// FADD
		{
			REG_FP[dst] = floatx80_add(REG_FP[dst], source);
			SET_CONDITION_CODES(REG_FP[dst]);
			USE_CYCLES(9);
			break;
		}
		case 0x23:		// FMUL
		{
			REG_FP[dst] = floatx80_mul(REG_FP[dst], source);
			SET_CONDITION_CODES(REG_FP[dst]);
			USE_CYCLES(11);
			break;
		}
		case 0x27:		// FSGLMUL
		{
			REG_FP[dst] = double_to_fx80((float)fx80_to_double(floatx80_mul(REG_FP[dst], source)));
			SET_CONDITION_CODES(REG_FP[dst]);
			USE_CYCLES(11);
			break;
		}
		case 0x25:		// FREM
		{
			REG_FP[dst] = floatx80_rem(REG_FP[dst], source);
			SET_CONDITION_CODES(REG_FP[dst]);
			USE_CYCLES(43);	// guess
			break;
		}
		case 0x28:		// FSUB
		{
			REG_FP[dst] = floatx80_sub(REG_FP[dst], source);
			SET_CONDITION_CODES(REG_FP[dst]);
			USE_CYCLES(9);
			break;
		}
		case 0x38:		// FCMP
		{
			floatx80 res;
			// handle inf in comparison if there is no nan.
			int d = is_inf(REG_FP[dst]);
			int s = is_inf(source);
			if (!floatx80_is_nan(REG_FP[dst]) && !floatx80_is_nan(source) && (d || s))
			{
				REG_FPSR &= ~(FPCC_N|FPCC_Z|FPCC_I|FPCC_NAN);

				if (s < 0) {
					if (d < 0)
						REG_FPSR |= FPCC_N | FPCC_Z;
				} else
				if (s > 0) {
					if (d > 0)
						REG_FPSR |= FPCC_Z;
					else
						REG_FPSR |= FPCC_N;
				} else
				if (d < 0)
					REG_FPSR |= FPCC_N;

			} else {
			res = floatx80_sub(REG_FP[dst], source);
			SET_CONDITION_CODES(res);
			}
			USE_CYCLES(7);
			break;
		}
		case 0x3a:		// FTST
		{
			floatx80 res;
			res = source;
			SET_CONDITION_CODES(res);
			USE_CYCLES(7);
			break;
		}

		default:	fatalerror("fpgen_rm_reg: unimplemented opmode %02X at %08X\n", opmode, REG_PC-4);
	}
	if (round == 1)
	{
		// round to single
		REG_FP[dst] = double_to_fx80((float)fx80_to_double(REG_FP[dst]));
	} else if (round == 2)
	{
		// round to double
		REG_FP[dst] = double_to_fx80(fx80_to_double(REG_FP[dst]));
	}

}

static void fmove_reg_mem(uint16 w2)
{
	int ea = REG_IR & 0x3f;
	int src = (w2 >>  7) & 0x7;
	int dst = (w2 >> 10) & 0x7;
	int k = (w2 & 0x7f);

	switch (dst)
	{
		case 0:		// Long-Word Integer
		{
			sint32 d = (sint32)floatx80_to_int32(REG_FP[src]);
			WRITE_EA_32(ea, d);
			break;
		}
		case 1:		// Single-precision Real
		{
			uint32 d = floatx80_to_float32(REG_FP[src]);
			WRITE_EA_32(ea, d);
			break;
		}
		case 2:		// Extended-precision Real
		{
		  	int mode = (ea >> 3) & 0x7;
		  	int reg = (ea & 0x7);
		  	uint32 di_mode_ea = mode == 5 ? (REG_A[reg]+MAKE_INT_16(m68ki_read_imm_16())) : 0;
			WRITE_EA_FPE(mode, reg, REG_FP[src], di_mode_ea);
			break;
		}
		case 3:		// Packed-decimal Real with Static K-factor
		{
			// sign-extend k
			k = (k & 0x40) ? (k | 0xffffff80) : (k & 0x7f);
			WRITE_EA_PACK(ea, k, REG_FP[src]);
			break;
		}
		case 4:		// Word Integer
		{
			WRITE_EA_16(ea, (sint16)floatx80_to_int32(REG_FP[src]));
			break;
		}
		case 5:		// Double-precision Real
		{
			uint64 d;

			d = floatx80_to_float64(REG_FP[src]);

			WRITE_EA_64(ea, d);
			break;
		}
		case 6:		// Byte Integer
		{
			WRITE_EA_8(ea, (sint8)floatx80_to_int32(REG_FP[src]));
			break;
		}
		case 7:		// Packed-decimal Real with Dynamic K-factor
		{
			WRITE_EA_PACK(ea, REG_D[k>>4], REG_FP[src]);
			break;
		}
	}

	USE_CYCLES(12);
}

static void fmove_fpcr(uint16 w2)
{
	int ea = REG_IR & 0x3f;
	int dir = (w2 >> 13) & 0x1;
	int reg = (w2 >> 10) & 0x7;

	if (dir)	// From system control reg to <ea>
	{
		if (reg & 4) WRITE_EA_32(ea, REG_FPCR);
		if (reg & 2) WRITE_EA_32(ea, REG_FPSR);
		if (reg & 1) WRITE_EA_32(ea, REG_FPIAR);
	}
	else		// From <ea> to system control reg
	{
      if (reg & 4) 
		{
		  REG_FPCR = READ_EA_32(ea);
		  // JFF: need to update rounding mode from softfloat module
		  float_rounding_mode = (REG_FPCR >> 4) & 0x3;
		}
		if (reg & 2) REG_FPSR = READ_EA_32(ea);
		if (reg & 1) REG_FPIAR = READ_EA_32(ea);
	}

	USE_CYCLES(10);
}

static void fmovem(uint16 w2)
{
	int i;
	int ea = REG_IR & 0x3f;
	int dir = (w2 >> 13) & 0x1;
	int mode = (w2 >> 11) & 0x3;
	int reglist = w2 & 0xff;

	if (dir)	// From FP regs to mem
	{
		switch (mode)
	{
	  	case 2:		// (JFF): Static register list, postincrement or control addressing mode.     
	    {
	      int imode = (ea >> 3) & 0x7;
	      int reg = (ea & 0x7);
	      int di_mode = imode == 5;	      
	      uint32 di_mode_ea = di_mode ? (REG_A[reg]+MAKE_INT_16(m68ki_read_imm_16())) : 0;
	      for (i=0; i < 8; i++)
			{
			  if (reglist & (1 << i))
			    {
			      WRITE_EA_FPE(imode,reg, REG_FP[7-i],di_mode_ea);
			      USE_CYCLES(2);
			      if (di_mode)
				{
				  di_mode_ea += 12;
				}
		    	}
			}
	      break;
	   	 }
			case 0:		// Static register list, predecrement addressing mode
			{
	      int imode = (ea >> 3) & 0x7;
	      int reg = (ea & 0x7);
	      // the "di_mode_ea" parameter kludge is required here else WRITE_EA_FPE would have
	      // to call EA_AY_DI_32() (that advances PC & reads displacement) each time
	      // when the proper behaviour is 1) read once, 2) increment ea for each matching register
	      // this forces to pre-read the mode (named "imode") so we can decide to read displacement, only once
	      int di_mode = imode == 5;	      
	      uint32 di_mode_ea =  di_mode ? (REG_A[reg]+MAKE_INT_16(m68ki_read_imm_16())) : 0;
				for (i=0; i < 8; i++)
				{
					if (reglist & (1 << i))
					{
		 			    WRITE_EA_FPE(imode,reg, REG_FP[i],di_mode_ea);
						USE_CYCLES(2);
					    if (di_mode)
						{
						  di_mode_ea += 12;
						}
					}
				}
				break;
			}

			default:	fatalerror("040fpu0: FMOVEM: mode %d unimplemented at %08X\n", mode, REG_PC-4);
		}
	}
	else		// From mem to FP regs
	{
		switch (mode)
		{
			case 2:		// Static register list, postincrement addressing mode
			{
		      int imode = (ea >> 3) & 0x7;
		      int reg = (ea & 0x7);
		      int di_mode = imode == 5;	    
		      uint32 di_mode_ea = di_mode ? (REG_A[reg]+MAKE_INT_16(m68ki_read_imm_16())) : 0;
				for (i=0; i < 8; i++)
				{
					if (reglist & (1 << i))
					{
		   			   	REG_FP[7-i] = READ_EA_FPE(imode,reg,di_mode_ea);
						USE_CYCLES(2);
					    if (di_mode)
						{
						  di_mode_ea += 12;
						}
					}
				}
				break;
			}

			default:	fatalerror("040fpu0: FMOVEM: mode %d unimplemented at %08X\n", mode, REG_PC-4);
		}
	}
}

static void fscc(void)
{
  // added by JFF, this seems to work properly now 
  int condition = OPER_I_16() & 0x3f;

  int cc = TEST_CONDITION(condition);
  int mode = (REG_IR & 0x38) >> 3;
  int v = (cc ? 0xff : 0x00);
  
  switch (mode)
  {
  case 0:  // fscc Dx
    {
      // If the specified floating-point condition is true, sets the byte integer operand at
      // the destination to TRUE (all ones); otherwise, sets the byte to FALSE (all zeros).
      
      REG_D[REG_IR & 7] = (REG_D[REG_IR & 7] & 0xFFFFFF00) | v;
      break;
    }
    case 5: // (disp,Ax)
    {
    int reg = REG_IR & 7;
    uint32 ea = REG_A[reg]+MAKE_INT_16(m68ki_read_imm_16());
    m68ki_write_8(ea,v);
    break;
    }
    
  default:
    {
      // unimplemented see fpu_uae.cpp around line 1300
      fatalerror("040fpu0: fscc: mode %d not implemented at %08X\n", mode, REG_PC-4);
    }
    }
  USE_CYCLES(7);  // JFF unsure of the number of cycles!!
}
static void fbcc16(void)
{
	sint32 offset;
	int condition = REG_IR & 0x3f;

	offset = (sint16)(OPER_I_16());

	// TODO: condition and jump!!!
	if (TEST_CONDITION(condition))
	{
		m68ki_trace_t0();			   /* auto-disable (see m68kcpu.h) */
		m68ki_branch_16(offset-2);
	}

	USE_CYCLES(7);
	}

static void fbcc32(void)
{
	sint32 offset;
	int condition = REG_IR & 0x3f;

	offset = OPER_I_32();

	// TODO: condition and jump!!!
	if (TEST_CONDITION(condition))
	{
		m68ki_trace_t0();			   /* auto-disable (see m68kcpu.h) */
		m68ki_branch_32(offset-4);
	}

	USE_CYCLES(7);
}


void m68040_fpu_op0(void)
{
	m68ki_cpu.fpu_just_reset = 0;

	switch ((REG_IR >> 6) & 0x3)
	{
		case 0:
		{
			uint16 w2 = OPER_I_16();
			switch ((w2 >> 13) & 0x7)
			{
				case 0x0:	// FPU ALU FP, FP
				case 0x2:	// FPU ALU ea, FP
				{
					fpgen_rm_reg(w2);
					break;
				}

				case 0x3:	// FMOVE FP, ea
				{
					fmove_reg_mem(w2);
					break;
				}

				case 0x4:	// FMOVEM ea, FPCR
				case 0x5:	// FMOVEM FPCR, ea
				{
					fmove_fpcr(w2);
					break;
				}

				case 0x6:	// FMOVEM ea, list
				case 0x7:	// FMOVEM list, ea
				{
					fmovem(w2);
					break;
				}

				default:	fatalerror("M68kFPU: unimplemented subop %d at %08X\n", (w2 >> 13) & 0x7, REG_PC-4);
			}
			break;
		}

	    case 1:           // FScc (JFF)
		{
		  fscc();
		  break;
		}
		case 2:		// FBcc disp16
		{
			fbcc16();
			break;
		}
		case 3:		// FBcc disp32
		{
			fbcc32();
			break;
		}

      default:	fatalerror("M68kFPU: unimplemented main op %d at %08X\n", (m68ki_cpu.ir >> 6) & 0x3,  REG_PC-4);
	}
}

static void perform_fsave(uint32 addr, int inc)
{
	if (inc)
	{
		// 68881 IDLE, version 0x1f
		m68ki_write_32(addr, 0x1f180000);
		m68ki_write_32(addr+4, 0);
		m68ki_write_32(addr+8, 0);
		m68ki_write_32(addr+12, 0);
		m68ki_write_32(addr+16, 0);
		m68ki_write_32(addr+20, 0);
		m68ki_write_32(addr+24, 0x70000000);
	}
	else
	{
		m68ki_write_32(addr, 0x70000000);
		m68ki_write_32(addr-4, 0);
		m68ki_write_32(addr-8, 0);
		m68ki_write_32(addr-12, 0);
		m68ki_write_32(addr-16, 0);
		m68ki_write_32(addr-20, 0);
		m68ki_write_32(addr-24, 0x1f180000);
	}
}

// FRESTORE on a NULL frame reboots the FPU - all registers to NaN, the 3 status regs to 0
static void do_frestore_null(void)
{
	int i;

	REG_FPCR = 0;
	REG_FPSR = 0;
	REG_FPIAR = 0;
	for (i = 0; i < 8; i++)
	{
		REG_FP[i].high = 0x7fff;
		REG_FP[i].low = U64(0xffffffffffffffff);
	}

	// Mac IIci at 408458e6 wants an FSAVE of a just-restored NULL frame to also be NULL
	// The PRM says it's possible to generate a NULL frame, but not how/when/why.  (need the 68881/68882 manual!)
	m68ki_cpu.fpu_just_reset = 1;
}

void m68040_fpu_op1(void)
{
	int ea = REG_IR & 0x3f;
	int mode = (ea >> 3) & 0x7;
	int reg = (ea & 0x7);
	uint32 addr, temp;

	switch ((REG_IR >> 6) & 0x3)
	{
		case 0:		// FSAVE <ea>
		{
			switch (mode)
			{
				case 3:	// (An)+
		    			addr = EA_AY_PI_32();

					if (m68ki_cpu.fpu_just_reset)
					{
						m68ki_write_32(addr, 0);
					}
					else
					{
						// we normally generate an IDLE frame
						REG_A[reg] += 6*4;
						perform_fsave(addr, 1);
					}
					break;

				case 4: // -(An)
		    			addr = EA_AY_PD_32();

					if (m68ki_cpu.fpu_just_reset)
					{
						m68ki_write_32(addr, 0);
					}
					else
					{
						// we normally generate an IDLE frame
						REG_A[reg] -= 6*4;
						perform_fsave(addr, 0);
					}
					break;

				default:
					fatalerror("M68kFPU: FSAVE unhandled mode %d reg %d at %x\n", mode, reg, REG_PC);
			}
			break;
		}
		break;

		case 1:		// FRESTORE <ea>
		{
			switch (mode)
			{
				case 2: // (An)
					addr = REG_A[reg];
					temp = m68ki_read_32(addr);

					// check for NULL frame
					if (temp & 0xff000000)
					{
						// we don't handle non-NULL frames and there's no pre/post inc/dec to do here
						m68ki_cpu.fpu_just_reset = 0;
					}
					else
					{
						do_frestore_null();
					}
					break;

				case 3:	// (An)+
		    			addr = EA_AY_PI_32();
					temp = m68ki_read_32(addr);

					// check for NULL frame
					if (temp & 0xff000000)
					{
						m68ki_cpu.fpu_just_reset = 0;

						// how about an IDLE frame?
						if ((temp & 0x00ff0000) == 0x00180000)
						{
							REG_A[reg] += 6*4;
						} // check UNIMP
						else if ((temp & 0x00ff0000) == 0x00380000)
						{
							REG_A[reg] += 14*4;
						} // check BUSY
						else if ((temp & 0x00ff0000) == 0x00b40000)
						{
							REG_A[reg] += 45*4;
						}
					}
					else
					{
						do_frestore_null();
					}
					break;

				default:
					fatalerror("M68kFPU: FRESTORE unhandled mode %d reg %d at %x\n", mode, reg, REG_PC);
			}
			break;
		}
		break;

		default:	fatalerror("m68040_fpu_op1: unimplemented op %d at %08X\n", (REG_IR >> 6) & 0x3, REG_PC-2);
	}
}



