// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from rust-dos (src/verite/lle).

//! The board's ROM as the RISC sees it, and what this emulator puts there
//! when it has no dump of a real board's: a microcode loader.
//!
//! Creative's CGL modules for the 3D Blaster PCI start the RISC on a
//! loader they copy from the board's ROM (2 KB at ROM offset 10000h, to
//! local memory at 3FF800h), and send it their microcode through the
//! FIFO: for each section its load address, its length in words and the
//! words; a zero address and length end the stream, and the microcode
//! starts at the first section's address. Our ROM's loader is a stub that
//! stores to a control address and waits; the store hands the FIFO to
//! `Core::loader_pump`, which does the loading here.
//!
//! The 1.70 modules keep their microcode's words XORed with a repeating
//! 128-byte key, which their board's loader removes. Microcode is mostly
//! zero bytes, so the key is each position's most common byte in a long
//! section; the stream's shorter sections use the key found before. Plain
//! microcode has few positions where that byte isn't 0, and is stored as
//! sent.

use super::{Core, mem};

/// The ROM's size ([SPEC] 5: 128 KB), mirrored over the top 16 MB of the
/// RISC's address space but for the control registers.
pub const ROM_SIZE: usize = 128 * 1024;

/// The store that starts the loader: an address among the pixel engine's
/// state registers that none of them uses.
pub const LOADER_PORT: u32 = 0xFFE0_0FF8;

/// Where the loader stub is in the ROM.
const STUB_AT: usize = 0x1_0000;

/// The ROM's loader stub: point RA at the loader port, store to it, and
/// spin should the RISC ever get past it.
const STUB: [u32; 6] = [
    0x77FE_FFE0, // lui    ra, #FFE0h
    0x4BFE_FE0F, // addsl8 ra, ra, #0Fh
    0x00FE_FEF8, // add    ra, ra, #F8h
    0x7A00_00FE, // sw     0(ra), zero
    0x68FF_FFFF, // rjmp   .
    0x0000_0000, // nop
];

/// The reset vector's code: spin until the host takes over.
const RESET: [u32; 2] = [0x68FF_FFFF, 0];

/// A ROM image with the loader stub and the reset loop, the rest FFh.
pub fn rom_image() -> Vec<u8> {
    let mut rom = vec![0xFF; ROM_SIZE];
    let mut put = |at: usize, words: &[u32]| {
        for (k, word) in words.iter().enumerate() {
            rom[at + 4 * k..at + 4 * k + 4].copy_from_slice(&word.to_be_bytes());
        }
    };
    put(STUB_AT, &STUB);
    put(ROM_SIZE - 16, &RESET);
    rom
}

/// The palette loader the Rendition BIOS's AX=1582h leaves at local
/// address 0: Speedy3D's microcode calls it through a pointer at F360h
/// (0 in cleared memory) with JMPL, and it takes a FIFO word of the first
/// index << 16 | the count, then the count's words of 00RRGGBB, which it
/// writes to the RAMDAC's palette, and returns through RA.
pub const PALETTE_LOADER: [u32; 15] = [
    0x77A0_FFE0, // lui    %160, #FFE0h
    0x4BA0_A005, // addsl8 %160, %160, #05h
    0x00A0_A080, // add    %160, %160, #80h      ; FFE00580h, the write index
    0x10A1_0021, // add    %161, zero, fifo
    0x47A2_A110, // sr     %162, %161, #16
    0x7800_A2A0, // sb     0(%160), %162
    0x14A1_A109, // and    %161, %161, h1msk     ; the count
    0x6000_05A1, // bez    %161, done
    0x0000_0000, // nop
    0x10A3_0021, // loop: add %163, zero, fifo
    0x01A1_A101, // sub    %161, %161, #1
    0x61FF_FDA1, // bnez   %161, loop
    0x7A02_A3A0, // sw     2(%160), %163         ; FFE00588h, the data
    0x6F00_00FE, // done: jmprl zero, (ra)
    0x0000_0000, // nop
];

/// Where the loader is in the stream.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
enum Expect {
    #[default]
    Address,
    Length,
    Words,
}

#[derive(Clone, Debug, Default)]
pub struct Loader {
    pub active: bool,
    expect: Expect,
    address: u32,
    left: u32,
    /// Where the microcode starts: the first section's address.
    start: Option<u32>,
    /// The section's words so far.
    words: Vec<u32>,
    /// The key the stream's microcode is XORed with, once found.
    key: Vec<u8>,
}

/// The bytes a section needs for its key to show, and the key's bytes that
/// aren't 0 for it to be one.
const KEY_SAMPLES: usize = 1024;
const KEY_NONZERO: usize = 96;

