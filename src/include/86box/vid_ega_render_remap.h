#ifndef VIDEO_EGA_RENDER_REMAP_H
# define VIDEO_EGA_RENDER_REMAP_H

#define VAR_BYTE_MODE      (0 << 0)
#define VAR_WORD_MODE_MA13 (1 << 0)
#define VAR_WORD_MODE_MA15 (2 << 0)
#define VAR_DWORD_MODE     (3 << 0)
#define VAR_MODE_MASK      (3 << 0)
#define VAR_ROW0_MA13      (1 << 2)
#define VAR_ROW1_MA14      (1 << 3)

#define ADDRESS_REMAP_FUNC(nr) \
        static uint32_t address_remap_func_ ## nr(ega_t *ega, uint32_t in_addr) \
        {                                                                       \
                uint32_t out_addr;                                              \
                                                                                \
                switch (nr & VAR_MODE_MASK)                                     \
                {                                                               \
                        case VAR_BYTE_MODE:                                     \
                        out_addr = in_addr;                                     \
                        break;                                                  \
                                                                                \
                        case VAR_WORD_MODE_MA13:                                \
                        out_addr = ((in_addr << 1) & 0x1fff8) |                 \
                                   ((in_addr >> 13) & 0x4) |                    \
                                   (in_addr & ~0x1ffff);                        \
                        break;                                                  \
                                                                                \
                        case VAR_WORD_MODE_MA15:                                \
                        out_addr = ((in_addr << 1) & 0x1fff8) |                 \
                                   ((in_addr >> 15) & 0x4) |                    \
                                   (in_addr & ~0x1ffff);                        \
                        break;                                                  \
                                                                                \
                        case VAR_DWORD_MODE:                                    \
                        out_addr = ((in_addr << 2) & 0x3fff0) |                 \
                                   ((in_addr >> 14) & 0xc) |                    \
                                   (in_addr & ~0x3ffff);                        \
                        break;                                                  \
                }                                                               \
																				\
                if (nr & VAR_ROW0_MA13)                                         \
                        out_addr = (out_addr & ~0x8000) |                \
                                   ((ega->sc & 1) ? 0x8000 : 0);        \
                if (nr & VAR_ROW1_MA14)                                         \
                        out_addr = (out_addr & ~0x10000) |                \
                                   ((ega->sc & 2) ? 0x10000 : 0);        \
                                                                                \
                return out_addr;                                                \
        }

ADDRESS_REMAP_FUNC(0)
ADDRESS_REMAP_FUNC(1)
ADDRESS_REMAP_FUNC(2)
ADDRESS_REMAP_FUNC(3)
ADDRESS_REMAP_FUNC(4)
ADDRESS_REMAP_FUNC(5)
ADDRESS_REMAP_FUNC(6)
ADDRESS_REMAP_FUNC(7)
ADDRESS_REMAP_FUNC(8)
ADDRESS_REMAP_FUNC(9)
ADDRESS_REMAP_FUNC(10)
ADDRESS_REMAP_FUNC(11)
ADDRESS_REMAP_FUNC(12)
ADDRESS_REMAP_FUNC(13)
ADDRESS_REMAP_FUNC(14)
ADDRESS_REMAP_FUNC(15)

static uint32_t (*address_remap_funcs[16])(ega_t *ega, uint32_t in_addr) =
{
        address_remap_func_0,
        address_remap_func_1,
        address_remap_func_2,
        address_remap_func_3,
        address_remap_func_4,
        address_remap_func_5,
        address_remap_func_6,
        address_remap_func_7,
        address_remap_func_8,
        address_remap_func_9,
        address_remap_func_10,
        address_remap_func_11,
        address_remap_func_12,
        address_remap_func_13,
        address_remap_func_14,
        address_remap_func_15
};

void ega_recalc_remap_func(ega_t *ega)
{
        int func_nr;

        if (ega->crtc[0x14] & 0x40)
                func_nr = VAR_DWORD_MODE;
        else if (ega->crtc[0x17] & 0x40)
                func_nr = VAR_BYTE_MODE;
        else if (ega->crtc[0x17] & 0x20)
                func_nr = VAR_WORD_MODE_MA15;
        else
                func_nr = VAR_WORD_MODE_MA13;

        if (!(ega->crtc[0x17] & 0x01))
                func_nr |= VAR_ROW0_MA13;
        if (!(ega->crtc[0x17] & 0x02))
                func_nr |= VAR_ROW1_MA14;

		ega->remap_required = (func_nr != 0);
        ega->remap_func = address_remap_funcs[func_nr];
}

#endif /*VIDEO_RENDER_REMAP_H*/
