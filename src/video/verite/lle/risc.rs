// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from rust-dos (src/verite/lle).

//! The RISC: fetch, decode and execute ([SPEC] 2), its special registers
//! (3) and its view of memory (5, table 5).
//!
//! Each instruction runs whole before the next: loads and multiplies are
//! interlocked on the chip, so their delays don't show. The delay slot of
//! branches and jumps does, as `pc` and `npc` keep it. Where [SPEC] leaves
//! something open, the choice is what the V1000 microcode in use needs:
//! links are to the instruction after the delay slot, and the seed is
//! floor(2^62 / m) of the normalised operand m, which line setup refines
//! with one Newton step.

use super::{Board, Core, intr, mem};

/// The constant registers %0-%31 (table 2.2b).
const CONSTANTS: [u32; 32] = [
    0,
    0,
    0x8000,
    0x8000_0000,
    0xFF00_0000,
    0x00FF_0000,
    0x0000_FF00,
    0x0000_00FF,
    0xFFFF_0000,
    0x0000_FFFF,
    0xFF00_FF00,
    0x00FF_00FF,
    0,
    0xFFFF_FFFF,
    0,
    0x0001_0000,
    0xFFFF_FFFF,
    0x0001_0000,
    0x007F_FFFF,
    0x0200_0000,
    0x0080_0000,
    0x0002_0000,
    0xB000,
    0xB800,
    0xC000,
    0xC800,
    0xD000,
    0xD800,
    0xE000,
    0xE800,
    0xF000,
    0xF800,
];

/// The special registers' numbers.
const SWFIFO: usize = 32;
const FIFO: usize = 33;
const SEED: usize = 34;
const SEG: usize = 35;
const SCALE: usize = 36;
const FLAG: usize = 37;
const CLIP: usize = 38;
const TIMER: usize = 39;
const NPSWFIFO: usize = 40;
const DRAWCTL: usize = 40;
const NPFIFO: usize = 41;
const WINCLIP: usize = 46;
const RISCINTR: usize = 47;
const RA: u8 = 254;

/// Where the control registers are for the RISC: the board's (I/O
/// 80h-FFh, 8 bytes apart) and the pixel engine's state (table 10).
const CONTROL: u32 = 0xFFE0_0000;
const BOARD_REGS: u32 = 0xFFE0_0400;
const PE_STATE: u32 = 0xFFE0_0800;
const CONTROL_END: u32 = 0xFFE0_1000;

/// The address after `a`: the RISC doesn't carry from local memory into
/// the ROM's area, or from the top of the address space to 0 ([SPEC] 2).
pub fn next(a: u32) -> u32 {
    a & 0xFF00_0000 | a.wrapping_add(4) & 0x00FF_FFFF
}

/// `a` moved by `words` words, as a relative branch moves.
fn relative(a: u32, words: i32) -> u32 {
    a & 0xFF00_0000 | a.wrapping_add((words as u32).wrapping_mul(4)) & 0x00FF_FFFF
}

/// An absolute jump's target: the 24-bit word address, in the ROM when
/// its top bit is set.
fn absolute(field: u32) -> u32 {
    let a = (field & 0xFF_FFFF) << 2;
    if field & 0x80_0000 != 0 {
        a | 0xFC00_0000
    } else {
        a
    }
}

/// A float scaled by 2^`shift` as an integer, rounded towards minus
/// infinity and saturated (`f2i`).
fn f2i(bits: u32, shift: u32) -> u32 {
    let v = f32::from_bits(bits) as f64 * (1u64 << (shift & 31)) as f64;
    if v.is_nan() {
        return if bits & 0x8000_0000 != 0 {
            0x8000_0000
        } else {
            0x7FFF_FFFF
        };
    }
    v.floor().clamp(i32::MIN as f64, i32::MAX as f64) as i32 as u32
}

