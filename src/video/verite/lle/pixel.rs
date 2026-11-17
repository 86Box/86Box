// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from rust-dos (src/verite/lle).

//! The pixel engine in the V1000's mode ([SPEC] 2.6-2.9, 4): its
//! registers, which `spr` sets and steps feed, and the draws, which write
//! pixels into the card's memory.
//!
//! Parallel draws are 2D: fills, blits and patterns combined with the
//! destination by the ALU mode, a source pixel walked along with each
//! destination pixel. Parameterized draws iterate the parameters DrawCtl
//! names (table 2.9c) and send each pixel through texturing, fog, the Z
//! buffer, blending and dithering. Draws run whole within their
//! instruction, so the engine is never busy.

use super::{Core, mem};

/// A parameter of the engine's iterators, as steps and draws name them.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Param {
    /// A register-file entry the draw updates but the engine ignores.
    None,
    XShadow,
    X,
    Y,
    Z,
    U,
    V,
    Q,
    R,
    G,
    B,
    A,
    F,
    /// Intensity: red, green and blue alike.
    I,
    Cnt,
}

use Param::{A, B, F, G, I, None as N, Q, R, U, V, X, Y, Z};

/// DrawCtl's first pair (table 2.9c) and whether X comes from XShadow.
const PAIR1: [(Param, Param, bool); 16] = [
    (X, N, false),
    (Q, N, true),
    (X, Y, false),
    (Q, Y, true),
    (X, U, false),
    (Q, U, true),
    (X, R, false),
    (Q, R, true),
    (X, A, false),
    (Q, A, true),
    (Z, X, false),
    (N, N, true),
    (X, F, false),
    (Q, F, true),
    (Y, U, true),
    (Q, U, true),
];

/// The third and fourth pairs, and the second's values 0-7.
const PAIR3: [(Param, Param); 16] = [
    (G, B),
    (Z, A),
    (Z, N),
    (N, F),
    (Z, F),
    (N, N),
    (R, F),
    (R, A),
    (R, N),
    (Z, R),
    (N, A),
    (F, A),
    (I, N),
    (Z, I),
    (I, A),
    (I, F),
];

/// The second pair's values 8-31.
const PAIR2: [(Param, Param); 24] = [
    (X, V),
    (Y, V),
    (R, V),
    (N, V),
    (I, V),
    (Z, V),
    (A, V),
    (Z, U),
    (G, B),
    (Z, A),
    (Z, N),
    (N, F),
    (Z, F),
    (F, V),
    (R, F),
    (R, A),
    (R, N),
    (Z, R),
    (N, A),
    (F, A),
    (I, N),
    (Z, I),
    (I, A),
    (I, F),
];

/// The 4x4 dither matrix (table 4.8), by line and column.
const DITHER: [[u32; 4]; 4] = [[0, 12, 3, 15], [7, 11, 4, 8], [13, 1, 14, 2], [10, 6, 9, 5]];

/// DstMode's bits (table 4.4).
mod dst {
    pub const ZBUF_WRITE: u32 = 1 << 16;
    pub const YUV2RGB: u32 = 1 << 18;
    pub const BLEND: u32 = 1 << 19;
    pub const DITHER: u32 = 1 << 20;
    pub const FOG: u32 = 1 << 21;
    pub const NO_PAD: u32 = 1 << 22;
    pub const READ_DISABLE: u32 = 1 << 23;
    pub const BGR: u32 = 1 << 24;
    pub const TRANSP_REJECT: u32 = 1 << 25;
}

/// SrcMode's bits (table 4.1).
mod src {
    pub const FILTER: u32 = 1 << 7;
    pub const NO_PAD: u32 = 1 << 9;
    pub const SWAP_UV: u32 = 1 << 10;
    pub const CHROMA: u32 = 1 << 11;
    pub const Z_SCISSOR: u32 = 1 << 12;
    pub const BGR: u32 = 1 << 13;
    pub const CHROMA_BLACK_YUV: u32 = 1 << 14;
    pub const U_CLAMP: u32 = 1 << 15;
    pub const V_CLAMP: u32 = 1 << 16;
}

#[derive(Clone, Debug)]
pub struct PixelEngine {
    pub src_base: u32,
    pub src_mode: u32,
    pub src_mask: u32,
    pub stride: u32,
    pub dst_base: u32,
    pub dst_mode: u32,
    pub dst_fmt: u32,
    pub pmask: u32,
    pub pattern: u32,
    pub pat_mode: u32,
    pub pat_offset: u32,
    pub scissor_x: u32,
    pub scissor_y: u32,
    pub z_base: u32,
    pub dither_offset: u32,
    pub fg: u32,
    pub bg: u32,
    pub fog_color: u32,
    pub dst_color: u32,
    pub chroma_color: u32,
    pub yuv_contrast: u32,
    pub chroma_mask: u32,
    pub alpha_thres: u32,
    pub spec_color: u32,
    pub tex4pal: [u32; 8],
    pub pick_valid: bool,
    pub pick_hit: bool,
    /// The iterators, as steps and draws last set them: X, Y, U, V and the
    /// colours 16.16, Z 21.11, Q 0.24; Cnt the 16-bit count.
    pub x_shadow: i32,
    pub x_dir: bool,
    pub x: i32,
    pub y: i32,
    pub z: i32,
    pub z_ovf: bool,
    pub u: i32,
    pub v: i32,
    pub u_dir: bool,
    pub v_dir: bool,
    /// SrcMask's U and V masks as U and V entered the engine ([SPEC] 4.2).
    pub u_mask: u32,
    pub v_mask: u32,
    pub q: u32,
    pub r: i32,
    pub g: i32,
    pub b: i32,
    pub a: i32,
    pub f: i32,
    pub cnt: u32,
    pub drawctl: u32,
}

