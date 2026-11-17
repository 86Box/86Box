// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from rust-dos (src/verite/lle).

use super::risc::asm::*;
use super::*;

/// A board that remembers what the chip wrote to its registers.
#[derive(Default)]
struct TestBoard {
    writes: Vec<(u8, u32, u8)>,
    ram: Vec<u32>,
}

impl Board for TestBoard {
    fn ext_read(&mut self, _io: u8, _len: u8) -> u32 {
        0
    }
    fn ext_write(&mut self, io: u8, value: u32, len: u8) {
        self.writes.push((io, value, len));
    }
    fn dma_read(&mut self, phys: u32) -> u32 {
        self.ram.get(phys as usize / 4).copied().unwrap_or(0)
    }
}

const HALT: u32 = 0x6D00_0000;

/// A chip running `program` from `at`, in 1 MB of memory.
fn machine(program: &[u32], at: u32) -> (Core, Vec<u8>, TestBoard) {
    let mut memory = vec![0u8; 1 << 20];
    for (k, &word) in program.iter().enumerate() {
        mem::write32(&mut memory, at + 4 * k as u32, word);
    }
    let mut core = Core::new(rom_image());
    core.regs[io::DEBUG as usize] = 0;
    core.pc = at;
    core.npc = risc::next(at);
    (core, memory, TestBoard::default())
}

fn run(core: &mut Core, memory: &mut [u8], board: &mut TestBoard) -> u64 {
    core.run(memory, board, 10_000)
}

#[test]
fn alu_operations_and_delay_slots() {
    let program = [
        imm16(0x76, 128, 0x1234), // li     %128, #1234h
        imm16(0x77, 129, 0x8000), // lui    %129, #8000h
        int(0x10, 130, 128, 129), // add    %130, %128, %129
        int(0x46, 131, 129, 4),   // asr    %131, %129, #4
        int(0x47, 132, 129, 4),   // sr     %132, %129, #4
        branch(0x61, 128, 3),     // bnez   %128, +3
        int(0x00, 133, 0, 7),     // add    %133, zero, #7 (delay slot)
        int(0x00, 134, 0, 1),     // skipped
        int(0x00, 135, 0, 2),     // skipped
        int(0x13, 136, 128, 133), // subf   %136, %128, %133
        HALT,
    ];
    let (mut core, mut memory, mut board) = machine(&program, 0x1000);
    run(&mut core, &mut memory, &mut board);
    assert!(core.halted);
    assert_eq!(core.reg[130], 0x8000_1234);
    assert_eq!(core.reg[131], 0xF800_0000);
    assert_eq!(core.reg[132], 0x0800_0000);
    assert_eq!(core.reg[133], 7);
    assert_eq!((core.reg[134], core.reg[135]), (0, 0));
    assert_eq!(core.reg[136], 7u32.wrapping_sub(0x1234));
    assert_ne!(core.intr & intr::HALT, 0);
}

#[test]
fn vector_operations_work_on_register_pairs() {
    let program = [
        imm16(0x76, 128, 10),
        imm16(0x76, 129, 20),
        imm16(0x76, 130, 1),
        imm16(0x76, 131, 2),
        int(0x20, 132, 128, 130), // addv   %132, %128, %130
        int(0x30, 134, 128, 131), // addvs  %134, %128, %131
        int(0xC0, 136, 0, 3),     // getxy  %136, msb
        HALT,
    ];
    let (mut core, mut memory, mut board) = machine(&program, 0);
    run(&mut core, &mut memory, &mut board);
    assert_eq!(&core.reg[132..136], &[11, 22, 12, 22]);
    assert_eq!(&core.reg[136..138], &[0x8000_0000, 0]);
}

