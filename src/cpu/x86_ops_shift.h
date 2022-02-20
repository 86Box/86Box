#ifdef USE_NEW_DYNAREC
#define OP_SHIFT_b(c, ea32)                                                             \
        {                                                                               \
                uint8_t temp_orig = temp;                                               \
                if (!c) return 0;                                                       \
                flags_rebuild();                                                        \
                switch (rmdat & 0x38)                                                   \
                {                                                                       \
                        case 0x00: /*ROL b, c*/                                         \
                        temp = (temp << (c & 7)) | (temp >> (8-(c & 7)));               \
                        seteab(temp);      if (cpu_state.abrt) return 1;                     \
                        set_flags_rotate(FLAGS_ROL8, temp);                             \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x08: /*ROR b,CL*/                                         \
                        temp = (temp >> (c & 7)) | (temp << (8-(c & 7)));               \
                        seteab(temp);      if (cpu_state.abrt) return 1;                \
                        set_flags_rotate(FLAGS_ROR8, temp);                             \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x10: /*RCL b,CL*/                                         \
                        temp2 = cpu_state.flags & C_FLAG;                                         \
                        if (is486) CLOCK_CYCLES_ALWAYS(c);                              \
                        while (c > 0)                                                   \
                        {                                                               \
                                tempc = temp2 ? 1 : 0;                                  \
                                temp2 = temp & 0x80;                                    \
                                temp = (temp << 1) | tempc;                             \
                                c--;                                                    \
                        }                                                               \
                        seteab(temp);           if (cpu_state.abrt) return 1;                     \
                        cpu_state.flags &= ~(C_FLAG | V_FLAG);                                    \
                        if (temp2) cpu_state.flags |= C_FLAG;                                     \
                        if ((cpu_state.flags & C_FLAG) ^ (temp >> 7)) cpu_state.flags |= V_FLAG;            \
                        CLOCK_CYCLES((cpu_mod == 3) ? 9 : 10);                                \
                        PREFETCH_RUN((cpu_mod == 3) ? 9 : 10, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x18: /*RCR b,CL*/                                         \
                        temp2 = cpu_state.flags & C_FLAG;                                         \
                        if (is486) CLOCK_CYCLES_ALWAYS(c);                              \
                        while (c > 0)                                                   \
                        {                                                               \
                                tempc = temp2 ? 0x80 : 0;                               \
                                temp2 = temp & 1;                                       \
                                temp = (temp >> 1) | tempc;                             \
                                c--;                                                    \
                        }                                                               \
                        seteab(temp);           if (cpu_state.abrt) return 1;                     \
                        cpu_state.flags &= ~(C_FLAG | V_FLAG);                                    \
                        if (temp2) cpu_state.flags |= C_FLAG;                                     \
                        if ((temp ^ (temp >> 1)) & 0x40) cpu_state.flags |= V_FLAG;               \
                        CLOCK_CYCLES((cpu_mod == 3) ? 9 : 10);                                \
                        PREFETCH_RUN((cpu_mod == 3) ? 9 : 10, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x20: case 0x30: /*SHL b,CL*/                              \
                        seteab(temp << c);      if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SHL8, temp_orig, c, (temp << c) & 0xff);  \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x28: /*SHR b,CL*/                                         \
                        seteab(temp >> c);      if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SHR8, temp_orig, c, temp >> c);           \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x38: /*SAR b,CL*/                                         \
                        temp = (int8_t)temp >> c;                                       \
                        seteab(temp);           if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SAR8, temp_orig, c, temp);                \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                }                                                                       \
        }

