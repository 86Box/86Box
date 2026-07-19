#ifndef EMU_808X_MARTY_86BOX_H
#define EMU_808X_MARTY_86BOX_H

#include <stdbool.h>
#include <stdint.h>

bool m808x_86box_should_use(void);
bool m808x_86box_active(void);

void m808x_86box_reset(int hard);
void m808x_86box_exec(int32_t cycs);
void m808x_86box_refresh(void);
void m808x_86box_dma_request(unsigned wait_clocks);
void m808x_86box_dma_request_ex(unsigned wait_clocks,
                                void (*ack_callback)(void *opaque),
                                void *opaque);
bool m808x_86box_dma_try_request_ex(unsigned wait_clocks,
                                    void (*ack_callback)(void *opaque),
                                    void *opaque);
bool m808x_86box_dma_cancel_request_ex(void (*ack_callback)(void *opaque),
                                       void *opaque);
uint64_t m808x_86box_cycle_number(void);
void m808x_86box_wait(int clocks, int bus);
void m808x_86box_external_sub_cycles(int clocks);
void m808x_86box_interrupt(uint16_t vector);
void m808x_86box_iret_complete(void);

void m808x_86box_pfq_set_pos(int pos);
void m808x_86box_pfq_set_ip(uint16_t ip);
void m808x_86box_pfq_set_prefetching(int enabled);
int m808x_86box_pfq_get_pos(void);
uint16_t m808x_86box_pfq_get_ip(void);
int m808x_86box_pfq_get_prefetching(void);
int m808x_86box_pfq_get_size(void);

#ifdef M808X_86BOX_TESTING
bool m808x_86box_test_halted(void);
uint64_t m808x_86box_test_captured_wait_clocks(void);
uint64_t m808x_86box_test_tw_clocks(void);
unsigned m808x_86box_test_dma_state(void);
unsigned m808x_86box_test_dma_wait_states(void);
bool m808x_86box_test_dma_holda(void);
bool m808x_86box_test_dma_ack(void);
bool m808x_86box_test_dma_aen(void);
unsigned m808x_86box_test_t_cycle(void);
unsigned m808x_86box_test_bus_status(void);
#endif

/* Implemented in 86Box's legacy 808x translation unit so the existing 8087
 * operation tables remain available while CPU timing is supplied by the new
 * EU/BIU core. */
void m808x_86box_fpu_exec(uint8_t opcode, uint8_t modrm,
                           uint16_t ea, uint8_t segment_index);
bool m808x_86box_fpu_busy(void);

#endif
