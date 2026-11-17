// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from rust-dos (src/verite/lle).

//! The V1000 emulated at a low level: its RISC processor runs the
//! microcode a program loads into the card's memory, instruction by
//! instruction, and its pixel engine draws what the microcode tells it to.
//! Whatever microcode the guest brings runs: Rendition's Speedy3D builds,
//! the Windows drivers' 2D and 3D microcode, and the microcode of
//! Creative's CGL modules.
//!
//! The chip follows Rendition's V2200 Technical Specification (1159-001
//! Rev B, "[SPEC]"), whose V1000 compatibility mode is the V1000's
//! instruction set, and xf86-video-rendition for how hosts drive the RISC.
//!
//! The card's memory is the VESA frame buffer's, `vram`, in 32-bit
//! little-endian words: the RISC is big-endian, so its byte at address A
//! is `vram[A ^ 3]` and its half-word at A the little-endian one at
//! `A ^ 2`, while a host access with MemEndian 0 ("no swap", for words)
//! lands on the same word unchanged. `vram`'s length is a power of two.

mod loader;
mod pixel;
mod risc;

use std::collections::VecDeque;

pub use loader::rom_image;
#[cfg(test)]
use loader::PALETTE_LOADER;
pub use pixel::PixelEngine;

/// What the chip needs of the board around it.
pub trait Board {
    /// The board's registers past the chip's own (I/O 80h-FFh: the CRTC,
    /// memory control, RAMDAC and clocks), from the host or from the RISC
    /// at FFE00400h on.
    fn ext_read(&mut self, io: u8, len: u8) -> u32;
    fn ext_write(&mut self, io: u8, value: u32, len: u8);
    /// A dword of the PC's memory at physical address `phys`, which the
    /// card reads by bus mastering.
    fn dma_read(&mut self, phys: u32) -> u32;
}

/// The chip's own I/O registers ([SPEC] 5.2), offsets from BAR1.
pub mod io {
    pub const FIFO: u8 = 0x00;
    pub const FIFO_IN_FREE: u8 = 0x40;
    pub const FIFO_OUT_VALID: u8 = 0x41;
    pub const COMM: u8 = 0x42;
    pub const MEM_ENDIAN: u8 = 0x43;
    pub const INTR: u8 = 0x44;
    pub const INTR_EN: u8 = 0x46;
    pub const DEBUG: u8 = 0x48;
    pub const LOW_WATER_MARK: u8 = 0x49;
    pub const STATUS: u8 = 0x4A;
    pub const DMA_CMD_PTR: u8 = 0x50;
    pub const DMA_ADDRESS: u8 = 0x54;
    pub const DMA_LENGTH: u8 = 0x58;
    pub const MEM_BASE: u8 = 0x5E;
    pub const STATE_INDEX: u8 = 0x60;
    pub const STATE_DATA: u8 = 0x64;
    pub const MODE: u8 = 0x72;
    pub const BANK_SELECT: u8 = 0x74;
}

/// The bits of Intr and IntrEn ([SPEC] 5.5).
pub mod intr {
    pub const VERT: u16 = 0x0001;
    pub const FIFO_LOW: u16 = 0x0002;
    pub const RISC: u16 = 0x0004;
    pub const HALT: u16 = 0x0008;
    pub const FIFO_ERROR: u16 = 0x0010;
    pub const DMA: u16 = 0x0040;
    pub const IDLE: u16 = 0x0400;
}

/// The bits of Debug ([SPEC] 5.7).
pub mod debug {
    pub const SOFT_RESET: u8 = 0x01;
    pub const HOLD: u8 = 0x02;
    pub const STEP: u8 = 0x04;
}

/// The bits of Mode ([SPEC] 5.12).
pub mod mode {
    pub const VESA: u8 = 0x01;
    pub const VGA: u8 = 0x02;
    pub const DMA: u8 = 0x08;
}

/// The input FIFO's and the output FIFO's entries.
const IN_FIFO: usize = 32;
const OUT_FIFO: usize = 8;

/// Where the RISC starts after a reset: in the ROM ([SPEC] 12).
pub const RESET_PC: u32 = 0xFFFF_FFF0;

/// Instructions the RISC may run ahead of the emulated clock, so that it
/// takes a word the host writes at once when it waits for one, and owed
/// at most after the host left it alone a while.
const AHEAD: i64 = 2_000;