#define OP_SHIFT_w(c, ea32)                                                             \
        {                                                                               \
                uint16_t temp_orig = temp;                                              \
                if (!c) return 0;                                                       \
                flags_rebuild();                                                        \
                switch (rmdat & 0x38)                                                   \
                {                                                                       \
                        case 0x00: /*ROL w, c*/                                         \
                        temp = (temp << (c & 15)) | (temp >> (16-(c & 15)));            \
                        seteaw(temp);      if (cpu_state.abrt) return 1;                \
                        set_flags_rotate(FLAGS_ROL16, temp);                            \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                           \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x08: /*ROR w,CL*/                                         \
                        temp = (temp >> (c & 15)) | (temp << (16-(c & 15)));            \
                        seteaw(temp);      if (cpu_state.abrt) return 1;                \
                        set_flags_rotate(FLAGS_ROR16, temp);                            \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                           \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x10: /*RCL w, c*/                                         \
                        temp2 = cpu_state.flags & C_FLAG;                                         \
                        if (is486) CLOCK_CYCLES_ALWAYS(c);                              \
                        while (c > 0)                                                   \
                        {                                                               \
                                tempc = temp2 ? 1 : 0;                                  \
                                temp2 = temp & 0x8000;                                  \
                                temp = (temp << 1) | tempc;                             \
                                c--;                                                    \
                        }                                                               \
                        seteaw(temp);           if (cpu_state.abrt) return 1;                     \
                        cpu_state.flags &= ~(C_FLAG | V_FLAG);                                    \
                        if (temp2) cpu_state.flags |= C_FLAG;                                     \
                        if ((cpu_state.flags & C_FLAG) ^ (temp >> 15)) cpu_state.flags |= V_FLAG;           \
                        CLOCK_CYCLES((cpu_mod == 3) ? 9 : 10);                                \
                        PREFETCH_RUN((cpu_mod == 3) ? 9 : 10, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x18: /*RCR w, c*/                                         \
                        temp2 = cpu_state.flags & C_FLAG;                                         \
                        if (is486) CLOCK_CYCLES_ALWAYS(c);                              \
                        while (c > 0)                                                   \
                        {                                                               \
                                tempc = temp2 ? 0x8000 : 0;                             \
                                temp2 = temp & 1;                                       \
                                temp = (temp >> 1) | tempc;                             \
                                c--;                                                    \
                        }                                                               \
                        seteaw(temp);           if (cpu_state.abrt) return 1;                     \
                        cpu_state.flags &= ~(C_FLAG | V_FLAG);                                    \
                        if (temp2) cpu_state.flags |= C_FLAG;                                     \
                        if ((temp ^ (temp >> 1)) & 0x4000) cpu_state.flags |= V_FLAG;             \
                        CLOCK_CYCLES((cpu_mod == 3) ? 9 : 10);                                \
                        PREFETCH_RUN((cpu_mod == 3) ? 9 : 10, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x20: case 0x30: /*SHL w, c*/                              \
                        seteaw(temp << c);      if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SHL16, temp_orig, c, (temp << c) & 0xffff); \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x28: /*SHR w, c*/                                         \
                        seteaw(temp >> c);      if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SHR16, temp_orig, c, temp >> c);          \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x38: /*SAR w, c*/                                         \
                        temp = (int16_t)temp >> c;                                      \
                        seteaw(temp);           if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SAR16, temp_orig, c, temp);               \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                }                                                                       \
        }

