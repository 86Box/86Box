/* Intel UPI-42 core used by ROM-backed keyboard controllers. */
#ifndef EMU_UPI42_H
#define EMU_UPI42_H

#include <stdint.h>

#define UPI42_ROM_8042 0x01ff07ffu

void *upi42_init(uint32_t type, uint8_t *rom);
void upi42_free(void *priv);
void upi42_reset(void *priv);
void upi42_exec(void *priv);
void upi42_cmd_write(uint16_t port, uint8_t val, void *priv);
void upi42_dbb_write(uint16_t port, uint8_t val, void *priv);
uint8_t upi42_dbb_read(uint16_t port, void *priv);
uint8_t upi42_sts_read(uint16_t port, void *priv);
void upi42_port_write(void *priv, int port, uint8_t val);
uint8_t upi42_port_output(void *priv, int port);
uint16_t upi42_pc(void *priv);
void upi42_set_test_inputs(void *priv, int t0, int t1);

#endif