/// The board register reads that end a run (`Core::polls`).
const POLLS: u32 = 64;
const MOST_OWED: i64 = 200_000;

/// The PCI bus master: the command list ([SPEC] 6) and the block it reads.
#[derive(Clone, Debug, Default)]
struct Dma {
    busy: bool,
    /// The command (address, length pair) being carried out.
    ptr: u32,
    /// Where the block's next word is, the bytes left of it, and how its
    /// words' bytes are swapped.
    addr: u32,
    left: u32,
    swap: u8,
}

pub struct Core {
    /// The register file; 0-63 are constants and special registers, read
    /// and written through `risc::special_*`.
    reg: [u32; 256],
    /// The instruction about to be decoded and the one after it: a jump
    /// sets `npc`, so that the delay slot at `pc` runs first.
    pub pc: u32,
    npc: u32,
    flag: u32,
    seg: u32,
    seed: u32,
    scale: u32,
    seed_index: u32,
    clip: u32,
    timer: u32,
    riscintr: u32,
    halted: bool,
    /// Waiting for the input FIFO to have a word, or the output FIFO room.
    stalled: bool,
    pub pe: PixelEngine,

    in_fifo: VecDeque<u32>,
    out_fifo: VecDeque<u32>,
    /// A FIFO word written a byte or a word at a time.
    partial: u32,
    partial_mask: u32,
    /// The plain registers' bytes, 10h-7Fh, as last written.
    regs: Vec<u8>,
    intr: u16,
    /// An instruction the host forced into the decode stage.
    forced: Option<u32>,
    dma: Dma,
    /// The microcode loader of the board ROM's image (`loader`).
    loader: loader::Loader,
    /// The board's ROM as the RISC sees it, mirrored over FF000000h on.
    rom: Vec<u8>,

    /// How fast the RISC runs (instructions in an emulated microsecond),
    /// when it last ran up to, and the instructions it is owed (negative
    /// when it ran ahead).
    pub mhz: u32,
    last_ns: u64,
    credit: i64,

    /// Whether it wrote the card's memory since the board last looked.
    pub drew: bool,
    /// Instructions run, for the debugger's status.
    pub instructions: u64,
    /// The board's registers read in this run: a RISC that polls the
    /// CRTC for the beam waits for time to pass, which it doesn't in a
    /// run, so the run ends after many.
    polls: u32,
    /// Lines for the emulator's log.
    pub log: Vec<String>,
    /// Opcodes seen that the chip doesn't have, logged once each.
    undefined: Vec<u8>,
    /// Instructions to trace into the log (`RUST_DOS_VERITE_RISC_TRACE`),
    /// and control register accesses (`RUST_DOS_VERITE_CTL_LOG`).
    pub trace_left: u64,
    pub ctl_left: u64,
    /// Instructions run before the trace starts (`skip:count`).
    pub trace_skip: u64,
}

/// A count from the environment variable `name`, for the traces: `count`,
/// or `skip:count` for the count after the first `skip`.
fn env_count(name: &str) -> (u64, u64) {
    let value = std::env::var(name).unwrap_or_default();
    let (skip, count) = value.split_once(':').unwrap_or(("0", &value));
    (skip.parse().unwrap_or(0), count.parse().unwrap_or(0))
}

impl Core {
    pub fn new(rom: Vec<u8>) -> Self {
        let mut core = Self {
            reg: [0; 256],
            pc: RESET_PC,
            npc: risc::next(RESET_PC),
            flag: 0,
            seg: 0,
            seed: 0,
            scale: 0,
            seed_index: 0,
            clip: 0,
            timer: 0,
            riscintr: 0,
            halted: false,
            stalled: false,
            pe: PixelEngine::default(),
            in_fifo: VecDeque::with_capacity(IN_FIFO),
            out_fifo: VecDeque::with_capacity(OUT_FIFO),
            partial: 0,
            partial_mask: 0,
            regs: vec![0; 0x80],
            intr: 0,
            forced: None,
            dma: Dma::default(),
            loader: loader::Loader::default(),
            rom,
            mhz: 25,
            last_ns: 0,
            credit: 0,
            drew: false,
            instructions: 0,
            polls: 0,
            log: Vec::new(),
            undefined: Vec::new(),
            trace_left: env_count("RUST_DOS_VERITE_RISC_TRACE").1,
            ctl_left: env_count("RUST_DOS_VERITE_CTL_LOG").1,
            trace_skip: env_count("RUST_DOS_VERITE_RISC_TRACE").0,
        };
        core.reset(true);
        core
    }