#define OP_SHIFT_l(c, ea32)                                                             \
        {                                                                               \
                uint32_t temp_orig = temp;                                              \
                if (!c) return 0;                                                       \
                flags_rebuild();                                                        \
                switch (rmdat & 0x38)                                                   \
                {                                                                       \
                        case 0x00: /*ROL l, c*/                                         \
                        temp = (temp << c) | (temp >> (32-c));                          \
                        seteal(temp);      if (cpu_state.abrt) return 1;                \
                        set_flags_rotate(FLAGS_ROL32, temp);                            \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                           \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x08: /*ROR l,CL*/                                         \
                        temp = (temp >> c) | (temp << (32-c));                          \
                        seteal(temp);      if (cpu_state.abrt) return 1;                \
                        set_flags_rotate(FLAGS_ROR32, temp);                            \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                           \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x10: /*RCL l, c*/                                         \
                        temp2 = CF_SET();                                               \
                        if (is486) CLOCK_CYCLES_ALWAYS(c);                              \
                        while (c > 0)                                                   \
                        {                                                               \
                                tempc = temp2 ? 1 : 0;                                  \
                                temp2 = temp & 0x80000000;                              \
                                temp = (temp << 1) | tempc;                             \
                                c--;                                                    \
                        }                                                               \
                        seteal(temp);           if (cpu_state.abrt) return 1;                     \
                        cpu_state.flags &= ~(C_FLAG | V_FLAG);                                    \
                        if (temp2) cpu_state.flags |= C_FLAG;                                     \
                        if ((cpu_state.flags & C_FLAG) ^ (temp >> 31)) cpu_state.flags |= V_FLAG;           \
                        CLOCK_CYCLES((cpu_mod == 3) ? 9 : 10);                                \
                        PREFETCH_RUN((cpu_mod == 3) ? 9 : 10, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, ea32); \
                        break;                                                          \
                        case 0x18: /*RCR l, c*/                                         \
                        temp2 = cpu_state.flags & C_FLAG;                                         \
                        if (is486) CLOCK_CYCLES_ALWAYS(c);                              \
                        while (c > 0)                                                   \
                        {                                                               \
                                tempc = temp2 ? 0x80000000 : 0;                         \
                                temp2 = temp & 1;                                       \
                                temp = (temp >> 1) | tempc;                             \
                                c--;                                                    \
                        }                                                               \
                        seteal(temp);           if (cpu_state.abrt) return 1;                     \
                        cpu_state.flags &= ~(C_FLAG | V_FLAG);                                    \
                        if (temp2) cpu_state.flags |= C_FLAG;                                     \
                        if ((temp ^ (temp >> 1)) & 0x40000000) cpu_state.flags |= V_FLAG;         \
                        CLOCK_CYCLES((cpu_mod == 3) ? 9 : 10);                                \
                        PREFETCH_RUN((cpu_mod == 3) ? 9 : 10, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, ea32); \
                        break;                                                          \
                        case 0x20: case 0x30: /*SHL l, c*/                              \
                        seteal(temp << c);      if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SHL32, temp_orig, c, temp << c);          \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, ea32); \
                        break;                                                          \
                        case 0x28: /*SHR l, c*/                                         \
                        seteal(temp >> c);      if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SHR32, temp_orig, c, temp >> c);          \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, ea32); \
                        break;                                                          \
                        case 0x38: /*SAR l, c*/                                         \
                        temp = (int32_t)temp >> c;                                      \
                        seteal(temp);           if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SAR32, temp_orig, c, temp);               \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, ea32); \
                        break;                                                          \
                }                                                                       \
        }