#[test]
fn links_point_past_the_delay_slot() {
    let program = [
        int(0x6B, 128, 0, 0),   // 0: getpc %128
        jump(0x6E, 0x10 >> 2),  // 4: jmpl 10h
        0,                      // 8: nop
        HALT,                   // C
        int(0x6F, 129, 0, 254), // 10: jmprl %129, (ra)
        0,                      // 14: nop
    ];
    let (mut core, mut memory, mut board) = machine(&program, 0);
    run(&mut core, &mut memory, &mut board);
    assert_eq!(core.reg[128], 8);
    assert_eq!(core.reg[254], 0xC);
    assert_eq!(core.reg[129], 0x18);
    assert!(core.halted);
    assert_eq!(core.pc, 0xC);
}

#[test]
fn memory_is_big_endian_to_the_risc() {
    let program = [
        imm16(0x77, 128, 0x1122),
        int(0x4B, 128, 128, 0x33), // addsl8 %128, %128, #33h
        int(0x00, 128, 128, 0x44), // add    %128, %128, #44h
        imm16(0x76, 129, 0x800),
        0x7A00_8081, // sw     0(%129), %128
        0x7082_0181, // lb     %130, 1(%129)
        0x7183_0181, // lh     %131, 1(%129)  (+2)
        0x7801_0081, // sb     1(%129), zero
        HALT,
    ];
    let (mut core, mut memory, mut board) = machine(&program, 0);
    run(&mut core, &mut memory, &mut board);
    assert_eq!(core.reg[128], 0x1122_3344);
    assert_eq!(core.reg[130], 0x22);
    assert_eq!(core.reg[131], 0x3344);
    // The host sees the word as it was stored, its second byte cleared.
    assert_eq!(&memory[0x800..0x804], &[0x44, 0x33, 0x00, 0x11]);
}

#[test]
fn the_fifo_stalls_the_risc_until_the_host_writes() {
    let program = [
        int(0x10, 128, 0, 33), // add %128, zero, fifo
        int(0x00, 33, 128, 1), // add fifo, %128, #1
        HALT,
    ];
    let (mut core, mut memory, mut board) = machine(&program, 0);
    assert_eq!(run(&mut core, &mut memory, &mut board), 0);
    assert!(core.stalled);
    assert_eq!(core.io_read(io::STATUS, 1, &memory) & 0x10, 0x10);
    core.io_write(io::FIFO, 41, 4, &mut memory, &mut board);
    run(&mut core, &mut memory, &mut board);
    assert!(core.halted);
    assert_eq!(core.io_read(io::FIFO_OUT_VALID, 1, &memory), 1);
    assert_eq!(core.io_read(io::FIFO, 4, &memory), 42);
    assert_eq!(core.io_read(io::FIFO_OUT_VALID, 1, &memory), 0);
}

// v1krisc.c's writeRF, risc_writemem, readRF and v1k_start through the
// debug registers.
#[test]
fn the_host_forces_instructions_through_the_debug_registers() {
    let (mut core, mut memory, mut board) = machine(&[], 0);
    let mut force = |core: &mut Core, memory: &mut Vec<u8>, ir: u32| {
        core.io_write(io::STATE_INDEX, 0x80, 1, memory, &mut board);
        core.io_write(io::STATE_DATA, ir, 4, memory, &mut board);
        core.io_write(
            io::DEBUG,
            (debug::HOLD | debug::STEP) as u32,
            1,
            memory,
            &mut board,
        );
        assert_eq!(core.io_read(io::DEBUG, 1, memory), debug::HOLD as u32);
    };
    core.io_write(
        io::DEBUG,
        (debug::SOFT_RESET | debug::HOLD) as u32,
        1,
        &mut memory,
        &mut TestBoard::default(),
    );
    core.io_write(
        io::DEBUG,
        debug::HOLD as u32,
        1,
        &mut memory,
        &mut TestBoard::default(),
    );
    assert_eq!(core.pc, RESET_PC);
    force(&mut core, &mut memory, 0x76FE_0800); // li ra, #800h
    force(&mut core, &mut memory, 0x77FF_1234); // lui fp, #1234h
    force(&mut core, &mut memory, 0x4BFF_FF56);
    force(&mut core, &mut memory, 0x00FF_FF78);
    force(&mut core, &mut memory, 0x7A00_FFFE); // sw 0(ra), fp
    assert_eq!(mem::read32(&memory, 0x800), 0x1234_5678);
    // readRF: "add zero, zero, fp" in decode, then S1.
    core.io_write(
        io::STATE_DATA,
        0x1000_00FF,
        4,
        &mut memory,
        &mut TestBoard::default(),
    );
    core.io_write(
        io::STATE_INDEX,
        0x82,
        1,
        &mut memory,
        &mut TestBoard::default(),
    );
    assert_eq!(core.io_read(io::STATE_DATA, 4, &memory), 0x1234_5678);
    // v1k_start: a jump and the nop in its delay slot.
    force(&mut core, &mut memory, jump(0x6C, 0x800 >> 2));
    force(&mut core, &mut memory, 0);
    core.io_write(
        io::STATE_INDEX,
        0x81,
        1,
        &mut memory,
        &mut TestBoard::default(),
    );
    assert_eq!(core.io_read(io::STATE_DATA, 4, &memory), 0x800);
}