/// The 128-byte key a section's bytes are XORed with: the most common
/// byte at each position, which is 0 for plain microcode.
fn key(bytes: &[u8]) -> [u8; 128] {
    std::array::from_fn(|k| {
        let mut count = [0u32; 256];
        for &b in bytes.iter().skip(k).step_by(128) {
            count[b as usize] += 1;
        }
        // The lowest byte among the most common, so ties go to 0.
        (0..256)
            .max_by_key(|&b| (count[b], std::cmp::Reverse(b)))
            .unwrap_or(0) as u8
    })
}

/// A section's words as the RISC is to see them: the FIFO's words as they
/// came, or, keyed, their bytes in order XORed with the key and read as
/// big-endian words. `found` is the stream's key, which a long enough
/// section sets.
fn unkeyed(words: &[u32], found: &mut Vec<u8>) -> Vec<u32> {
    let bytes: Vec<u8> = words.iter().flat_map(|w| w.to_le_bytes()).collect();
    if bytes.len() >= KEY_SAMPLES {
        let key = key(&bytes);
        *found = if key.iter().filter(|&&b| b != 0).count() >= KEY_NONZERO {
            key.to_vec()
        } else {
            Vec::new()
        };
    }
    if found.len() != 128 {
        return words.to_vec();
    }
    let key = &found[..];
    bytes
        .as_chunks::<4>()
        .0
        .iter()
        .enumerate()
        .map(|(i, b)| {
            let x: [u8; 4] = std::array::from_fn(|k| b[k] ^ key[(4 * i + k) % 128]);
            u32::from_be_bytes(x)
        })
        .collect()
}

impl Core {
    /// The ROM's stub stored to the loader port: the FIFO is the loader's
    /// from now on, and the RISC waits.
    pub(super) fn loader_begin(&mut self) {
        self.loader = Loader {
            active: true,
            ..Loader::default()
        };
        let line = format!(
            "[VERITE] Microcode loader started by the RISC at {:08X}",
            self.pc
        );
        self.note(line);
    }

    /// The loader takes the words in the input FIFO.
    pub(super) fn loader_pump(&mut self, mem: &mut [u8]) {
        while self.loader.active {
            let Some(word) = self.pop_input() else {
                return;
            };
            let l = &mut self.loader;
            match l.expect {
                Expect::Address => {
                    l.address = word;
                    l.expect = Expect::Length;
                }
                Expect::Length if word == 0 => {
                    l.expect = Expect::Address;
                    if l.address == 0 {
                        // The end: the microcode starts.
                        let start = l.start.unwrap_or(0);
                        l.active = false;
                        self.pc = start;
                        self.npc = super::risc::next(start);
                        self.halted = false;
                        self.note(format!(
                            "[VERITE] Microcode loaded; it starts at {:08X}",
                            start
                        ));
                    }
                }
                Expect::Length => {
                    l.left = word;
                    l.start.get_or_insert(l.address);
                    l.words.clear();
                    l.expect = Expect::Words;
                }
                Expect::Words => {
                    l.words.push(word);
                    l.left -= 1;
                    if l.left == 0 {
                        l.expect = Expect::Address;
                        let (address, words) = (l.address, std::mem::take(&mut l.words));
                        for (k, word) in unkeyed(&words, &mut l.key).into_iter().enumerate() {
                            mem::write32(mem, address.wrapping_add(4 * k as u32), word);
                        }
                        self.drew = true;
                        self.note(format!(
                            "[VERITE] Microcode: {} words at {:08X}",
                            words.len(),
                            address
                        ));
                    }
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn plain_sections_are_stored_as_sent() {
        let words = vec![0x1234_5678, 0, 0, 0x0000_00FF];
        assert_eq!(unkeyed(&words, &mut Vec::new()), words);
        let code: Vec<u32> = (0..1024)
            .map(|i| if i % 3 == 0 { 0x10A1_0021 } else { 0 })
            .collect();
        assert_eq!(unkeyed(&code, &mut Vec::new()), code);
    }

    #[test]
    fn keyed_sections_lose_their_key() {
        // Mostly zero words, keyed byte by byte as the modules keep them.
        let plain: Vec<u32> = (0..512)
            .map(|i| if i % 7 == 0 { 0x6C00_0200 } else { 0 })
            .collect();
        let key: Vec<u8> = (0..128).map(|i| (i * 37 + 11) as u8 | 1).collect();
        let sent: Vec<u32> = plain
            .iter()
            .enumerate()
            .map(|(i, w)| {
                let b = w.to_be_bytes();
                u32::from_le_bytes(std::array::from_fn(|k| b[k] ^ key[(4 * i + k) % 128]))
            })
            .collect();
        let mut found = Vec::new();
        assert_eq!(unkeyed(&sent, &mut found), plain);
        // A short section after it, with the same key.
        assert_eq!(unkeyed(&sent[..4], &mut found), &plain[..4]);
    }
}