/// An ALU operation of table 2.3a, by its low nibble.
fn alu(op: u8, s2: u32, s1: u32) -> u32 {
    let product = |shift: u32| ((s2 as i32 as i64 * s1 as i32 as i64) >> shift) as u32;
    match op & 0xF {
        0x0 => s2.wrapping_add(s1),
        0x1 => s2.wrapping_sub(s1),
        0x2 => s2 & !s1,
        0x3 => s1.wrapping_sub(s2),
        0x4 => s2 & s1,
        0x5 => s2 | s1,
        0x6 => !(s2 | s1),
        0x7 => s2 ^ s1,
        0x8 => product(0),
        0x9 => product(8),
        0xA => product(16),
        0xB => product(24),
        0xC => product(32),
        0xD => product(31),
        _ => f2i(s2, s1),
    }
}

/// Whether `s2` and `s1` give a clip flag: S2 - S1 > 0, signed.
fn clip_flag(s2: u32, s1: u32) -> u32 {
    if (s2 as i32 as i64 - s1 as i32 as i64) > 0 {
        4
    } else {
        0
    }
}

impl Core {
    // --- Registers ---

    /// The register an operand field names: %64-%127 reach %128-%255
    /// through the Seg register's four bases ([SPEC] 3.3).
    #[inline]
    fn resolve(&self, field: u8) -> usize {
        if field & 0xC0 == 0x40 {
            let base = self.seg >> ((field >> 4 & 3) * 4) & 7;
            128 | (base as usize) << 4 | (field & 15) as usize
        } else {
            field as usize
        }
    }

    /// A source register's value; reading the FIFO pops it.
    #[inline]
    fn src(&mut self, field: u8) -> u32 {
        let r = self.resolve(field);
        if r >= 64 {
            self.reg[r]
        } else {
            self.special_read(r, true)
        }
    }

    /// A register's value without side effects, for StateData.
    pub(super) fn peek(&self, field: u8) -> u32 {
        let r = self.resolve(field);
        if r >= 64 {
            return self.reg[r];
        }
        match r {
            SWFIFO | NPSWFIFO => self.in_fifo.front().map_or(0, |w| w.swap_bytes()),
            FIFO | NPFIFO => self.in_fifo.front().copied().unwrap_or(0),
            _ => self.special_value(r),
        }
    }

    /// A vector source pair: the even and odd registers from `field`'s
    /// even one. The FIFO as a pair pops one word, swapped in the even
    /// half and plain in the odd ([SPEC] 3.1).
    fn src_pair(&mut self, field: u8) -> (u32, u32) {
        let r = self.resolve(field & !1);
        match r {
            SWFIFO => {
                let w = self.pop_input().unwrap_or(0);
                (w.swap_bytes(), w)
            }
            NPSWFIFO => {
                let w = self.in_fifo.front().copied().unwrap_or(0);
                (w.swap_bytes(), w)
            }
            _ => (self.src(field & !1), self.src(field | 1)),
        }
    }

    #[inline]
    fn dst(&mut self, field: u8, value: u32) {
        let r = self.resolve(field);
        if r >= 64 {
            self.reg[r] = value
        } else {
            self.special_write(r, value)
        }
    }

    fn dst_pair(&mut self, field: u8, (even, odd): (u32, u32)) {
        self.dst(field & !1, even);
        self.dst(field | 1, odd);
    }

    fn special_value(&self, r: usize) -> u32 {
        match r {
            0..=31 => CONSTANTS[r],
            SEED => self.seed,
            // Bits 3, 7, 11 and 15 read as 1; the seed ROM's index on top.
            SEG => self.seed_index << 19 | self.seg & 0xFFFF | 0x8888,
            SCALE => self.scale,
            FLAG => self.flag_value(),
            CLIP => self.clip & 0xFC,
            TIMER => self.timer,
            WINCLIP => self.clip & 0x3C,
            RISCINTR => {
                let pending = self.riscintr & self.riscintr >> 16 & 0x3F != 0;
                self.riscintr | (pending as u32) << 31
            }
            56 => 0xFFFF_F000,
            57 => 0xFFFF_F800,
            _ => 0,
        }
    }

    fn special_read(&mut self, r: usize, pop: bool) -> u32 {
        match r {
            SWFIFO if pop => self.pop_input().unwrap_or(0).swap_bytes(),
            FIFO if pop => self.pop_input().unwrap_or(0),
            SWFIFO | NPSWFIFO => self.in_fifo.front().map_or(0, |w| w.swap_bytes()),
            FIFO | NPFIFO => self.in_fifo.front().copied().unwrap_or(0),
            _ => self.special_value(r),
        }
    }