#else
#define OP_SHIFT_b(c, ea32)                                                             \
        {                                                                               \
                uint8_t temp_orig = temp;                                               \
                if (!c) return 0;                                                       \
                flags_rebuild();                                                        \
                switch (rmdat & 0x38)                                                   \
                {                                                                       \
                        case 0x00: /*ROL b, c*/                                         \
                        temp = (temp << (c & 7)) | (temp >> (8-(c & 7)));               \
                        seteab(temp);      if (cpu_state.abrt) return 1;                     \
                        cpu_state.flags &= ~(C_FLAG | V_FLAG);                          \
                        if (temp & 1) cpu_state.flags |= C_FLAG;                        \
                        if ((temp ^ (temp >> 7)) & 1) cpu_state.flags |= V_FLAG;        \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x08: /*ROR b,CL*/                                         \
                        temp = (temp >> (c & 7)) | (temp << (8-(c & 7)));               \
                        seteab(temp);      if (cpu_state.abrt) return 1;                     \
                        cpu_state.flags &= ~(C_FLAG | V_FLAG);                          \
                        if (temp & 0x80) cpu_state.flags |= C_FLAG;                     \
                        if ((temp ^ (temp >> 1)) & 0x40) cpu_state.flags |= V_FLAG;     \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;   														\
                        case 0x10: /*RCL b,CL*/                                         \
                        temp2 = cpu_state.flags & C_FLAG;                                         \
                        if (is486) CLOCK_CYCLES_ALWAYS(c);                              \
                        while (c > 0)                                                   \
                        {                                                               \
                                tempc = temp2 ? 1 : 0;                                  \
                                temp2 = temp & 0x80;                                    \
                                temp = (temp << 1) | tempc;                             \
                                c--;                                                    \
                        }                                                               \
                        seteab(temp);           if (cpu_state.abrt) return 1;                     \
                        cpu_state.flags &= ~(C_FLAG | V_FLAG);                                    \
                        if (temp2) cpu_state.flags |= C_FLAG;                                     \
                        if ((cpu_state.flags & C_FLAG) ^ (temp >> 7)) cpu_state.flags |= V_FLAG;            \
                        CLOCK_CYCLES((cpu_mod == 3) ? 9 : 10);                                \
                        PREFETCH_RUN((cpu_mod == 3) ? 9 : 10, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x18: /*RCR b,CL*/                                         \
                        temp2 = cpu_state.flags & C_FLAG;                                         \
                        if (is486) CLOCK_CYCLES_ALWAYS(c);                              \
                        while (c > 0)                                                   \
                        {                                                               \
                                tempc = temp2 ? 0x80 : 0;                               \
                                temp2 = temp & 1;                                       \
                                temp = (temp >> 1) | tempc;                             \
                                c--;                                                    \
                        }                                                               \
                        seteab(temp);           if (cpu_state.abrt) return 1;                     \
                        cpu_state.flags &= ~(C_FLAG | V_FLAG);                                    \
                        if (temp2) cpu_state.flags |= C_FLAG;                                     \
                        if ((temp ^ (temp >> 1)) & 0x40) cpu_state.flags |= V_FLAG;               \
                        CLOCK_CYCLES((cpu_mod == 3) ? 9 : 10);                                \
                        PREFETCH_RUN((cpu_mod == 3) ? 9 : 10, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x20: case 0x30: /*SHL b,CL*/                              \
                        seteab(temp << c);      if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SHL8, temp_orig, c, (temp << c) & 0xff);  \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x28: /*SHR b,CL*/                                         \
                        seteab(temp >> c);      if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SHR8, temp_orig, c, temp >> c);           \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x38: /*SAR b,CL*/                                         \
                        temp = (int8_t)temp >> c;                                       \
                        seteab(temp);           if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SAR8, temp_orig, c, temp);                \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                }                                                                       \
        }

#define OP_SHIFT_w(c, ea32)                                                             \
        {                                                                               \
                uint16_t temp_orig = temp;                                              \
                if (!c) return 0;                                                       \
                flags_rebuild();                                                        \
                switch (rmdat & 0x38)                                                   \
                {                                                                       \
                        case 0x00: /*ROL w, c*/                                         \
                        temp = (temp << (c & 15)) | (temp >> (16-(c & 15)));            \
                        seteaw(temp);      if (cpu_state.abrt) return 1;                \
                        cpu_state.flags &= ~(C_FLAG | V_FLAG);                          \
                        if (temp & 1) cpu_state.flags |= C_FLAG;                        \
                        if ((temp ^ (temp >> 15)) & 1) cpu_state.flags |= V_FLAG;       \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                           \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x08: /*ROR w,CL*/                                         \
                        temp = (temp >> (c & 15)) | (temp << (16-(c & 15)));            \
                        seteaw(temp);      if (cpu_state.abrt) return 1;                \
                        cpu_state.flags &= ~(C_FLAG | V_FLAG);                          \
                        if (temp & 0x8000) cpu_state.flags |= C_FLAG;                   \
                        if ((temp ^ (temp >> 1)) & 0x4000) cpu_state.flags |= V_FLAG;   \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                           \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x10: /*RCL w, c*/                                         \
                        temp2 = cpu_state.flags & C_FLAG;                                         \
                        if (is486) CLOCK_CYCLES_ALWAYS(c);                              \
                        while (c > 0)                                                   \
                        {                                                               \
                                tempc = temp2 ? 1 : 0;                                  \
                                temp2 = temp & 0x8000;                                  \
                                temp = (temp << 1) | tempc;                             \
                                c--;                                                    \
                        }                                                               \
                        seteaw(temp);           if (cpu_state.abrt) return 1;                     \
                        cpu_state.flags &= ~(C_FLAG | V_FLAG);                                    \
                        if (temp2) cpu_state.flags |= C_FLAG;                                     \
                        if ((cpu_state.flags & C_FLAG) ^ (temp >> 15)) cpu_state.flags |= V_FLAG;           \
                        CLOCK_CYCLES((cpu_mod == 3) ? 9 : 10);                                \
                        PREFETCH_RUN((cpu_mod == 3) ? 9 : 10, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x18: /*RCR w, c*/                                         \
                        temp2 = cpu_state.flags & C_FLAG;                                         \
                        if (is486) CLOCK_CYCLES_ALWAYS(c);                              \
                        while (c > 0)                                                   \
                        {                                                               \
                                tempc = temp2 ? 0x8000 : 0;                             \
                                temp2 = temp & 1;                                       \
                                temp = (temp >> 1) | tempc;                             \
                                c--;                                                    \
                        }                                                               \
                        seteaw(temp);           if (cpu_state.abrt) return 1;                     \
                        cpu_state.flags &= ~(C_FLAG | V_FLAG);                                    \
                        if (temp2) cpu_state.flags |= C_FLAG;                                     \
                        if ((temp ^ (temp >> 1)) & 0x4000) cpu_state.flags |= V_FLAG;             \
                        CLOCK_CYCLES((cpu_mod == 3) ? 9 : 10);                                \
                        PREFETCH_RUN((cpu_mod == 3) ? 9 : 10, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x20: case 0x30: /*SHL w, c*/                              \
                        seteaw(temp << c);      if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SHL16, temp_orig, c, (temp << c) & 0xffff); \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x28: /*SHR w, c*/                                         \
                        seteaw(temp >> c);      if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SHR16, temp_orig, c, temp >> c);          \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x38: /*SAR w, c*/                                         \
                        temp = (int16_t)temp >> c;                                      \
                        seteaw(temp);           if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SAR16, temp_orig, c, temp);               \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                }                                                                       \
        }