    /// A reset: at power-on (`hard`) the host registers too, else as the
    /// Debug register's SoftReset leaves the chip ([SPEC] 12): the RISC at
    /// the ROM's reset vector, the FIFOs empty, the pixel engine's state
    /// as at reset.
    pub fn reset(&mut self, hard: bool) {
        self.pc = RESET_PC;
        self.npc = risc::next(RESET_PC);
        self.flag = 0x8;
        self.seg = 0;
        self.clip = 0;
        self.riscintr = 0;
        self.halted = false;
        self.stalled = false;
        self.pe = PixelEngine::default();
        self.in_fifo.clear();
        self.out_fifo.clear();
        self.partial_mask = 0;
        self.forced = None;
        self.dma = Dma::default();
        self.loader = loader::Loader::default();
        if hard {
            self.regs.fill(0);
            self.regs[io::DEBUG as usize] = debug::HOLD;
            self.regs[io::MODE as usize] = mode::VGA;
            self.regs[0x5C] = 4;
            self.intr = 0;
            self.reg = [0; 256];
        }
    }

    /// The board's ROM: a dump of a real board's, or `rom_image`.
    pub fn set_rom(&mut self, rom: Vec<u8>) {
        self.rom = rom;
    }

    // --- The host's view ---

    fn byte(&self, reg: u8) -> u8 {
        self.regs[reg as usize & 0x7F]
    }

    fn dword(&self, reg: u8) -> u32 {
        (0..4).map(|i| (self.byte(reg + i) as u32) << (8 * i)).sum()
    }

    pub fn debug(&self) -> u8 {
        self.byte(io::DEBUG)
    }

    pub fn mode(&self) -> u8 {
        self.byte(io::MODE)
    }

    pub fn mem_endian(&self) -> u8 {
        self.byte(io::MEM_ENDIAN) & 3
    }

    pub fn bank_select(&self) -> u32 {
        self.dword(io::BANK_SELECT)
    }

    /// Whether the chip asks for its interrupt.
    pub fn irq(&self) -> bool {
        self.intr & self.dword(io::INTR_EN) as u16 != 0
    }

    /// Whether the board should run the RISC without the host asking: it
    /// has instructions to run or a DMA to carry out.
    pub fn busy(&self) -> bool {
        self.runnable() || self.dma.busy && self.in_fifo.len() < IN_FIFO
    }

    /// Whether the RISC would carry out instructions if run.
    fn runnable(&self) -> bool {
        let debug = self.debug();
        debug & (debug::SOFT_RESET | debug::HOLD) == 0
            && !self.halted
            && !self.stalled
            && !self.loader.active
            || self.loader.active && !self.in_fifo.is_empty()
    }

    /// Whether a vertical retrace interrupts the host or the RISC.
    pub fn wants_retrace(&self) -> bool {
        self.dword(io::INTR_EN) as u16 & intr::VERT != 0 || self.riscintr & 0x1_0000 != 0
    }

    /// The vertical retrace began: Intr's and riscintr's VertIntr.
    pub fn vertical_retrace(&mut self) {
        self.intr |= intr::VERT;
        self.riscintr |= 1;
    }

    /// Whether the host may write a word to the input FIFO.
    pub fn fifo_full(&self) -> bool {
        self.in_fifo.len() >= IN_FIFO
    }

    /// A word for the input FIFO, from the host or the DMA.
    fn push_input(&mut self, word: u32) {
        if self.in_fifo.len() >= IN_FIFO {
            if self.intr & intr::FIFO_ERROR == 0 {
                let line = format!(
                    "[VERITE] Input FIFO full: {:08X} lost (RISC at {:08X}, debug {:02X}, halted {}, dma {})",
                    word,
                    self.pc,
                    self.debug(),
                    self.halted,
                    self.dma.busy
                );
                self.note(line);
            }
            self.intr |= intr::FIFO_ERROR;
            return;
        }
        self.in_fifo.push_back(word);
        self.stalled = false;
    }