    fn special_write(&mut self, r: usize, value: u32) {
        match r {
            FIFO => {
                self.out_fifo.push_back(value);
            }
            SEG => self.seg = value & 0xFFFF,
            FLAG => {
                // SignalSys sets and holds the host's RISC interrupt; the
                // status nibble shows in Comm.
                if value & 1 != 0 {
                    self.intr |= intr::RISC;
                }
                self.flag = self.flag & 1 | value & (1 | 0x8 | 0xF0 | 0x4000_0000);
            }
            TIMER => self.timer = value,
            DRAWCTL => self.pe.drawctl = value & 0xFFFF,
            RISCINTR => {
                // Interrupts written as 1 are reset; the enables are kept.
                self.riscintr = self.riscintr & !(value & 0x3F) & 0xFFFF | value & 0x3F_0000;
            }
            _ => {}
        }
    }

    /// The Flag register ([SPEC] 3.2): what was written, the FIFOs' and
    /// the pixel engine's state, the host's status nibble and pick.
    fn flag_value(&self) -> u32 {
        let out_ready = self.out_fifo.len() < super::OUT_FIFO;
        let sys = (self.byte(super::io::COMM) & 0x0F) as u32;
        self.flag
            | (out_ready as u32) << 2
            | sys << 8
            | (self.pe.pick_valid as u32) << 16
            | (self.pe.pick_hit as u32) << 17
            | (!self.in_fifo.is_empty() as u32) << 31
    }

    /// The seed ROM ([SPEC] 3.5) on `x`: Scale is the place of its top
    /// bit, plus 1; Seed the reciprocal of `x` normalised to bit 31.
    fn seed_op(&mut self, x: u32) {
        self.scale = 32 - x.leading_zeros();
        if x == 0 {
            self.seed = u32::MAX;
            self.seed_index = 0;
            return;
        }
        let m = x << x.leading_zeros();
        self.seed_index = m >> 19;
        self.seed = ((1u64 << 62) / m as u64).min(u32::MAX as u64) as u32;
    }

    // --- Memory ---

    /// The instruction word at `a`.
    pub(super) fn fetch(&self, a: u32, mem: &[u8]) -> u32 {
        if a < 0x0100_0000 {
            mem::read32(mem, a)
        } else {
            self.rom_word(a)
        }
    }

    fn rom_word(&self, a: u32) -> u32 {
        if self.rom.is_empty() {
            return u32::MAX;
        }
        let o = a as usize & (self.rom.len() - 1) & !3;
        u32::from_be_bytes([
            self.rom[o],
            self.rom[o + 1],
            self.rom[o + 2],
            self.rom[o + 3],
        ])
    }

    /// Whether `a` is a control register's address rather than the ROM's.
    fn control(a: u32) -> bool {
        (CONTROL..CONTROL_END).contains(&a)
    }

    pub(super) fn load32(&mut self, a: u32, mem: &[u8], board: &mut dyn Board) -> u32 {
        if a < 0x0100_0000 {
            mem::read32(mem, a)
        } else if Self::control(a) {
            self.control_read(a & !3, 4, board)
        } else if a >= 0xFF00_0000 {
            self.rom_word(a)
        } else {
            0
        }
    }

    fn load16(&mut self, a: u32, mem: &[u8], board: &mut dyn Board) -> u32 {
        if a < 0x0100_0000 {
            mem::read16(mem, a)
        } else {
            self.load32(a & !3, mem, board) >> (16 - (a & 2) * 8) & 0xFFFF
        }
    }

    fn load8(&mut self, a: u32, mem: &[u8], board: &mut dyn Board) -> u32 {
        if a < 0x0100_0000 {
            mem::read8(mem, a)
        } else if Self::control(a) && (BOARD_REGS..PE_STATE).contains(&(a & !7)) {
            // The RAMDAC's registers are bytes.
            self.control_read(a & !7, 1, board) & 0xFF
        } else {
            self.load32(a & !3, mem, board) >> (24 - (a & 3) * 8) & 0xFF
        }
    }