impl Default for PixelEngine {
    fn default() -> Self {
        Self {
            src_base: 0,
            src_mode: 0,
            src_mask: 0x07FF_07FF,
            stride: 0,
            dst_base: 0,
            dst_mode: 0xC,
            dst_fmt: 4,
            pmask: u32::MAX,
            pattern: 0,
            pat_mode: 0,
            pat_offset: 0,
            scissor_x: 0x800,
            scissor_y: 0x800,
            z_base: 0,
            dither_offset: 0,
            fg: 0xFF00_0000,
            bg: 0,
            fog_color: 0,
            dst_color: 0,
            chroma_color: 0,
            yuv_contrast: 41,
            chroma_mask: 0,
            alpha_thres: 0,
            spec_color: 0,
            tex4pal: [0; 8],
            pick_valid: false,
            pick_hit: false,
            x_shadow: 0,
            x_dir: false,
            x: 0,
            y: 0,
            z: 0,
            z_ovf: false,
            u: 0,
            v: 0,
            u_dir: false,
            v_dir: false,
            u_mask: 0x7FF,
            v_mask: 0x7FF,
            q: 1 << 24,
            r: 0,
            g: 0,
            b: 0,
            a: 255 << 16,
            f: 255 << 16,
            cnt: 0,
            drawctl: 0,
        }
    }
}

/// A colour channel of 16.16 as 0-255, saturated as it enters the engine.
fn sat(v: i32) -> i32 {
    (v >> 16).clamp(0, 255)
}

/// The bytes of a pixel of format `fmt` (table 4.1/4.5); 0 for 4 bits.
fn fmt_bytes(fmt: u32) -> u32 {
    match fmt {
        1..=3 => 1,
        8..=0xA => 0,
        0xC | 0xD => 4,
        _ => 2,
    }
}

/// Bytes a line for a Stride field: the sum of its shifts ([SPEC] 4.3).
fn line(code: u32) -> u32 {
    crate::verite::draw::stride(code & 0x7F)
}

fn read(mem: &[u8], a: u32, bytes: u32) -> u32 {
    match bytes {
        1 => mem::read8(mem, a),
        2 => mem::read16(mem, a),
        _ => mem::read32(mem, a),
    }
}

fn write(mem: &mut [u8], a: u32, bytes: u32, v: u32) {
    match bytes {
        1 => mem::write8(mem, a, v),
        2 => mem::write16(mem, a, v),
        _ => mem::write32(mem, a, v),
    }
}

/// The ALU mode `code` (table 4.4) on source and destination: a truth
/// table, bit (S << 1 | D) of the code for each bit.
fn rop(code: u32, s: u32, d: u32) -> u32 {
    let bit = |n: u32| if code >> n & 1 != 0 { u32::MAX } else { 0 };
    !s & !d & bit(0) | !s & d & bit(1) | s & !d & bit(2) | s & d & bit(3)
}

/// Whether the ALU mode's result depends on the destination.
fn rop_reads(code: u32) -> bool {
    (code >> 1 ^ code) & 5 != 0
}

/// A raw pixel replicated across a word, as the chroma key compares
/// texels ([SPEC] 4.14).
fn replicated(raw: u32, bytes: u32) -> u32 {
    match bytes {
        1 => raw * 0x0101_0101,
        2 => raw * 0x0001_0001,
        _ => raw,
    }
}

/// A channel of `n` bits widened to 8: exactly, or with zeros below.
fn widen(v: u32, n: u32, no_pad: bool) -> i32 {
    let v = v & ((1 << n) - 1);
    let max = (1 << n) - 1;
    (if no_pad {
        v << (8 - n)
    } else {
        (v * 255 + max / 2) / max
    }) as i32
}

/// A pixel of format `fmt` as red, green, blue and alpha.
fn decode(fmt: u32, raw: u32, bgr: bool, no_pad: bool) -> [i32; 4] {
    let w = |v: u32, n: u32| widen(v, n, no_pad);
    let mut c = match fmt {
        1 => [w(raw >> 5, 3), w(raw >> 2, 3), w(raw, 2), 255],
        2 => {
            let i = (raw & 0xFF) as i32;
            [i, i, i, 255]
        }
        3 => [255, 255, 255, (raw & 0xFF) as i32],
        5 => [w(raw >> 8, 4), w(raw >> 4, 4), w(raw, 4), w(raw >> 12, 4)],
        6 => [
            w(raw >> 10, 5),
            w(raw >> 5, 5),
            w(raw, 5),
            if raw & 0x8000 != 0 { 255 } else { 0 },
        ],
        0xC => [
            (raw >> 16 & 0xFF) as i32,
            (raw >> 8 & 0xFF) as i32,
            (raw & 0xFF) as i32,
            (raw >> 24) as i32,
        ],
        _ => [w(raw >> 11, 5), w(raw >> 5, 6), w(raw, 5), 255],
    };
    if bgr {
        c.swap(0, 2);
    }
    c
}

