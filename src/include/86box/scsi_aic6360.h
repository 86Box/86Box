/* Adaptec AIC-6360 ISA SCSI adapters. */
#ifndef EMU_SCSI_AIC6360_H
#define EMU_SCSI_AIC6360_H

extern const device_t aha1520a_device;
extern const device_t sb16_scsi_device;

/* The chip alone, for a board that maps its 32 registers and
   wires its interrupt itself (the APA-1460 PC Card).  The register handlers
   take the port (its low five bits select the register) and the chip. */
extern void    *aic6360_chip_init(uint8_t bus, uint8_t porta, uint8_t portb, void (*irq_out)(void *priv, int level),
                                  void *irq_priv);
extern void     aic6360_chip_close(void *chip);
extern void     aic6360_reset(void *chip);
extern uint8_t  aic6360_read(uint16_t port, void *chip);
extern uint16_t aic6360_readw(uint16_t port, void *chip);
extern void     aic6360_write(uint16_t port, uint8_t val, void *chip);
extern void     aic6360_writew(uint16_t port, uint16_t val, void *chip);

#endif