    fn store(&mut self, a: u32, value: u32, size: u32, mem: &mut [u8], board: &mut dyn Board) {
        if a < 0x0100_0000 {
            match size {
                1 => mem::write8(mem, a, value),
                2 => mem::write16(mem, a, value),
                _ => mem::write32(mem, a, value),
            }
            self.drew = true;
        } else if Self::control(a) {
            self.control_write(a, value, size, board);
        }
    }

    /// A control register read: the board's registers (bytes where they
    /// are the RAMDAC's) and the pixel engine's state.
    fn control_read(&mut self, a: u32, len: u8, board: &mut dyn Board) -> u32 {
        let value = self.control_value(a, len, board);
        if self.ctl_left > 0 {
            self.ctl_left -= 1;
            self.note(format!(
                "[VERITE] RISC at {:08X} reads {:08X}: {:08X}",
                self.pc, a, value
            ));
        }
        value
    }

    fn control_value(&mut self, a: u32, len: u8, board: &mut dyn Board) -> u32 {
        if (BOARD_REGS..PE_STATE).contains(&a) {
            let io = 0x80 + ((a - BOARD_REGS) >> 3) as u8;
            let len = if (0xB0..0xC0).contains(&io) { 1 } else { len };
            self.polls += 1;
            board.ext_read(io, len)
        } else if a >= PE_STATE {
            self.pe.state(((a - PE_STATE) >> 3) as u8)
        } else {
            0
        }
    }

    fn control_write(&mut self, a: u32, value: u32, size: u32, board: &mut dyn Board) {
        if self.ctl_left > 0 {
            self.ctl_left -= 1;
            self.note(format!(
                "[VERITE] RISC at {:08X} writes {:08X}: {:08X}",
                self.pc, a, value
            ));
        }
        if a & !7 == super::loader::LOADER_PORT {
            self.loader_begin();
            return;
        }
        if !(BOARD_REGS..PE_STATE).contains(&a) {
            return;
        }
        let io = 0x80 + ((a - BOARD_REGS) >> 3) as u8;
        match io {
            // The microcode's stores to the CRTC's timing and control have
            // no effect on the V1000: Creative's CGL microcode starts with
            // them, and the display stays as the host set it up.
            0x80..=0x8F => {}
            // A word to the palette's data is a whole RGB triple ([SPEC] 7).
            0xB1 if size == 4 => {
                for shift in [16, 8, 0] {
                    board.ext_write(io, value >> shift & 0xFF, 1);
                }
            }
            0xB0..=0xBF => board.ext_write(io, value & 0xFF, 1),
            _ => board.ext_write(io, value, 4),
        }
    }

    // --- Execution ---

    /// How many words the instruction `ir` pops from the input FIFO, and
    /// whether it writes the output FIFO, for the stalls ([SPEC] 3.1).
    fn fifo_use(&self, ir: u32) -> (usize, bool) {
        let (op, d, s2, s1) = (
            (ir >> 24) as u8,
            (ir >> 16) as u8,
            (ir >> 8) as u8,
            ir as u8,
        );
        let fifo = |field: u8| matches!(self.resolve(field), SWFIFO | FIFO);
        let pair = |field: u8| matches!(self.resolve(field & !1), SWFIFO | FIFO);
        let (pops, out) = match op {
            // Immediates: S2 is the source.
            0x00..=0x0F | 0x40..=0x4D => (
                fifo(s2) as usize,
                !matches!(op, 0x0E) && self.resolve(d) == FIFO,
            ),
            0x4F => (fifo(s2) as usize, false),
            0x10..=0x1F | 0x50..=0x5E => {
                let srcs = if op == 0x52 {
                    fifo(s1) as usize
                } else {
                    fifo(s2) as usize + fifo(s1) as usize
                };
                (srcs, !matches!(op, 0x1E) && self.resolve(d) == FIFO)
            }
            0x5F => (fifo(s2) as usize + fifo(s1) as usize, false),
            0x20..=0x2F => (pair(s2) as usize + pair(s1) as usize, false),
            0x30..=0x3F => (pair(s2) as usize + fifo(s1) as usize, false),
            0x60..=0x65 | 0x6F | 0x70..=0x75 => {
                (fifo(s1) as usize, op >= 0x6F && self.resolve(d) == FIFO)
            }
            0x6B | 0x76 | 0x77 => (0, self.resolve(d) == FIFO),
            0x78..=0x7F => (fifo(s1) as usize + fifo(s2) as usize, false),
            0xC0..=0xC3 => (fifo(s1) as usize, false),
            0xD0..=0xD7 => (fifo(s2) as usize + fifo(s1) as usize, false),
            0xD8..=0xDF => (pair(s2) as usize + pair(s1) as usize, false),
            _ => (0, false),
        };
        (pops, out)
    }