/// Red, green, blue and alpha as a pixel of format `fmt`, with the dither
/// value `dither` (0-15) added before each channel loses bits.
fn encode(fmt: u32, mut c: [i32; 4], dither: u32, bgr: bool) -> u32 {
    if bgr {
        c.swap(0, 2);
    }
    let q = |v: i32, n: u32| ((v.clamp(0, 255) as u32 + ((dither << 4) >> n)).min(255)) >> (8 - n);
    let [r, g, b, a] = c;
    match fmt {
        1 => q(r, 3) << 5 | q(g, 3) << 2 | q(b, 2),
        2 => q(r, 8),
        3 => q(a, 8),
        5 => q(a, 4) << 12 | q(r, 4) << 8 | q(g, 4) << 4 | q(b, 4),
        6 => q(a, 1) << 15 | q(r, 5) << 10 | q(g, 5) << 5 | q(b, 5),
        0xC => q(a, 8) << 24 | q(r, 8) << 16 | q(g, 8) << 8 | q(b, 8),
        _ => q(r, 5) << 11 | q(g, 6) << 5 | q(b, 5),
    }
}

fn argb(colour: u32) -> [i32; 4] {
    [
        (colour >> 16 & 0xFF) as i32,
        (colour >> 8 & 0xFF) as i32,
        (colour & 0xFF) as i32,
        (colour >> 24) as i32,
    ]
}

impl PixelEngine {
    /// A parameter written to the engine. Colours update FGColor's field
    /// too; U and V take the SrcMask masks with them.
    pub fn set(&mut self, param: Param, value: u32) {
        let v = value as i32;
        let field = |fg: u32, shift: u32| fg & !(0xFF << shift) | (sat(v) as u32) << shift;
        match param {
            Param::None => {}
            Param::XShadow => self.x_shadow = v,
            Param::X => self.x = v,
            Param::Y => self.y = v,
            Param::Z => {
                self.z = v;
                self.z_ovf = value & 0xF800_0000 != 0 && self.src_mode & src::Z_SCISSOR != 0;
            }
            Param::U | Param::V => {
                let swap = self.src_mode & src::SWAP_UV != 0;
                if (param == Param::U) != swap {
                    self.u = v;
                    self.u_mask = self.src_mask & 0x7FF;
                } else {
                    self.v = v;
                    self.v_mask = self.src_mask >> 16 & 0x7FF;
                }
            }
            Param::Q => self.q = value,
            Param::R => (self.r, self.fg) = (v, field(self.fg, 16)),
            Param::G => (self.g, self.fg) = (v, field(self.fg, 8)),
            Param::B => (self.b, self.fg) = (v, field(self.fg, 0)),
            Param::A => (self.a, self.fg) = (v, field(self.fg, 24)),
            Param::F => self.f = v,
            Param::I => {
                (self.r, self.g, self.b) = (v, v, v);
                self.fg = field(field(field(self.fg, 16), 8), 0);
            }
            Param::Cnt => self.cnt = value >> 16,
        }
    }

    /// FGColor set whole: the colour iterators follow it.
    fn set_fg(&mut self, fg: u32, alpha: bool) {
        self.fg = if alpha {
            fg
        } else {
            self.fg & 0xFF00_0000 | fg & 0x00FF_FFFF
        };
        self.r = ((self.fg >> 16 & 0xFF) as i32) << 16;
        self.g = ((self.fg >> 8 & 0xFF) as i32) << 16;
        self.b = ((self.fg & 0xFF) as i32) << 16;
        self.a = ((self.fg >> 24) as i32) << 16;
    }

    /// The engine's state at `index` (table 10), for StateData and the
    /// RISC's loads from FFE00800h on.
    pub fn state(&self, index: u8) -> u32 {
        match index {
            0 => self.src_base,
            1 => self.src_mode,
            2 => self.src_mask,
            3 => self.stride,
            4 => self.dst_base,
            5 => self.dst_mode,
            6 => self.dst_fmt,
            7 => self.pmask,
            8 => self.pattern,
            12 => self.pat_mode,
            13 => self.pat_offset,
            14 => self.scissor_x,
            15 => self.scissor_y,
            16 => self.z_base,
            17 => self.dither_offset,
            19 => self.fg,
            20 => self.bg,
            21 => self.fog_color,
            22 => self.dst_color,
            23 => self.chroma_color,
            24 => self.yuv_contrast,
            26 => self.chroma_mask,
            28 => (self.pick_valid as u32) << 16 | (self.pick_hit as u32) << 17,
            29 => self.alpha_thres,
            30 => self.spec_color,
            32..=39 => self.tex4pal[index as usize - 32],
            112 => self.x as u32,
            113 => self.y as u32,
            114 => (self.z_ovf as u32) << 27 | self.z as u32 & 0x07FF_FFFF,
            117 => (sat(self.f) as u32) << 16,
            119 => self.u as u32 & 0x07FF_FF00,
            121 => self.v as u32 & 0x07FF_FF00,
            123 => (self.u_dir as u32) << 31,
            125 => (self.v_dir as u32) << 31,
            131 => self.drawctl,
            132 => self.x_shadow as u32 & 0xFFFF_0000 | self.x_dir as u32,
            133 => self.cnt << 16,
            _ => 0,
        }
    }