#define OP_SHIFT_l(c, ea32)                                                             \
        {                                                                               \
                uint32_t temp_orig = temp;                                              \
                if (!c) return 0;                                                       \
                flags_rebuild();                                                        \
                switch (rmdat & 0x38)                                                   \
                {                                                                       \
                        case 0x00: /*ROL l, c*/                                         \
                        temp = (temp << c) | (temp >> (32-c));                          \
                        seteal(temp);      if (cpu_state.abrt) return 1;                \
                        cpu_state.flags &= ~(C_FLAG | V_FLAG);                          \
                        if (temp & 1) cpu_state.flags |= C_FLAG;                        \
                        if ((temp ^ (temp >> 31)) & 1) cpu_state.flags |= V_FLAG;       \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                           \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x08: /*ROR l,CL*/                                         \
                        temp = (temp >> c) | (temp << (32-c));                          \
                        seteal(temp);      if (cpu_state.abrt) return 1;                \
                        cpu_state.flags &= ~(C_FLAG | V_FLAG);                          \
                        if (temp & 0x80000000) cpu_state.flags |= C_FLAG;               \
                        if ((temp ^ (temp >> 1)) & 0x40000000) cpu_state.flags |= V_FLAG;       \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                           \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x10: /*RCL l, c*/                                         \
                        temp2 = CF_SET();                                               \
                        if (is486) CLOCK_CYCLES_ALWAYS(c);                              \
                        while (c > 0)                                                   \
                        {                                                               \
                                tempc = temp2 ? 1 : 0;                                  \
                                temp2 = temp & 0x80000000;                              \
                                temp = (temp << 1) | tempc;                             \
                                c--;                                                    \
                        }                                                               \
                        seteal(temp);           if (cpu_state.abrt) return 1;                     \
                        cpu_state.flags &= ~(C_FLAG | V_FLAG);                                    \
                        if (temp2) cpu_state.flags |= C_FLAG;                                     \
                        if ((cpu_state.flags & C_FLAG) ^ (temp >> 31)) cpu_state.flags |= V_FLAG;           \
                        CLOCK_CYCLES((cpu_mod == 3) ? 9 : 10);                                \
                        PREFETCH_RUN((cpu_mod == 3) ? 9 : 10, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, ea32); \
                        break;                                                          \
                        case 0x18: /*RCR l, c*/                                         \
                        temp2 = cpu_state.flags & C_FLAG;                                         \
                        if (is486) CLOCK_CYCLES_ALWAYS(c);                              \
                        while (c > 0)                                                   \
                        {                                                               \
                                tempc = temp2 ? 0x80000000 : 0;                         \
                                temp2 = temp & 1;                                       \
                                temp = (temp >> 1) | tempc;                             \
                                c--;                                                    \
                        }                                                               \
                        seteal(temp);           if (cpu_state.abrt) return 1;                     \
                        cpu_state.flags &= ~(C_FLAG | V_FLAG);                                    \
                        if (temp2) cpu_state.flags |= C_FLAG;                                     \
                        if ((temp ^ (temp >> 1)) & 0x40000000) cpu_state.flags |= V_FLAG;         \
                        CLOCK_CYCLES((cpu_mod == 3) ? 9 : 10);                                \
                        PREFETCH_RUN((cpu_mod == 3) ? 9 : 10, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, ea32); \
                        break;                                                          \
                        case 0x20: case 0x30: /*SHL l, c*/                              \
                        seteal(temp << c);      if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SHL32, temp_orig, c, temp << c);          \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, ea32); \
                        break;                                                          \
                        case 0x28: /*SHR l, c*/                                         \
                        seteal(temp >> c);      if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SHR32, temp_orig, c, temp >> c);          \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, ea32); \
                        break;                                                          \
                        case 0x38: /*SAR l, c*/                                         \
                        temp = (int32_t)temp >> c;                                      \
                        seteal(temp);           if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SAR32, temp_orig, c, temp);               \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, ea32); \
                        break;                                                          \
                }                                                                       \
        }
