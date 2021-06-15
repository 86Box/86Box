/*Variables :
        byte/word/doubleword mode
        word has MA13/MA15->MA0
        ET4000 treats doubleword as byte
        row 0 -> MA13
        row 1 -> MA14
*/

//S3 - enhanced mode mappings CR31.3 can force doubleword mode
//Cirrus Logic handles SVGA writes seperately
//S3, CL, TGUI blitters need checking

//CL, S3, Mach64, ET4000, Banshee, TGUI all okay
//Still to check - ViRGE, HT216
#define VAR_BYTE_MODE      (0 << 0)
#define VAR_WORD_MODE_MA13 (1 << 0)
#define VAR_WORD_MODE_MA15 (2 << 0)
#define VAR_DWORD_MODE     (3 << 0)
#define VAR_MODE_MASK      (3 << 0)
#define VAR_ROW0_MA13      (1 << 2)
#define VAR_ROW1_MA14      (1 << 3)

#define ADDRESS_REMAP_FUNC(nr) \
        static uint32_t address_remap_func_ ## nr(svga_t *svga, uint32_t in_addr) \
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
                                   ((svga->sc & 1) ? 0x8000 : 0);        \
                if (nr & VAR_ROW1_MA14)                                         \
                        out_addr = (out_addr & ~0x10000) |                \
                                   ((svga->sc & 2) ? 0x10000 : 0);        \
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

static uint32_t (*address_remap_funcs[16])(svga_t *svga, uint32_t in_addr) =
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

void svga_recalc_remap_func(svga_t *svga)
{
        int func_nr;
        
        if (svga->fb_only)
                func_nr = 0;
        else {
                if (svga->force_dword_mode)
                        func_nr = VAR_DWORD_MODE;
                else if (svga->crtc[0x14] & 0x40)
                        func_nr = svga->packed_chain4 ? VAR_BYTE_MODE : VAR_DWORD_MODE;
                else if (svga->crtc[0x17] & 0x40)
                        func_nr = VAR_BYTE_MODE;
                else if (svga->crtc[0x17] & 0x20)
                        func_nr = VAR_WORD_MODE_MA15;
                else
                        func_nr = VAR_WORD_MODE_MA13;
                        
                if (!(svga->crtc[0x17] & 0x01))
                        func_nr |= VAR_ROW0_MA13;
                if (!(svga->crtc[0x17] & 0x02))
                        func_nr |= VAR_ROW1_MA14;
        }

		svga->remap_required = (func_nr != 0);
        svga->remap_func = address_remap_funcs[func_nr];
}