    /// Whether pixel x, y is inside the scissors and the 2048 square.
    fn inside(&self, x: i32, y: i32) -> bool {
        let (left, right) = (
            (self.scissor_x >> 16 & 0xFFF) as i32,
            (self.scissor_x & 0xFFF) as i32,
        );
        let (top, bottom) = (
            (self.scissor_y >> 16 & 0xFFF) as i32,
            (self.scissor_y & 0xFFF) as i32,
        );
        (left..right).contains(&x)
            && (top..bottom).contains(&y)
            && (0..2048).contains(&x)
            && (0..2048).contains(&y)
    }

    /// The pattern's bit for the next pixel, None while patterns are off;
    /// PatOffset moves on whether the pixel is drawn or not.
    fn pattern_bit(&mut self) -> Option<bool> {
        if self.pat_mode & 0x1_0000 == 0 {
            return None;
        }
        let bit = self.pattern >> (31 - (self.pat_offset & 31)) & 1 != 0;
        self.pat_offset = if self.pat_offset & 31 != self.pat_mode & 31 {
            (self.pat_offset + 1) & 31
        } else {
            0
        };
        Some(bit)
    }

    fn pattern_opaque(&self) -> bool {
        self.pat_mode & 0x2_0000 != 0
    }

    fn dst_bytes(&self) -> u32 {
        fmt_bytes(self.dst_fmt & 0xF).max(1)
    }

    fn dst_at(&self, x: i32, y: i32) -> u32 {
        let row = line(self.stride >> 8).wrapping_mul(y as u32);
        self.dst_base
            .wrapping_add(row)
            .wrapping_add((x as u32).wrapping_mul(self.dst_bytes()))
    }

    fn src_fmt(&self) -> u32 {
        match self.src_mode & 0xF {
            0 => self.dst_fmt & 0xF,
            fmt => fmt,
        }
    }

    /// Whether a raw texel or source pixel is the chroma key.
    fn keyed(&self, raw: u32, bytes: u32) -> bool {
        self.src_mode & src::CHROMA != 0
            && replicated(raw, bytes) & self.chroma_mask == self.chroma_color & self.chroma_mask
    }

    /// The texel at whole texel coordinates, wrapped by the masks or
    /// clamped to them.
    fn texel(&self, tx: i32, ty: i32, mem: &[u8]) -> [i32; 4] {
        let (um, vm) = (self.u_mask as i32, self.v_mask as i32);
        let tx = if self.src_mode & src::U_CLAMP != 0 {
            tx.clamp(0, um)
        } else {
            tx & um
        } as u32;
        let ty = if self.src_mode & src::V_CLAMP != 0 {
            ty.clamp(0, vm)
        } else {
            ty & vm
        } as u32;
        let fmt = self.src_fmt();
        let row = self
            .src_base
            .wrapping_add(line(self.stride).wrapping_mul(ty));
        let bgr = self.src_mode & src::BGR != 0;
        let no_pad = self.src_mode & src::NO_PAD != 0;
        match fmt {
            // 4-bit indexes, the first texel in a byte's high nibble, into
            // Tex4Pal's 16-bit colours, even indexes high.
            8..=0xA => {
                let byte = mem::read8(mem, row.wrapping_add(tx / 2));
                let index = if tx & 1 == 0 { byte >> 4 } else { byte & 15 };
                let entry = self.tex4pal[index as usize / 2] >> if index & 1 == 0 { 16 } else { 0 }
                    & 0xFFFF;
                decode(fmt - 4, entry, bgr, no_pad)
            }
            // Y0 Cr Y1 Cb: two pixels a word, red = Cr, green = Y, blue = Cb.
            0xD => {
                let w = mem::read32(mem, row.wrapping_add(tx / 2 * 4));
                if self.keyed(w, 4) {
                    return self.key_colour();
                }
                let y = if tx & 1 == 0 { w >> 24 } else { w >> 8 & 0xFF };
                let (cr, cb) = (w >> 16 & 0xFF, w & 0xFF);
                let (cr, cb) = if bgr { (cb, cr) } else { (cr, cb) };
                [cr as i32, y as i32, cb as i32, 255]
            }
            _ => {
                let n = fmt_bytes(fmt);
                let raw = read(mem, row.wrapping_add(tx.wrapping_mul(n)), n);
                if self.keyed(raw, n) {
                    return self.key_colour();
                }
                decode(fmt, raw, bgr, no_pad)
            }
        }
    }