#[test]
fn the_seed_gives_reciprocals() {
    // seedsr D, S2, #n gives 2^(30 + n) / S2.
    let program = [
        imm16(0x76, 129, 4),
        int(0x42, 128, 129, 0),
        int(0x42, 130, 129, 2),
        HALT,
    ];
    let (mut core, mut memory, mut board) = machine(&program, 0);
    run(&mut core, &mut memory, &mut board);
    assert_eq!(core.scale, 3);
    assert_eq!(core.reg[128], 1 << 28);
    assert_eq!(core.reg[130], 1 << 30);
}

#[test]
fn dma_feeds_the_fifo_from_a_command_list() {
    let program = [
        int(0x10, 128, 0, 33),
        int(0x10, 129, 0, 33),
        int(0x10, 130, 0, 33),
        HALT,
    ];
    let (mut core, mut memory, mut board) = machine(&program, 0);
    // The list at 100h: a block of three words at 200h, swapped by bytes.
    board.ram = vec![0; 0x100];
    board.ram[0x40] = 0x200;
    board.ram[0x41] = 12 | 1;
    board.ram[0x80..0x83].copy_from_slice(&[0x1122_3344, 2, 3]);
    core.io_write(io::MODE, mode::DMA as u32, 1, &mut memory, &mut board);
    core.io_write(io::DMA_CMD_PTR, 0x100, 4, &mut memory, &mut board);
    run(&mut core, &mut memory, &mut board);
    assert!(core.halted);
    assert_eq!(
        &core.reg[128..131],
        &[0x4433_2211, 0x0200_0000, 0x0300_0000]
    );
    assert!(!core.dma.busy);
    assert_ne!(core.intr & intr::DMA, 0);
}

/// Pixel engine set up for a 16-bit 565 destination 1024 bytes a line.
fn sixteen_bit(core: &mut Core) {
    core.pe.dst_fmt = 4;
    core.pe.stride = 0x0300; // DstStride0 = 3: 1024 bytes
    core.pe.dst_base = 0x1_0000;
}

#[test]
fn a_parallel_draw_fills_a_span() {
    let program = [
        // stepv xy with XShadow 3, Y 2 (16.16) and Cnt 5.
        imm16(0x77, 128, 3),
        imm16(0x77, 129, 2),
        int(0xD8, 140, 128, 0),
        imm16(0x77, 130, 5),
        int(0xD7, 142, 130, 0),
        int(0xE1, 144, 146, 148), // rdrawp
        HALT,
    ];
    let (mut core, mut memory, mut board) = machine(&program, 0);
    sixteen_bit(&mut core);
    core.pe.fg = 0xF800_F800;
    core.pe.dst_mode = 0xC;
    run(&mut core, &mut memory, &mut board);
    let pixel = |x: u32| mem::read16(&memory, 0x1_0000 + 2 * 1024 + 2 * x);
    assert_eq!(
        (pixel(2), pixel(3), pixel(7), pixel(8)),
        (0, 0xF800, 0xF800, 0)
    );
    assert_eq!(core.pe.cnt, 0);
    assert!(core.drew);
}