    /// Carry out the next instruction: `forced`, the host's, or the one
    /// at PC. False, having done nothing, if it waits for a FIFO.
    pub(super) fn step(
        &mut self,
        forced: Option<u32>,
        mem: &mut [u8],
        board: &mut dyn Board,
    ) -> bool {
        let cur = self.pc;
        let ir = forced.unwrap_or_else(|| self.fetch(cur, mem));
        let (pops, out) = self.fifo_use(ir);
        if pops > self.in_fifo.len()
            || out && self.out_fifo.len() >= super::OUT_FIFO && self.flag & 0x4000_0000 == 0
        {
            self.stalled = true;
            return false;
        }
        self.stalled = false;
        if self.trace_skip > 0 {
            self.trace_skip -= 1;
        } else if self.trace_left > 0 {
            self.trace_left -= 1;
            let pe = if ir >> 28 >= 0xE {
                let (e, o) = (self.peek((ir >> 8) as u8), self.peek((ir >> 8) as u8 | 1));
                let pe = &self.pe;
                format!(
                    " src {:08X} dst {:08X} fmt {:X} mask {:08X} u {:08X} v {:08X} \
                     s2 {:08X} {:08X} cnt {:X} base {:08X} stride {:08X} y {:08X}",
                    pe.src_mode,
                    pe.dst_mode,
                    pe.dst_fmt,
                    pe.src_mask,
                    pe.u,
                    pe.v,
                    e,
                    o,
                    pe.cnt,
                    pe.dst_base,
                    pe.stride,
                    pe.y
                )
            } else {
                String::new()
            };
            self.note(format!("[VERITE] {:08X}: {:08X}{}", cur, ir, pe));
        }
        self.pc = self.npc;
        self.npc = next(self.npc);
        self.timer = self.timer.wrapping_add(1);
        self.execute(ir, cur, mem, board);
        true
    }