    /// What a keyed texel becomes: transparent black, or YUV's black.
    fn key_colour(&self) -> [i32; 4] {
        if self.src_mode & src::CHROMA_BLACK_YUV != 0 {
            [128, 16, 128, 0]
        } else {
            [0; 4]
        }
    }

    /// The texture at u, v (16.16), point sampled or filtered from the
    /// four texels around it by U's and V's fractions.
    fn sample(&self, u: i32, v: i32, mem: &[u8]) -> [i32; 4] {
        let (tx, ty) = (u >> 16, v >> 16);
        if self.src_mode & src::FILTER == 0 {
            return self.texel(tx, ty, mem);
        }
        let (fu, fv) = (u >> 8 & 0xFF, v >> 8 & 0xFF);
        let taps = [
            (0, 0, (256 - fu) * (256 - fv)),
            (1, 0, fu * (256 - fv)),
            (0, 1, (256 - fu) * fv),
            (1, 1, fu * fv),
        ];
        let mut sum = [0i32; 4];
        for (dx, dy, weight) in taps {
            let c = self.texel(tx + dx, ty + dy, mem);
            for k in 0..4 {
                sum[k] += c[k] * weight;
            }
        }
        sum.map(|s| (s + 32768) >> 16)
    }

    /// A blend factor (table 4.4) for channel `k`, from the source `s`
    /// and the destination `d`; `source` for BlendSrcFunc's numbering.
    fn factor(func: u32, source: bool, k: usize, s: &[i32; 4], d: &[i32; 4]) -> i32 {
        let saturate = if k == 3 { 255 } else { s[3].min(255 - d[3]) };
        match (func, source) {
            (0, true) | (10, false) => d[k],
            (1, true) | (11, false) => 255 - d[k],
            (0, false) | (10, true) => s[k],
            (1, false) | (11, true) => 255 - s[k],
            (2, _) => s[3],
            (3, _) => 255 - s[3],
            (4, _) => d[3],
            (5, _) => 255 - d[3],
            (6, _) => 0,
            (8, _) => saturate,
            (9, _) => 255 - saturate,
            _ => 255,
        }
    }

    fn blend(&self, s: [i32; 4], d: [i32; 4]) -> [i32; 4] {
        let (sf, df) = (self.dst_mode >> 4 & 0xF, self.dst_mode >> 8 & 0xF);
        std::array::from_fn(|k| {
            let term = |func: u32, source: bool, v: i32| {
                if func == 15 {
                    v - 128
                } else {
                    (v * Self::factor(func, source, k, &s, &d) + 127) / 255
                }
            };
            (term(sf, true, s[k]) + term(df, false, d[k])).clamp(0, 255)
        })
    }

    /// YCrCb (red = Cr, green = Y, blue = Cb) to RGB ([SPEC] 4.15).
    fn yuv_to_rgb(&self, c: [i32; 4]) -> [i32; 4] {
        let (cr, y, cb) = (c[0] - 128, c[1], c[2] - 128);
        let black = (self.fog_color >> 8 & 0xFF) as i32;
        let sy = (self.yuv_contrast as i32 & 0xFF) + 256;
        let sy = sy * (y - black) + 128;
        let f = |v: i32| v.div_euclid(255).clamp(0, 255);
        [
            f(sy + 407 * cr),
            f(sy - 207 * cr - 100 * cb),
            f(sy + 515 * cb),
            c[3],
        ]
    }
}