#endif

static int opC0_a16(uint32_t fetchdat)
{
        int c;
        int tempc;
        uint8_t temp, temp2 = 0;

        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
        c = readmemb(cs, cpu_state.pc) & 31; cpu_state.pc++;
        PREFETCH_PREFIX();
        temp = geteab();                if (cpu_state.abrt) return 1;
        OP_SHIFT_b(c, 0);
        return 0;
}
static int opC0_a32(uint32_t fetchdat)
{
        int c;
        int tempc;
        uint8_t temp, temp2 = 0;

        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
        c = readmemb(cs, cpu_state.pc) & 31; cpu_state.pc++;
        PREFETCH_PREFIX();
        temp = geteab();                if (cpu_state.abrt) return 1;
        OP_SHIFT_b(c, 1);
        return 0;
}
static int opC1_w_a16(uint32_t fetchdat)
{
        int c;
        int tempc;
        uint16_t temp, temp2 = 0;

        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
        c = readmemb(cs, cpu_state.pc) & 31; cpu_state.pc++;
        PREFETCH_PREFIX();
        temp = geteaw();                if (cpu_state.abrt) return 1;
        OP_SHIFT_w(c, 0);
        return 0;
}
static int opC1_w_a32(uint32_t fetchdat)
{
        int c;
        int tempc;
        uint16_t temp, temp2 = 0;

        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
        c = readmemb(cs, cpu_state.pc) & 31; cpu_state.pc++;
        PREFETCH_PREFIX();
        temp = geteaw();                if (cpu_state.abrt) return 1;
        OP_SHIFT_w(c, 1);
        return 0;
}
static int opC1_l_a16(uint32_t fetchdat)
{
        int c;
        int tempc;
        uint32_t temp, temp2 = 0;

        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
        c = readmemb(cs, cpu_state.pc) & 31; cpu_state.pc++;
        PREFETCH_PREFIX();
        temp = geteal();                if (cpu_state.abrt) return 1;
        OP_SHIFT_l(c, 0);
        return 0;
}
static int opC1_l_a32(uint32_t fetchdat)
{
        int c;
        int tempc;
        uint32_t temp, temp2 = 0;

        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
        c = readmemb(cs, cpu_state.pc) & 31; cpu_state.pc++;
        PREFETCH_PREFIX();
        temp = geteal();                if (cpu_state.abrt) return 1;
        OP_SHIFT_l(c, 1);
        return 0;
}