    fn execute(&mut self, ir: u32, cur: u32, mem: &mut [u8], board: &mut dyn Board) {
        let (op, d, s2, s1) = (
            (ir >> 24) as u8,
            (ir >> 16) as u8,
            (ir >> 8) as u8,
            ir as u8,
        );
        let link = relative(cur, 2);
        match op {
            // Scalar and immediate ALU operations and clip.
            0x0E | 0x1E => {
                let (b, a) = (
                    if op == 0x0E { s1 as u32 } else { self.src(s1) },
                    self.src(s2),
                );
                self.clip = (self.clip << 1) & 0xFC | clip_flag(a, b);
            }
            0x00..=0x1F => {
                let a = self.src(s2);
                let b = if op < 0x10 { s1 as u32 } else { self.src(s1) };
                let v = alu(op, a, b);
                self.dst(d, v);
            }
            // Vector and vector-scalar.
            0x2E | 0x3E => {
                let (a0, a1) = self.src_pair(s2);
                let (b0, b1) = if op == 0x2E {
                    self.src_pair(s1)
                } else {
                    let b = self.src(s1);
                    (b, b)
                };
                self.clip = (self.clip << 1) & 0xFC | clip_flag(a0, b0);
                self.clip = (self.clip << 1) & 0xFC | clip_flag(a1, b1);
            }
            0x20..=0x2D | 0x30..=0x3D => {
                let (a0, a1) = self.src_pair(s2);
                let (b0, b1) = if op < 0x30 {
                    self.src_pair(s1)
                } else {
                    let b = self.src(s1);
                    (b, b)
                };
                let v = (alu(op, a0, b0), alu(op, a1, b1));
                self.dst_pair(d, v);
            }
            // Table 2.3b, immediate and scalar forms.
            0x40..=0x4D | 0x52..=0x5E => self.alu_b(op, d, s2, s1),
            0x4E => {
                let value = ir >> 8 & 0xFFFF;
                self.spr(s1 as u32, value, mem);
            }
            0x4F => {
                let value = self.src(s2);
                self.spr(s1 as u32, value, mem);
            }
            0x5F => {
                let (value, index) = (self.src(s2), self.src(s1));
                self.spr(index, value, mem);
            }
            // Branches, relative to the delay slot.
            0x60..=0x65 => {
                let v = self.src(s1) as i32;
                let taken = match op {
                    0x60 => v == 0,
                    0x61 => v != 0,
                    0x62 => v >= 0,
                    0x63 => v < 0,
                    0x64 => v > 0,
                    _ => v <= 0,
                };
                if taken {
                    self.npc = relative(self.pc, (ir >> 8) as u16 as i16 as i32);
                }
            }
            0x68 | 0x6A => {
                if op == 0x6A {
                    self.dst(RA, link);
                }
                self.npc = relative(self.pc, (ir << 8) as i32 >> 8);
            }
            0x6B => self.dst(d, link),
            0x6C | 0x6E => {
                if op == 0x6E {
                    self.dst(RA, link);
                }
                self.npc = absolute(ir);
            }
            0x6D => {
                // Halt: the last instruction, without a delay slot.
                self.halted = true;
                self.pc = cur;
                self.npc = next(cur);
                self.intr |= intr::HALT;
            }
            0x6F => {
                let target = self.src(s1);
                self.dst(d, link);
                self.npc = target;
            }
            // Loads and stores; offsets count in the data's size.
            0x70..=0x75 => {
                let base = self.src(s1);
                let off = s2 as u32;
                match op {
                    0x70 => {
                        let v = self.load8(base.wrapping_add(off), mem, board);
                        self.dst(d, v);
                    }
                    0x71 => {
                        let v = self.load16(base.wrapping_add(off * 2), mem, board);
                        self.dst(d, v);
                    }
                    0x72 => {
                        let v = self.load32(base.wrapping_add(off * 4), mem, board);
                        self.dst(d, v);
                    }
                    0x73 => {}
                    _ => {
                        let a = base.wrapping_add(off * 8) & !7;
                        let v = (self.load32(a, mem, board), self.load32(a + 4, mem, board));
                        self.dst_pair(d, v);
                    }
                }
            }
            0x76 => self.dst(d, ir & 0xFFFF),
            0x77 => self.dst(d, ir << 16),
            0x78..=0x7A | 0x7C..=0x7F => {
                let base = self.src(s1);
                let off = d as u32;
                match op {
                    0x78 => {
                        let v = self.src(s2);
                        self.store(base.wrapping_add(off), v, 1, mem, board);
                    }
                    0x79 => {
                        let v = self.src(s2);
                        self.store(base.wrapping_add(off * 2), v, 2, mem, board);
                    }
                    0x7A => {
                        let v = self.src(s2);
                        self.store(base.wrapping_add(off * 4), v, 4, mem, board);
                    }
                    0x7C => {
                        let a = base.wrapping_add(off * 8) & !7;
                        let (even, odd) = self.src_pair(s2);
                        self.store(a, even, 4, mem, board);
                        self.store(a + 4, odd, 4, mem, board);
                    }
                    _ => {
                        // sy, scr, scb: the four bytes of S2, high first,
                        // scattered 2 or 4 bytes apart.
                        let v = self.src(s2);
                        let a = base.wrapping_add(off * 8);
                        let (first, apart) = match op {
                            0x7D => (0, 2),
                            0x7E => (1, 4),
                            _ => (3, 4),
                        };
                        for k in 0..4 {
                            let byte = v >> (24 - 8 * k) & 0xFF;
                            self.store(a.wrapping_add(first + apart * k), byte, 1, mem, board);
                        }
                    }
                }
            }
            0xC0..=0xC3 => {
                let v = self.src(s1);
                let pair = match op {
                    0xC0 => (v & 0xFFFF_0000, v << 16),
                    0xC1 => (v << 16, v & 0xFFFF_0000),
                    0xC2 => (v & 0xFF_0000, v >> 8 & 0xFF_0000),
                    _ => (v << 8 & 0xFF_0000, v << 16 & 0xFF_0000),
                };
                self.dst_pair(d, pair);
            }
            0xC4 => self.sort(d, s2, s1),
            0xD0..=0xDF => self.step_op(op, d, s2, s1),
            0xE0 | 0xE1 | 0xE4 | 0xE5 => self.draw_parallel(op, d, s2, s1, mem),
            0xF0..=0xF3 | 0xF8..=0xFD => self.draw_param(op, d, s2, s1, mem),
            _ => self.undefined(op, ir, cur),
        }
    }