    /// A word the RISC took from the input FIFO: the low water mark's
    /// interrupt as the free entries rise above it.
    fn pop_input(&mut self) -> Option<u32> {
        let word = self.in_fifo.pop_front()?;
        let free = IN_FIFO - self.in_fifo.len();
        if free == self.byte(io::LOW_WATER_MARK) as usize % 32 + 1 {
            self.intr |= intr::FIFO_LOW;
        }
        Some(word)
    }

    /// The host's read of `len` bytes at `reg` (below 80h).
    pub fn io_read(&mut self, reg: u8, len: u8, mem: &[u8]) -> u32 {
        if reg < 0x10 {
            // The output FIFO, through its byte swapping apertures.
            let word = match self.out_fifo.pop_front() {
                Some(word) => {
                    self.stalled = false;
                    word
                }
                None => {
                    self.intr |= intr::FIFO_ERROR;
                    0
                }
            };
            return crate::verite::swap(word, reg >> 2 & 3) >> (8 * (reg & 3));
        }
        let state = (io::STATE_DATA..io::STATE_DATA + 4)
            .contains(&reg)
            .then(|| self.state(self.byte(io::STATE_INDEX), mem));
        let mut value = 0;
        for i in 0..len {
            let r = reg.wrapping_add(i);
            let byte = match r {
                io::FIFO_IN_FREE => (IN_FIFO - self.in_fifo.len()).min(31) as u8,
                io::FIFO_OUT_VALID => self.out_fifo.len().min(7) as u8,
                io::COMM => self.byte(io::COMM) & 0x0F | (self.flag & 0xF0) as u8,
                io::MEM_ENDIAN => self.mem_endian() | if self.dma.busy { 0x80 } else { 0 },
                0x44 | 0x45 => (self.intr >> (8 * (r - io::INTR))) as u8,
                io::STATUS => self.status(),
                0x50..=0x53 => {
                    ((self.dma.ptr & !3 | self.dma.busy as u32) >> (8 * (r - io::DMA_CMD_PTR)))
                        as u8
                }
                0x54..=0x57 => (self.dma.addr >> (8 * (r - io::DMA_ADDRESS))) as u8,
                0x58..=0x5B => {
                    ((self.dma.left | self.dma.swap as u32) >> (8 * (r - io::DMA_LENGTH))) as u8
                }
                0x64..=0x67 => (state.unwrap_or(0) >> (8 * (r - io::STATE_DATA))) as u8,
                r if r >= 0x80 => 0,
                r => self.byte(r),
            };
            value |= (byte as u32) << (8 * i);
        }
        value
    }

    /// The Status register ([SPEC] 5.9): halted, held, the engines idle
    /// (they always are: draws finish within their instruction), stalled
    /// on a FIFO, and idle overall.
    fn status(&self) -> u8 {
        let held = self.debug() & debug::HOLD != 0;
        let (stall_in, stall_out) = (
            self.stalled && self.in_fifo.is_empty(),
            self.stalled && !self.in_fifo.is_empty(),
        );
        let mut status = self.halted as u8
            | (held as u8) << 1
            | 0x0C
            | (stall_in as u8) << 4
            | (stall_out as u8) << 5;
        if self.halted || held || self.stalled {
            status |= 0x80;
        }
        status
    }

    /// What StateData reads with StateIndex at `index` ([SPEC] 10): the
    /// pixel engine's state, or the decode stage's instruction, PC and
    /// first source register.
    fn state(&self, index: u8, mem: &[u8]) -> u32 {
        match index {
            0x80 => self.decode_ir(mem),
            0x81 => self.pc,
            0x82 => self.peek(self.decode_ir(mem) as u8),
            _ => self.pe.state(index),
        }
    }

    /// The instruction in the decode stage: the host's, or the one at PC.
    fn decode_ir(&self, mem: &[u8]) -> u32 {
        self.forced.unwrap_or_else(|| self.fetch(self.pc, mem))
    }