static int opD0_a16(uint32_t fetchdat)
{
        int c = 1;
        int tempc;
        uint8_t temp, temp2 = 0;

        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteab();                if (cpu_state.abrt) return 1;
        OP_SHIFT_b(c, 0);
        return 0;
}
static int opD0_a32(uint32_t fetchdat)
{
        int c = 1;
        int tempc;
        uint8_t temp, temp2 = 0;

        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteab();                if (cpu_state.abrt) return 1;
        OP_SHIFT_b(c, 1);
        return 0;
}
static int opD1_w_a16(uint32_t fetchdat)
{
        int c = 1;
        int tempc;
        uint16_t temp, temp2 = 0;

        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteaw();                if (cpu_state.abrt) return 1;
        OP_SHIFT_w(c, 0);
        return 0;
}
static int opD1_w_a32(uint32_t fetchdat)
{
        int c = 1;
        int tempc;
        uint16_t temp, temp2 = 0;

        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteaw();                if (cpu_state.abrt) return 1;
        OP_SHIFT_w(c, 1);
        return 0;
}
static int opD1_l_a16(uint32_t fetchdat)
{
        int c = 1;
        int tempc;
        uint32_t temp, temp2 = 0;

        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteal();                if (cpu_state.abrt) return 1;
        OP_SHIFT_l(c, 0);
        return 0;
}
static int opD1_l_a32(uint32_t fetchdat)
{
        int c = 1;
        int tempc;
        uint32_t temp, temp2 = 0;

        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteal();                if (cpu_state.abrt) return 1;
        OP_SHIFT_l(c, 1);
        return 0;
}

static int opD2_a16(uint32_t fetchdat)
{
        int c;
        int tempc;
        uint8_t temp, temp2 = 0;

        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
        c = CL & 31;
        temp = geteab();                if (cpu_state.abrt) return 1;
        OP_SHIFT_b(c, 0);
        return 0;
}
static int opD2_a32(uint32_t fetchdat)
{
        int c;
        int tempc;
        uint8_t temp, temp2 = 0;

        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
        c = CL & 31;
        temp = geteab();                if (cpu_state.abrt) return 1;
        OP_SHIFT_b(c, 1);
        return 0;
}
static int opD3_w_a16(uint32_t fetchdat)
{
        int c;
        int tempc;
        uint16_t temp, temp2 = 0;

        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
        c = CL & 31;
        temp = geteaw();                if (cpu_state.abrt) return 1;
        OP_SHIFT_w(c, 0);
        return 0;
}
static int opD3_w_a32(uint32_t fetchdat)
{
        int c;
        int tempc;
        uint16_t temp, temp2 = 0;

        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
        c = CL & 31;
        temp = geteaw();                if (cpu_state.abrt) return 1;
        OP_SHIFT_w(c, 1);
        return 0;
}
static int opD3_l_a16(uint32_t fetchdat)
{
        int c;
        int tempc;
        uint32_t temp, temp2 = 0;

        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
        c = CL & 31;
        temp = geteal();                if (cpu_state.abrt) return 1;
        OP_SHIFT_l(c, 0);
        return 0;
}
static int opD3_l_a32(uint32_t fetchdat)
{
        int c;
        int tempc;
        uint32_t temp, temp2 = 0;

        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
        c = CL & 31;
        temp = geteal();                if (cpu_state.abrt) return 1;
        OP_SHIFT_l(c, 1);
        return 0;
}


#define SHLD_w()                                                                \
        if (count)                                                              \
        {                                                                       \
		int tempc;							\
		uint32_t templ;							\
                uint16_t tempw = geteaw();      if (cpu_state.abrt) return 1;             \
                tempc = ((tempw << (count - 1)) & (1 << 15)) ? 1 : 0;       \
                templ = (tempw << 16) | cpu_state.regs[cpu_reg].w;         \
                if (count <= 16) tempw =  templ >> (16 - count);                \
                else             tempw = (templ << count) >> 16;                \
                seteaw(tempw);                  if (cpu_state.abrt) return 1;             \
                setznp16(tempw);                                                \
                flags_rebuild();                                                \
                if (tempc) cpu_state.flags |= C_FLAG;                                     \
        }