    /// The operations of table 2.3b.
    fn alu_b(&mut self, op: u8, d: u8, s2: u8, s1: u8) {
        let a = self.src(s2);
        let b = if op < 0x50 {
            s1 as u32
        } else if op == 0x52 {
            0
        } else {
            self.src(s1)
        };
        let v = match op {
            0x40 => a.wrapping_add(b << 16),
            0x41 => a.wrapping_sub(b << 16),
            0x42 | 0x53 => {
                self.seed_op(a);
                match self.scale.checked_sub(b) {
                    Some(shift) => self.seed.checked_shr(shift).unwrap_or(0),
                    None => self.seed.checked_shl(b - self.scale).unwrap_or(0),
                }
            }
            0x43 => (b << 16).wrapping_sub(a),
            0x44 | 0x54 => a.rotate_right(b & 31),
            0x45 | 0x55 => a << (b & 31),
            0x46 | 0x56 => ((a as i32) >> (b & 31)) as u32,
            0x47 | 0x57 => a >> (b & 31),
            0x48 | 0x58 => ((a as i32) < (b as i32)) as u32,
            0x49 | 0x59 => (a < b) as u32,
            0x4A | 0x5A => (a == b) as u32,
            0x4B => a.wrapping_add(b << 8),
            0x4C | 0x5C => (a as i32).min(b as i32) as u32,
            0x4D | 0x5D => (a as i32).max(b as i32) as u32,
            // abs takes S1 alone.
            0x52 => (self.src(s1) as i32).unsigned_abs(),
            0x5E => a.wrapping_add(b) & 0xFFFF_0000,
            _ => {
                self.undefined(op, (op as u32) << 24, self.pc);
                return;
            }
        };
        self.dst(d, v);
    }

    /// Sort S3, S2 and S1 ([SPEC] table 2.3b): the smallest's segment base
    /// into Seg0, the middle one's into Seg1, the largest's into Seg2, ties
    /// taking S1 as the smaller, then S2; four clip flags, set when all
    /// three are within 1 of each other.
    fn sort(&mut self, s3: u8, s2: u8, s1: u8) {
        let fields = [s1, s2, s3];
        let mut entries: Vec<(i32, usize, u8)> = fields
            .iter()
            .enumerate()
            .map(|(k, &f)| (self.src(f) as i32, k, f))
            .collect();
        entries.sort_by_key(|&(v, k, _)| (v, k));
        let base = |f: u8| (self.resolve(f) >> 4 & 7) as u32;
        let (low, mid, high) = (base(entries[0].2), base(entries[1].2), base(entries[2].2));
        self.seg = self.seg & !0x777 | low | mid << 4 | high << 8;
        let (min, max) = (entries[0].0 as i64, entries[2].0 as i64);
        let flags = if max - 1 < min { 0x3C } else { 0 };
        self.clip = (self.clip << 4) & 0xFC | flags;
    }