#[test]
fn a_parallel_blit_copies_and_xors() {
    let (mut core, mut memory, _) = machine(&[HALT], 0);
    sixteen_bit(&mut core);
    core.pe.stride = 0x0303;
    core.pe.src_base = 0x2_0000;
    core.pe.src_mode = 1 << 4;
    for x in 0..4 {
        mem::write16(&mut memory, 0x2_0000 + 2 * x, 0x100 + x);
        mem::write16(&mut memory, 0x1_0000 + 2 * x, 0x00FF);
    }
    core.pe.dst_mode = 6; // XOR
    core.pe.cnt = 4;
    core.pe.set(pixel::Param::U, 0);
    core.draw_parallel(0xE1, 200, 202, 204, &mut memory);
    for x in 0..4 {
        assert_eq!(mem::read16(&memory, 0x1_0000 + 2 * x), (0x100 + x) ^ 0xFF);
    }
}

#[test]
fn a_parameterized_draw_iterates_its_parameters() {
    let (mut core, mut memory, _) = machine(&[HALT], 0);
    sixteen_bit(&mut core);
    core.pe.dst_mode = 0xC;
    core.pe.dst_fmt = 0xC; // 8888, 4 bytes
    core.pe.stride = 0x0400; // 2 << 2? Shift 2: 4 bytes a line, unused at Y 0
    core.pe.drawctl = 6; // pair1: X, R; pair2 (0): G, B
    // X 0, red 10 rising by 20; green and blue 0.
    core.reg[160] = 0;
    core.reg[161] = 10 << 16;
    core.reg[176] = 1 << 16;
    core.reg[177] = 20 << 16;
    core.pe.cnt = 3;
    core.pe.a = 255 << 16;
    core.draw_param(0xF3, 192, 160, 176, &mut memory);
    let pixel = |x: u32| mem::read32(&memory, 0x1_0000 + 4 * x);
    assert_eq!(pixel(0), 0xFF0A_0000);
    assert_eq!(pixel(1), 0xFF1E_0000);
    assert_eq!(pixel(2), 0xFF32_0000);
    assert_eq!(core.pe.cnt, 0);
    // A single draw adds the deltas into D.
    core.pe.cnt = 1;
    core.draw_param(0xF0, 192, 160, 176, &mut memory);
    assert_eq!((core.reg[192], core.reg[193]), (1 << 16, 30 << 16));
}

#[test]
fn five_pair_draws_use_all_ten_values_and_align_register_groups() {
    let (mut core, mut memory, _) = machine(&[HALT], 0);
    sixteen_bit(&mut core);
    core.pe.dst_mode = 0xC;
    core.pe.dst_fmt = 0xC;
    // X/R, Z/A, no parameters in pairs 3/4, then G/B in pair 5.
    core.pe.drawctl = 6 | 1 << 4 | 5 << 9 | 5 << 13;
    core.reg[160] = 0;
    core.reg[161] = 20 << 16;
    core.reg[162] = 0;
    core.reg[163] = 255 << 16;
    core.reg[168] = 40 << 16;
    core.reg[169] = 60 << 16;
    for k in 0..10 {
        core.reg[176 + k] = (k as u32 + 1) << 16;
    }
    core.pe.cnt = 1;
    // Each field is deliberately unaligned: ten values require a group
    // aligned to sixteen registers, including the destination.
    core.draw_param(0xFC, 199, 167, 183, &mut memory);
    assert_eq!(mem::read32(&memory, 0x1_0000), 0xFF14_283C);
    assert_eq!(core.reg[192], 1 << 16);
    assert_eq!(core.reg[193], 22 << 16);
    assert_eq!(core.reg[200], 49 << 16);
    assert_eq!(core.reg[201], 70 << 16);
    assert_eq!(core.pe.cnt, 0);
}