#define SHLD_l()                                                                \
        if (count)                                                              \
        {                                                                       \
		int tempc;							\
                uint32_t templ = geteal();      if (cpu_state.abrt) return 1;             \
                tempc = ((templ << (count - 1)) & (1 << 31)) ? 1 : 0;       \
                templ = (templ << count) | (cpu_state.regs[cpu_reg].l >> (32 - count)); \
                seteal(templ);                  if (cpu_state.abrt) return 1;             \
                setznp32(templ);                                                \
                flags_rebuild();                                                \
                if (tempc) cpu_state.flags |= C_FLAG;                                     \
        }


#define SHRD_w()                                                                \
        if (count)                                                              \
        {                  							\
		int tempc;                                                     \
		uint32_t templ;							\
                uint16_t tempw = geteaw();      if (cpu_state.abrt) return 1;             \
                tempc = (tempw >> (count - 1)) & 1;                         \
                templ = tempw | (cpu_state.regs[cpu_reg].w << 16);         \
                tempw = templ >> count;                                         \
                seteaw(tempw);                  if (cpu_state.abrt) return 1;             \
                setznp16(tempw);                                                \
                flags_rebuild();                                                \
                if (tempc) cpu_state.flags |= C_FLAG;                                     \
        }

#define SHRD_l()                                                                \
        if (count)                                                              \
        {                                                                       \
		int tempc;							\
                uint32_t templ = geteal();      if (cpu_state.abrt) return 1;             \
                tempc = (templ >> (count - 1)) & 1;                         \
                templ = (templ >> count) | (cpu_state.regs[cpu_reg].l << (32 - count)); \
                seteal(templ);                  if (cpu_state.abrt) return 1;             \
                setznp32(templ);                                                \
                flags_rebuild();                                                \
                if (tempc) cpu_state.flags |= C_FLAG;                                     \
        }

#define opSHxD(operation)                                                       \
        static int op ## operation ## _i_a16(uint32_t fetchdat)                 \
        {                                                                       \
                int count;                                                      \
                                                                                \
                fetch_ea_16(fetchdat);                                          \
                if (cpu_mod != 3)                                               \
                        SEG_CHECK_WRITE(cpu_state.ea_seg);                      \
                count = getbyte() & 31;                                         \
                operation();                                                    \
                                                                                \
                CLOCK_CYCLES(3);                                                \
                PREFETCH_RUN(3, 3, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, 0); \
                return 0;                                                       \
        }                                                                       \
        static int op ## operation ## _CL_a16(uint32_t fetchdat)                \
        {                                                                       \
                int count;                                                      \
                                                                                \
                fetch_ea_16(fetchdat);                                          \
                if (cpu_mod != 3)                                               \
                        SEG_CHECK_WRITE(cpu_state.ea_seg);                      \
                count = CL & 31;                                                \
                operation();                                                    \
                                                                                \
                CLOCK_CYCLES(3);                                                \
                PREFETCH_RUN(3, 3, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, 0); \
                return 0;                                                       \
        }                                                                       \
        static int op ## operation ## _i_a32(uint32_t fetchdat)                 \
        {                                                                       \
                int count;                                                      \
                                                                                \
                fetch_ea_32(fetchdat);                                          \
                if (cpu_mod != 3)                                               \
                        SEG_CHECK_WRITE(cpu_state.ea_seg);                      \
                count = getbyte() & 31;                                         \
                operation();                                                    \
                                                                                \
                CLOCK_CYCLES(3);                                                \
                PREFETCH_RUN(3, 3, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, 1); \
                return 0;                                                       \
        }                                                                       \
        static int op ## operation ## _CL_a32(uint32_t fetchdat)                \
        {                                                                       \
                int count;                                                      \
                                                                                \
                fetch_ea_32(fetchdat);                                          \
                if (cpu_mod != 3)                                               \
                        SEG_CHECK_WRITE(cpu_state.ea_seg);                      \
                count = CL & 31;                                                \
                operation();                                                    \
                                                                                \
                CLOCK_CYCLES(3);                                                \
                PREFETCH_RUN(3, 3, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, 1); \
                return 0;                                                       \
        }

opSHxD(SHLD_w)
opSHxD(SHLD_l)
opSHxD(SHRD_w)
opSHxD(SHRD_l)
