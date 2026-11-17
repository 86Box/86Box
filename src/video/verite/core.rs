// SPDX-License-Identifier: GPL-2.0-or-later

// The 86Box C device owns VRAM and bus mappings. The Rust core borrows
// that VRAM only for the duration of each call.
#![allow(dead_code)]

use std::ffi::c_void;
use std::slice;

mod verite {
    pub mod draw {
        pub fn stride(code: u32) -> u32 {
            const HIGH: [u32; 8] = [0, 16, 32, 64, 128, 1024, 2048, 4096];
            const LOW: [u32; 8] = [0, 256, 512, 1024, 4, 768, 576, 1152];
            HIGH[(code >> 4 & 7) as usize] + LOW[(code & 7) as usize]
        }
    }

    pub fn swap(word: u32, mode: u8) -> u32 {
        match mode {
            0 => word,
            1 => word.swap_bytes(),
            2 => (word & 0xFF00_FF00) >> 8 | (word & 0x00FF_00FF) << 8,
            _ => word.rotate_left(16),
        }
    }
}

mod lle;
use lle::Core;

#[repr(C)]
pub struct Callbacks {
    read: extern "C" fn(*mut c_void, u8, u8) -> u32,
    write: extern "C" fn(*mut c_void, u8, u32, u8),
    dma_read: extern "C" fn(*mut c_void, u32) -> u32,
}

struct Board<'a> {
    callbacks: &'a Callbacks,
    opaque: *mut c_void,
}

impl lle::Board for Board<'_> {
    fn ext_read(&mut self, reg: u8, len: u8) -> u32 {
        (self.callbacks.read)(self.opaque, reg, len)
    }

    fn ext_write(&mut self, reg: u8, value: u32, len: u8) {
        (self.callbacks.write)(self.opaque, reg, value, len)
    }

    fn dma_read(&mut self, address: u32) -> u32 {
        (self.callbacks.dma_read)(self.opaque, address)
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn verite_core_new() -> *mut Core {
    Box::into_raw(Box::new(Core::new(lle::rom_image())))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn verite_core_free(core: *mut Core) {
    drop(unsafe { Box::from_raw(core) });
}

#[unsafe(no_mangle)]
pub extern "C" fn verite_core_reset(core: &mut Core) {
    core.reset(true);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn verite_core_read(
    core: &mut Core,
    reg: u8,
    len: u8,
    vram: *const u8,
    size: usize,
) -> u32 {
    core.io_read(reg, len, unsafe { slice::from_raw_parts(vram, size) })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn verite_core_write(
    core: &mut Core,
    reg: u8,
    value: u32,
    len: u8,
    vram: *mut u8,
    size: usize,
    callbacks: &Callbacks,
    opaque: *mut c_void,
) {
    core.io_write(
        reg,
        value,
        len,
        unsafe { slice::from_raw_parts_mut(vram, size) },
        &mut Board { callbacks, opaque },
    );
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn verite_core_run(
    core: &mut Core,
    now_ns: u64,
    vram: *mut u8,
    size: usize,
    callbacks: &Callbacks,
    opaque: *mut c_void,
) {
    core.catch_up(
        now_ns,
        unsafe { slice::from_raw_parts_mut(vram, size) },
        &mut Board { callbacks, opaque },
    );
    core.log.clear();
}

#[unsafe(no_mangle)]
pub extern "C" fn verite_core_retrace(core: &mut Core) {
    core.vertical_retrace();
}

#[unsafe(no_mangle)]
pub extern "C" fn verite_core_status(core: &mut Core) -> u32 {
    let flags = u32::from(core.irq()) | (u32::from(std::mem::take(&mut core.drew)) << 1);
    flags | (u32::from(core.mem_endian()) << 8)
}

#[unsafe(no_mangle)]
pub extern "C" fn verite_core_bank(core: &Core) -> u32 {
    core.bank_select()
}

#[unsafe(no_mangle)]
pub extern "C" fn verite_core_mode(core: &Core) -> u8 {
    core.mode()
}