impl Core {
    /// `spr`: the pixel engine register or register field `index` set to
    /// `value` (table 4); fields keep their bit positions.
    pub(super) fn spr(&mut self, index: u32, value: u32, mem: &[u8]) {
        let pe = &mut self.pe;
        let field = |reg: &mut u32, mask: u32| *reg = *reg & !mask | value & mask;
        match index {
            0 => pe.src_base = value,
            1 => pe.src_mode = value,
            2 => pe.src_mask = value,
            3 => pe.stride = value,
            4 => pe.dst_base = value,
            5 => pe.dst_mode = value,
            6 => pe.dst_fmt = value,
            7 => pe.pmask = value,
            8 => (pe.pattern, pe.pat_offset) = (value, 0),
            10 => {
                let word = (0..4).fold(0, |w, k| w << 8 | mem::read8(mem, value.wrapping_add(k)));
                (pe.pattern, pe.pat_offset) = (word, 0);
            }
            12 => pe.pat_mode = value,
            13 => pe.pat_offset = value & 31,
            14 => pe.scissor_x = value,
            15 => pe.scissor_y = value,
            16 => pe.z_base = value,
            17 => pe.dither_offset = value,
            19 => pe.set_fg(value, true),
            20 => pe.bg = value,
            21 => pe.fog_color = value,
            22 => pe.dst_color = value,
            23 => pe.chroma_color = value,
            24 => pe.yuv_contrast = value,
            26 => pe.chroma_mask = value,
            27 => pe.set_fg(value, false),
            28 => (pe.pick_valid, pe.pick_hit) = (value & 0x1_0000 != 0, value & 0x2_0000 != 0),
            29 => pe.alpha_thres = value,
            30 => pe.spec_color = value,
            31 => {}
            32..=39 => pe.tex4pal[index as usize - 32] = value,
            48 => field(&mut pe.src_mode, 0xF),
            49 => field(&mut pe.src_mode, 0x70),
            50..=55 => field(
                &mut pe.src_mode,
                1 << [7, 9, 10, 11, 12, 13][index as usize - 50],
            ),
            61..=63 => field(&mut pe.src_mode, 1 << (index - 61 + 14)),
            83 => field(&mut pe.src_mode, 1 << 17),
            56 => field(&mut pe.src_mask, 0x7FF),
            57 => field(&mut pe.src_mask, 0x7FF_0000),
            58..=60 => field(&mut pe.stride, 0x7F << (8 * (index - 58))),
            64 => field(&mut pe.dst_mode, 0xF),
            65 => field(&mut pe.dst_mode, 0xF0),
            66 => field(&mut pe.dst_mode, 0xF00),
            67 => field(&mut pe.dst_mode, 0x7000),
            68 => field(&mut pe.dst_mode, 1 << 16),
            69..=76 => field(&mut pe.dst_mode, 1 << (index - 69 + 18)),
            80 => field(&mut pe.pat_mode, 0x1F),
            81 => field(&mut pe.pat_mode, 1 << 16),
            82 => field(&mut pe.pat_mode, 1 << 17),
            _ => {
                let line = format!(
                    "[VERITE] spr {} = {:08X}: no such pixel engine register",
                    index, value
                );
                self.note(line);
            }
        }
    }

    /// The parallel draws (table 2.8): from XShadow in XDir along line Y,
    /// with the source walked a pixel per pixel from U, V. drawp draws to
    /// the end of the destination's word, rdrawp all of Cnt; the resetting
    /// ones take Y from S2 and leave Cnt and XShadow as they found them.
    pub(super) fn draw_parallel(&mut self, op: u8, d: u8, s2: u8, s1: u8, mem: &mut [u8]) {
        let (e2, o2) = (self.draw_reg(s2 & !1, 0), self.draw_reg(s2 & !1, 1));
        let (e1, o1) = (self.draw_reg(s1 & !1, 0), self.draw_reg(s1 & !1, 1));
        let resetting = op & 4 != 0;
        let repeated = op & 1 != 0;
        if resetting {
            self.pe.y = o2 as i32;
        } else {
            self.pe.set(Param::U, o2);
        }
        let cnt = self.pe.cnt & 0xFFFF;
        let (x0, cnt0) = (self.pe.x_shadow, cnt);
        let step = if self.pe.x_dir { -0x1_0000 } else { 0x1_0000 };
        let n = if repeated || resetting {
            cnt
        } else {
            // To the end of the destination's word.
            let at = self.pe.dst_at(self.pe.x_shadow >> 16, self.pe.y >> 16);
            let bytes = self.pe.dst_bytes();
            let left = if self.pe.x_dir {
                (at & 3) / bytes + 1
            } else {
                (4 - (at & 3)) / bytes
            };
            left.min(cnt)
        };
        for _ in 0..n {
            let (x, y) = (self.pe.x_shadow >> 16, self.pe.y >> 16);
            let (sx, sy) = (self.pe.u >> 16, self.pe.v >> 16);
            self.pixel_parallel(x, y, sx, sy, mem);
            self.pe.x_shadow = self.pe.x_shadow.wrapping_add(step);
            self.pe.u = self.pe.u.wrapping_add(step);
        }
        self.pe.cnt = cnt - n;
        if resetting {
            (self.pe.x_shadow, self.pe.cnt) = (x0, cnt0);
        }
        if resetting || !repeated && cnt != 0 {
            self.set_draw_reg(d & !1, 0, e2.wrapping_add(e1));
            self.set_draw_reg(d & !1, 1, o2.wrapping_add(o1));
        }
    }

    /// A parallel draw's pixel: the foreground (or the pattern's
    /// background) or the source pixel, combined with the destination by
    /// the ALU mode under the plane mask.
    fn pixel_parallel(&mut self, x: i32, y: i32, sx: i32, sy: i32, mem: &mut [u8]) {
        let background = match self.pe.pattern_bit() {
            Some(false) if !self.pe.pattern_opaque() => return,
            Some(bit) => !bit,
            Option::None => false,
        };
        let pe = &self.pe;
        if !pe.inside(x, y) {
            return;
        }
        let bytes = pe.dst_bytes();
        let at = pe.dst_at(x, y);
        let mask = if bytes == 4 {
            u32::MAX
        } else {
            (1 << (8 * bytes)) - 1
        };
        let source = if pe.src_mode >> 4 & 7 == 0 || background {
            // The colour word holds a pixel for each place in a word, the
            // first in its high bits.
            let word = if background { pe.bg } else { pe.fg };
            let shift = 8 * (4 - bytes - (at & 3 & !(bytes - 1)));
            word >> shift & mask
        } else {
            let fmt = pe.src_fmt();
            let n = fmt_bytes(fmt).max(1);
            let (sx, sy) = (
                (sx & pe.u_mask as i32) as u32,
                (sy & pe.v_mask as i32) as u32,
            );
            let row = pe.src_base.wrapping_add(line(pe.stride).wrapping_mul(sy));
            let raw = read(mem, row.wrapping_add(sx.wrapping_mul(n)), n);
            if pe.keyed(raw, n) {
                return;
            }
            raw
        };
        let code = pe.dst_mode & 0xF;
        let pmask = pe.pmask & mask;
        let old = if rop_reads(code) || pmask != mask {
            read(mem, at, bytes)
        } else {
            0
        };
        let new = rop(code, source, old) & pmask | old & !pmask;
        write(mem, at, bytes, new & mask);
        self.pe.pick_hit = true;
        self.drew = true;
    }

