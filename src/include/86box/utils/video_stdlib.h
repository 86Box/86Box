/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Standard library for implementation of video functionality that is duplicated across multiple cards.
 *
 *
 *
 * Authors: Connor Hyde <mario64crashed@gmail.com> <nomorestarfrost@gmail.com>
 *
 *          Copyright 2025 Connor Hyde
 */

 
/* ROP */
int32_t video_rop_gdi_ternary(int32_t rop, int32_t dst, int32_t pattern, int32_t src, int32_t out);