    /// The step instructions ([SPEC] 2.6): S2 to a pixel engine register,
    /// and the scalar or vector add of the operands into D.
    fn step_op(&mut self, op: u8, d: u8, s2: u8, s1: u8) {
        use super::pixel::Param;
        if op < 0xD8 {
            let (a, b) = (self.src(s2), self.src(s1));
            let odd = self.resolve(s2) & 1 != 0;
            let param = match (op, odd) {
                (0xD0, false) => Param::XShadow,
                (0xD0, true) => Param::Y,
                (0xD1, _) => Param::Q,
                (0xD2, false) => Param::R,
                (0xD2, true) => Param::V,
                (0xD3, false) => Param::I,
                (0xD3, true) => Param::U,
                (0xD4, _) => Param::F,
                (0xD5, false) => Param::G,
                (0xD5, true) => Param::B,
                (0xD6, false) => Param::Z,
                (0xD6, true) => Param::A,
                _ => Param::Cnt,
            };
            self.step_param(param, a, b);
            self.dst(d, a.wrapping_add(b));
            return;
        }
        let (a0, a1) = self.src_pair(s2);
        let (b0, b1) = self.src_pair(s1);
        let (even, odd) = match op {
            0xD8 => (Param::XShadow, Param::Y),
            0xD9 => (Param::Q, Param::U),
            0xDA => (Param::R, Param::V),
            0xDB => (Param::I, Param::V),
            0xDC => (Param::Z, Param::V),
            0xDD => (Param::G, Param::B),
            0xDE => (Param::Z, Param::A),
            _ => (Param::Cnt, Param::None),
        };
        self.step_param(even, a0, b0);
        self.step_param(odd, a1, b1);
        self.dst_pair(d, (a0.wrapping_add(b0), a1.wrapping_add(b1)));
    }

    /// A step's parameter to the pixel engine; X, U and V take their
    /// direction from the delta's sign, Q goes through the seed ROM too.
    fn step_param(&mut self, param: super::pixel::Param, value: u32, delta: u32) {
        use super::pixel::Param;
        let negative = (delta as i32) < 0;
        match param {
            Param::XShadow => self.pe.x_dir = negative,
            Param::U => self.pe.u_dir = negative,
            Param::V => self.pe.v_dir = negative,
            Param::Q => self.seed_op(value),
            _ => {}
        }
        if param == Param::Q {
            self.pe.q = value;
        } else {
            self.pe.set(param, value);
        }
    }

    fn undefined(&mut self, op: u8, ir: u32, at: u32) {
        if !self.undefined.contains(&op) {
            self.undefined.push(op);
            self.note(format!(
                "[VERITE] RISC instruction {:08X} at {:08X} isn't a V1000's",
                ir, at
            ));
        }
    }

    /// A register pair the parameterized draws read: `field + offset`.
    pub(super) fn draw_reg(&mut self, field: u8, offset: u8) -> u32 {
        self.src(field.wrapping_add(offset))
    }

    pub(super) fn set_draw_reg(&mut self, field: u8, offset: u8, value: u32) {
        self.dst(field.wrapping_add(offset), value);
    }
}

#[cfg(test)]
pub mod asm {
    //! Instruction words for the tests and for the code this emulator puts
    //! in the card's memory.
    pub fn int(op: u8, d: u8, s2: u8, s1: u8) -> u32 {
        (op as u32) << 24 | (d as u32) << 16 | (s2 as u32) << 8 | s1 as u32
    }
    pub fn imm16(op: u8, d: u8, imm: u16) -> u32 {
        (op as u32) << 24 | (d as u32) << 16 | imm as u32
    }
    pub fn branch(op: u8, s1: u8, words: i16) -> u32 {
        (op as u32) << 24 | (words as u16 as u32) << 8 | s1 as u32
    }
    pub fn jump(op: u8, field: u32) -> u32 {
        (op as u32) << 24 | field & 0xFF_FFFF
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn absolute_jumps_reach_the_rom_with_their_top_bit() {
        assert_eq!(absolute(0x200), 0x800);
        assert_eq!(absolute(0xFF_FFFC), 0xFFFF_FFF0);
        assert_eq!(next(0x00FF_FFFC), 0);
        assert_eq!(next(0xFFFF_FFFC), 0xFF00_0000);
    }

    #[test]
    fn f2i_rounds_down_and_saturates() {
        assert_eq!(f2i(1.75f32.to_bits(), 0), 1);
        assert_eq!(f2i((-1.25f32).to_bits(), 0), -2i32 as u32);
        assert_eq!(f2i(1.5f32.to_bits(), 16), 0x18000);
        assert_eq!(f2i(1e20f32.to_bits(), 0), i32::MAX as u32);
    }

    #[test]
    fn multiplies_shift_the_signed_product() {
        assert_eq!(alu(0x1A, 0x0002_0000, 0x0003_0000), 0x0006_0000);
        assert_eq!(alu(0x18, -3i32 as u32, 5), -15i32 as u32);
        assert_eq!(alu(0x1C, 0x8000_0000, 0x8000_0000), 0x4000_0000);
    }
}