    /// The parameterized draws (table 2.9a): the parameters DrawCtl names
    /// for each pair, from the register groups at S2 (values) and S1
    /// (deltas). draw1-5 draw one pixel and add the deltas into D; rdraw1-5
    /// draw Cnt pixels, iterating, and leave Cnt at 0.
    pub(super) fn draw_param(&mut self, op: u8, d: u8, s2: u8, s1: u8, mem: &mut [u8]) {
        let ctl = self.pe.drawctl;
        let pairs = match op & 0xFE {
            0xF0 => 1,
            0xF2 => 2,
            0xF8 => 3,
            0xFA => 4,
            _ => 5,
        };
        let (p1e, p1o, shadow) = PAIR1[(ctl & 15) as usize];
        let p2 = match ctl >> 4 & 31 {
            v @ 0..=7 => PAIR3[v as usize],
            v => PAIR2[v as usize - 8],
        };
        let p3 = PAIR3[(ctl >> 9 & 15) as usize];
        let p4 = PAIR3[(ctl >> 13 & 7) as usize];
        let params = [p1e, p1o, p2.0, p2.1, p3.0, p3.1, p4.0, p4.1, G, B];
        let count: u8 = pairs * 2;
        let params = &params[..count as usize];
        let align = count.next_power_of_two().max(2);
        let (d, s2, s1) = (d & !(align - 1), s2 & !(align - 1), s1 & !(align - 1));
        // All the values before the deltas: a group at the FIFO pops it.
        let mut values = [0u32; 10];
        let mut deltas = [0u32; 10];
        for k in 0..count {
            values[k as usize] = self.draw_reg(s2, k);
        }
        for k in 0..count {
            deltas[k as usize] = self.draw_reg(s1, k);
        }
        let values = &mut values[..count as usize];
        let deltas = &deltas[..count as usize];
        let cnt = self.pe.cnt as u16 as i16;
        if cnt <= 0 {
            return;
        }
        let perspective = params.contains(&Q);
        let explicit_x = params.contains(&X);
        let step = if self.pe.x_dir { -0x1_0000 } else { 0x1_0000 };
        let n = if op & 1 != 0 { cnt } else { 1 };
        for i in 0..n {
            let mut next_q = self.pe.q;
            for (k, &param) in params.iter().enumerate() {
                match param {
                    Q => next_q = values[k],
                    p => self.pe.set(p, values[k]),
                }
            }
            let x = if !explicit_x && shadow {
                self.pe.x_shadow >> 16
            } else {
                self.pe.x >> 16
            };
            let y = self.pe.y >> 16;
            self.pixel_full(x, y, perspective, mem);
            self.pe.q = next_q;
            if !explicit_x && shadow {
                self.pe.x_shadow = self.pe.x_shadow.wrapping_add(step);
            }
            if op & 1 != 0 && i + 1 < n {
                for (value, delta) in values.iter_mut().zip(deltas) {
                    *value = value.wrapping_add(*delta);
                }
            }
        }
        if op & 1 != 0 {
            self.pe.cnt = 0;
        } else {
            self.pe.cnt = (cnt - 1) as u16 as u32;
            for k in 0..count {
                self.set_draw_reg(d, k, values[k as usize].wrapping_add(deltas[k as usize]));
            }
        }
    }