    /// The host's write of `len` bytes of `value` at `reg` (below 80h).
    pub fn io_write(
        &mut self,
        reg: u8,
        value: u32,
        len: u8,
        mem: &mut [u8],
        board: &mut dyn Board,
    ) {
        if reg < 0x10 {
            self.fifo_write(reg, value, len, mem, board);
            return;
        }
        for i in 0..len {
            let r = reg.wrapping_add(i);
            let byte = (value >> (8 * i)) as u8;
            match r {
                io::FIFO_IN_FREE | io::FIFO_OUT_VALID | io::STATUS | 0x54..=0x5B | io::MEM_BASE => {
                }
                // Write 1 to clear; the RISC's SignalSys goes with RISCIntr.
                0x44 | 0x45 => {
                    let bits = (byte as u16) << (8 * (r - io::INTR));
                    self.intr &= !bits;
                    if bits & intr::RISC != 0 {
                        self.flag &= !1;
                    }
                }
                r if r < 0x80 => self.regs[r as usize] = byte,
                _ => {}
            }
        }
        let covers = |r: u8| reg <= r && r < reg.wrapping_add(len);
        if covers(io::STATE_DATA + 3) {
            self.forced = Some(self.dword(io::STATE_DATA));
        }
        if covers(io::DEBUG) {
            self.debug_written(mem, board);
        }
        if covers(io::DMA_CMD_PTR + 3) {
            self.dma_start(board);
        }
        if covers(io::MODE) && self.mode() & mode::DMA == 0 {
            self.dma.busy = false;
        }
    }

    /// A FIFO aperture written: bits 3-2 of the port say how its bytes are
    /// swapped, bits 1-0 which of the word's bytes this is.
    fn fifo_write(&mut self, reg: u8, value: u32, len: u8, mem: &mut [u8], board: &mut dyn Board) {
        let shift = 8 * (reg & 3) as u32;
        let mask = if len >= 4 {
            u32::MAX
        } else {
            ((1u32 << (8 * len)) - 1) << shift
        };
        self.partial = self.partial & !mask | (value << shift) & mask;
        self.partial_mask |= mask;
        if self.partial_mask != u32::MAX {
            return;
        }
        self.partial_mask = 0;
        // A full FIFO gets the RISC's attention before the word is lost.
        if self.fifo_full() {
            self.credit -= self.run(mem, board, 20_000) as i64;
        }
        self.push_input(crate::verite::swap(self.partial, reg >> 2 & 3));
    }

    /// Debug written: a soft reset while its bit is set, the hold, and a
    /// single step of the decode stage's instruction.
    fn debug_written(&mut self, mem: &mut [u8], board: &mut dyn Board) {
        let debug = self.debug();
        if debug & debug::SOFT_RESET != 0 {
            self.reset(false);
            return;
        }
        if debug & debug::HOLD != 0 {
            self.intr |= intr::IDLE;
        } else {
            // Let go: the RISC carries on from where it is.
            self.forced = None;
        }
        if debug & debug::STEP != 0 {
            if debug & debug::HOLD != 0 {
                let ir = self.forced.take();
                self.halted = false;
                if !self.step(ir, mem, board) {
                    // Stalled on a FIFO: it stays in decode.
                    self.forced = ir;
                    return;
                }
            }
            self.regs[io::DEBUG as usize] &= !debug::STEP;
        }
    }

    // --- DMA ---

    /// DMACmdPtr written: the list it points to is carried out while the
    /// Mode register allows DMA.
    fn dma_start(&mut self, board: &mut dyn Board) {
        let ptr = self.dword(io::DMA_CMD_PTR) & !3;
        self.dma = Dma {
            busy: ptr != 0 && self.mode() & mode::DMA != 0,
            ptr,
            ..Dma::default()
        };
        if self.dma.busy {
            self.dma_command(board);
        }
    }

    /// The command at the list pointer: a block, a link to another list,
    /// or a null address, the end.
    fn dma_command(&mut self, board: &mut dyn Board) {
        for _ in 0..0x1_0000 {
            let (addr, length) = (
                board.dma_read(self.dma.ptr),
                board.dma_read(self.dma.ptr.wrapping_add(4)),
            );
            if addr == 0 {
                self.dma_done();
                return;
            }
            if length & 0x8000_0000 != 0 {
                self.dma.ptr = addr & !3;
                continue;
            }
            self.dma.addr = addr & !3;
            self.dma.left = length & 0x00FF_FFFC;
            self.dma.swap = (length & 3) as u8;
            if self.dma.left != 0 {
                return;
            }
            self.dma.ptr = self.dma.ptr.wrapping_add(8);
        }
        self.dma_done();
    }