#[test]
fn a_parameterized_draw_from_the_fifo_pops_the_values_before_the_deltas() {
    let (mut core, mut memory, _) = machine(&[HALT], 0);
    sixteen_bit(&mut core);
    core.pe.dst_mode = 0xC;
    core.pe.dst_fmt = 0xC;
    core.pe.drawctl = 6; // pair1: X, R
    // S2 and S1 both at the FIFO's group: SWFIFO, then FIFO.
    let words = [
        (1u32 << 16).swap_bytes(),
        10 << 16,
        (2u32 << 16).swap_bytes(),
        20 << 16,
    ];
    core.in_fifo.extend(words);
    core.pe.cnt = 1;
    core.draw_param(0xF0, 192, 32, 32, &mut memory);
    assert_eq!((core.reg[192], core.reg[193]), (3 << 16, 30 << 16));
    assert!(core.in_fifo.is_empty());
}

#[test]
fn textures_are_sampled_through_the_masks() {
    let (mut core, mut memory, _) = machine(&[HALT], 0);
    sixteen_bit(&mut core);
    core.pe.src_base = 0x3_0000;
    core.pe.src_mode = 4 | 1 << 4; // 565, replace
    core.pe.stride = 0x0301 | 1 << 4; // source lines of 256 + 16 bytes: unused at V 0
    core.pe.src_mask = 0x0003_0003;
    for x in 0..4 {
        mem::write16(&mut memory, 0x3_0000 + 2 * x, 0x1000 * (x + 1));
    }
    core.pe.dst_mode = 0xC;
    // U = 5 wraps to 1.
    core.pe.set(pixel::Param::U, 5 << 16);
    core.pe.set(pixel::Param::V, 0);
    core.pe.set(pixel::Param::X, 0);
    core.pe.set(pixel::Param::Y, 0);
    core.pe.cnt = 1;
    core.pe.drawctl = 0;
    core.reg[160] = 0;
    core.draw_param(0xF0, 192, 160, 176, &mut memory);
    assert_eq!(mem::read16(&memory, 0x1_0000), 0x2000);
}

#[test]
fn the_rom_loader_stores_sections_and_starts_the_microcode() {
    let rom = rom_image();
    let stub: Vec<u32> = (0..6)
        .map(|k| u32::from_be_bytes(rom[0x10000 + 4 * k..][..4].try_into().unwrap()))
        .collect();
    let (mut core, mut memory, mut board) = machine(&stub, 0x3F_F800);
    run(&mut core, &mut memory, &mut board);
    assert!(core.loader.active);
    for word in [0x2000, 2, 0x1111_1111, HALT, 0x3000, 1, 7, 0, 0] {
        core.io_write(io::FIFO, word, 4, &mut memory, &mut board);
    }
    run(&mut core, &mut memory, &mut board);
    assert!(!core.loader.active);
    assert_eq!(mem::read32(&memory, 0x2000), 0x1111_1111);
    assert_eq!(mem::read32(&memory, 0x3000), 7);
    // It started at the first section, and halted at its second word.
    assert!(core.halted);
    assert_eq!(core.pc, 0x2004);
}

#[test]
fn the_palette_loader_writes_the_ramdac() {
    let mut program = PALETTE_LOADER.to_vec();
    program.extend([HALT; 2]);
    let (mut core, mut memory, mut board) = machine(&program, 0);
    core.reg[254] = 4 * PALETTE_LOADER.len() as u32;
    for word in [5 << 16 | 2, 0x0011_2233, 0x0044_5566] {
        core.io_write(io::FIFO, word, 4, &mut memory, &mut board);
    }
    run(&mut core, &mut memory, &mut board);
    assert!(core.halted);
    let rgb: Vec<u32> = board
        .writes
        .iter()
        .filter(|w| w.0 == 0xB1)
        .map(|w| w.1)
        .collect();
    assert_eq!(board.writes[0], (0xB0, 5, 1));
    assert_eq!(rgb, [0x11, 0x22, 0x33, 0x44, 0x55, 0x66]);
}
