#ifndef I8080_I8080_H_
#define I8080_I8080_H_

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct i8080 {
  // memory + io interface
  uint8_t (*read_byte)(void*, uint16_t); // user function to read from memory
  void (*write_byte)(void*, uint16_t, uint8_t); // same for writing to memory
  uint8_t (*read_byte_seg)(void*, uint16_t); // user function to read from memory (Code segment)
  uint8_t (*port_in)(void*, uint8_t); // user function to read from port
  void (*port_out)(void*, uint8_t, uint8_t); // same for writing to port
  void* userdata; // user custom pointer

  unsigned long cyc; // cycle count

  uint16_t pc, sp; // program counter, stack pointer
  uint8_t a, b, c, d, e, h, l; // registers
  // flags: sign, zero, half-carry, parity, carry, interrupt flip-flop
  bool sf : 1, zf : 1, hf : 1, pf : 1, cf : 1, iff : 1;
  bool halted : 1;

  bool interrupt_pending : 1;
  uint8_t interrupt_vector;
  uint8_t interrupt_delay;
} i8080;

void i8080_init(i8080* const c);
void i8080_step(i8080* const c);
void i8080_interrupt(i8080* const c, uint8_t opcode);
void i8080_debug_output(i8080* const c, bool print_disassembly);

#endif // I8080_I8080_H_