    /// A parameterized draw's pixel: the colour or the texture through the
    /// source function, the alpha test, fog, the Z test, blending or YUV
    /// conversion, dithering, and the ALU mode under the plane mask.
    fn pixel_full(&mut self, x: i32, y: i32, perspective: bool, mem: &mut [u8]) {
        let background = match self.pe.pattern_bit() {
            Some(false) if !self.pe.pattern_opaque() => return,
            Some(bit) => !bit,
            Option::None => false,
        };
        let pe = &self.pe;
        if !pe.inside(x, y) || pe.z_ovf && pe.src_mode & src::Z_SCISSOR != 0 {
            return;
        }
        let mut c = if background {
            argb(pe.bg)
        } else {
            [sat(pe.r), sat(pe.g), sat(pe.b), sat(pe.a)]
        };
        let func = pe.src_mode >> 4 & 7;
        if func != 0 {
            let (u, v) = if perspective && pe.q != 0 {
                let q = pe.q as i64;
                (((pe.u as i64) << 24) / q, ((pe.v as i64) << 24) / q)
            } else {
                (pe.u as i64, pe.v as i64)
            };
            let t = pe.sample(u as i32, v as i32, mem);
            c = match func {
                2 => {
                    let mix = |k: usize| (c[k] * (255 - t[3]) + t[k] * t[3] + 127) / 255;
                    [mix(0), mix(1), mix(2), c[3]]
                }
                3 => std::array::from_fn(|k| (c[k] * t[k] + 127) / 255),
                _ => t,
            };
        }
        if pe.dst_mode & dst::TRANSP_REJECT != 0 && c[3] <= (pe.alpha_thres & 0xFF) as i32 {
            return;
        }
        if pe.dst_mode & dst::FOG != 0 {
            let (f, fog) = (sat(pe.f), argb(pe.fog_color));
            for k in 0..3 {
                c[k] = (f * (c[k] - fog[k])) / 255 + fog[k];
            }
        }
        let z_mode = pe.dst_mode >> 12 & 7;
        let z_at = pe
            .z_base
            .wrapping_add(line(pe.stride >> 16).wrapping_mul(y as u32))
            .wrapping_add((x as u32).wrapping_mul(2));
        let z = (pe.z >> 11) as u32 & 0xFFFF;
        let pass = match z_mode {
            0 => true,
            7 => false,
            mode => {
                let old = mem::read16(mem, z_at);
                match mode {
                    1 => z < old,
                    2 => z == old,
                    3 => z <= old,
                    4 => z > old,
                    5 => z != old,
                    _ => z >= old,
                }
            }
        };
        if !pass {
            return;
        }
        let fmt = pe.dst_fmt & 0xF;
        let bytes = pe.dst_bytes();
        let at = pe.dst_at(x, y);
        let mask = if bytes == 4 {
            u32::MAX
        } else {
            (1 << (8 * bytes)) - 1
        };
        let code = pe.dst_mode & 0xF;
        let pmask = pe.pmask & mask;
        let blending = pe.dst_mode & dst::BLEND != 0;
        let read_disabled = pe.dst_mode & dst::READ_DISABLE != 0;
        let bgr = pe.dst_mode & dst::BGR != 0;
        let old = if (blending || rop_reads(code) || pmask != mask) && !read_disabled {
            read(mem, at, bytes)
        } else {
            0
        };
        if blending {
            let d = if read_disabled {
                argb(pe.dst_color)
            } else {
                decode(fmt, old, bgr, pe.dst_mode & dst::NO_PAD != 0)
            };
            c = pe.blend(c, d);
        } else if pe.dst_mode & dst::YUV2RGB != 0 {
            c = pe.yuv_to_rgb(c);
        }
        let dither = if pe.dst_mode & dst::DITHER != 0 {
            let (ox, oy) = (pe.dither_offset & 3, pe.dither_offset >> 4 & 3);
            DITHER[((y as u32).wrapping_add(oy) & 3) as usize]
                [((x as u32).wrapping_add(ox) & 3) as usize]
        } else {
            0
        };
        let new = encode(fmt, c, dither, bgr);
        let out = rop(code, new, old) & pmask | old & !pmask;
        let z_write = pe.dst_mode & dst::ZBUF_WRITE != 0;
        write(mem, at, bytes, out & mask);
        if z_write {
            mem::write16(mem, z_at, z);
        }
        self.pe.pick_hit = true;
        self.drew = true;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rops_are_truth_tables() {
        assert_eq!(rop(0xC, 0xF0, 0xAA), 0xF0);
        assert_eq!(rop(0x6, 0xF0, 0xAA), 0x5A);
        assert_eq!(rop(0x8, 0xF0, 0xAA), 0xA0);
        assert_eq!(rop(0xA, 0xF0, 0xAA), 0xAA);
        assert!(!rop_reads(0xC) && !rop_reads(0x3) && rop_reads(0x6) && rop_reads(0xA));
    }

    #[test]
    fn pixels_round_trip_through_their_formats() {
        let white = [255, 255, 255, 255];
        assert_eq!(encode(4, white, 0, false), 0xFFFF);
        assert_eq!(decode(4, 0xFFFF, false, false), white);
        assert_eq!(decode(4, 0xFFFF, false, true), [248, 252, 248, 255]);
        assert_eq!(encode(6, [255, 0, 0, 255], 0, false), 0xFC00);
        assert_eq!(encode(0xC, [1, 2, 3, 4], 0, true), 0x0403_0201);
        assert_eq!(decode(1, 0b1110_0011, false, false), [255, 0, 255, 255]);
    }

    #[test]
    fn dither_adds_its_share_before_the_bits_go() {
        // Color5 = min(Color8 + (d >> 1), 255) >> 3.
        assert_eq!(encode(4, [6, 0, 0, 255], 15, false) >> 11, 1);
        assert_eq!(encode(4, [6, 0, 0, 255], 0, false) >> 11, 0);
    }
}