    fn dma_done(&mut self) {
        self.dma.busy = false;
        self.intr |= intr::DMA;
    }

    /// Words of the current block into the input FIFO while it has room.
    fn dma_pump(&mut self, board: &mut dyn Board) {
        while self.dma.busy && self.in_fifo.len() < IN_FIFO {
            let word = board.dma_read(self.dma.addr);
            self.push_input(crate::verite::swap(word, self.dma.swap));
            self.dma.addr = self.dma.addr.wrapping_add(4);
            self.dma.left -= 4;
            if self.dma.left == 0 {
                self.dma.ptr = self.dma.ptr.wrapping_add(8);
                self.dma_command(board);
            }
        }
    }

    // --- Running ---

    /// Run the RISC up to the emulated time `now_ns`, at `mhz` million
    /// instructions a second. Returns the instructions run.
    pub fn catch_up(&mut self, now_ns: u64, mem: &mut [u8], board: &mut dyn Board) -> u64 {
        let elapsed = now_ns.saturating_sub(self.last_ns);
        self.last_ns = self.last_ns.max(now_ns);
        self.credit = (self.credit + (elapsed * self.mhz as u64 / 1000) as i64).min(MOST_OWED);
        let mut used = 0;
        if self.credit + AHEAD > 0 {
            used = self.run(mem, board, (self.credit + AHEAD) as u64);
            self.credit -= used as i64;
        }
        // Time the RISC spent waiting isn't owed to it.
        if !self.runnable() || self.polls >= POLLS {
            self.timer = self.timer.wrapping_add(self.credit.max(0) as u32);
            self.credit = self.credit.min(0);
        }
        used
    }

    /// Run up to `budget` instructions; fewer when the RISC stalls, halts
    /// or is held. Draws count as one.
    pub fn run(&mut self, mem: &mut [u8], board: &mut dyn Board, budget: u64) -> u64 {
        let mut used = 0;
        self.polls = 0;
        while used < budget && self.polls < POLLS {
            if self.dma.busy {
                self.dma_pump(board);
            }
            if self.loader.active {
                self.loader_pump(mem);
                if self.loader.active {
                    break;
                }
            }
            if !self.runnable() {
                break;
            }
            if !self.step(None, mem, board) {
                break;
            }
            used += 1;
        }
        self.instructions += used;
        used
    }

    // --- Diagnostics ---

    fn note(&mut self, line: String) {
        if self.log.len() < 1000 || self.trace_left > 0 {
            self.log.push(line);
        }
    }

}

/// A word stored at `a` in `vram` as the RISC stores it.
pub fn write_word(vram: &mut [u8], a: u32, word: u32) {
    mem::write32(vram, a, word);
}

/// The RISC's view of the card's memory, `mem` (see the module's notes).
pub(crate) mod mem {
    #[inline]
    fn at(mem: &[u8], a: u32) -> usize {
        a as usize & (mem.len() - 1)
    }

    #[inline]
    pub fn read8(mem: &[u8], a: u32) -> u32 {
        mem[at(mem, a) ^ 3] as u32
    }

    #[inline]
    pub fn read16(mem: &[u8], a: u32) -> u32 {
        let i = at(mem, a) & !1 ^ 2;
        u16::from_le_bytes([mem[i], mem[i + 1]]) as u32
    }

    #[inline]
    pub fn read32(mem: &[u8], a: u32) -> u32 {
        let i = at(mem, a) & !3;
        u32::from_le_bytes([mem[i], mem[i + 1], mem[i + 2], mem[i + 3]])
    }

    #[inline]
    pub fn write8(mem: &mut [u8], a: u32, v: u32) {
        let i = at(mem, a) ^ 3;
        mem[i] = v as u8;
    }

    #[inline]
    pub fn write16(mem: &mut [u8], a: u32, v: u32) {
        let i = at(mem, a) & !1 ^ 2;
        mem[i..i + 2].copy_from_slice(&(v as u16).to_le_bytes());
    }

    #[inline]
    pub fn write32(mem: &mut [u8], a: u32, v: u32) {
        let i = at(mem, a) & !3;
        mem[i..i + 4].copy_from_slice(&v.to_le_bytes());
    }
}

#[cfg(test)]
mod tests;
