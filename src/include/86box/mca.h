/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 */
/**
 * @file mca.h
 * @brief Micro Channel Architecture (MCA) Bus Emulation Interface
 */
#ifndef EMU_MCA_H
#define EMU_MCA_H

/**
 * @brief Initializes the MCA bus subsystem.
 *
 * Clears all slot registrations and sets the number of available physical slots.
 *
 * @param nr_cards Total number of physical MCA slots available on the system.
 */
extern void mca_init(const uint8_t nr_cards);

/**
 * @brief Registers an MCA adapter into the first available empty slot.
 *
 * @param read   Callback for POS register reads (0x100-0x107).
 * @param write  Callback for POS register writes (0x100-0x107).
 * @param feedb  Callback to poll the Card Selected Feedback (CD SFDBK) signal.
 * @param reset  Callback for handling adapter-specific reset logic.
 * @param priv   Pointer to the adapter's private state/context.
 * @note This function iterates through all slots and assigns the adapter to
 * the first one where both read and write callbacks are NULL.
 */
extern void mca_add(uint8_t (*read)(uint16_t port, void *priv),
                    void (*write)(uint16_t port, uint8_t val, void *priv),
                    uint8_t (*feedb)(void *priv),
                    void (*reset)(void *priv),
                    void *priv);

/**
 * @brief Registers an MCA adapter into a specific hardware slot.
 *
 * @param read   Callback for POS register reads.
 * @param write  Callback for POS register writes.
 * @param feedb  Callback for CD SFDBK signal status.
 * @param reset  Callback for adapter-specific reset.
 * @param priv   Pointer to the adapter's private state/context.
 * @param slot   The specific slot index (0 to MCA_MAX_CARDS-1) to occupy.
 */
extern void mca_add_to_slot(uint8_t (*read)(uint16_t port, void *priv),
                            void (*write)(uint16_t port, uint8_t val, void *priv),
                            uint8_t (*feedb)(void *priv),
                            void (*reset)(void *priv),
                            void *priv,
                            const uint8_t slot);

/**
 * @brief Sets the currently active slot for POS (Programmable Option Select) operations.
 *
 * This is typically driven by writes to system configuration ports (e.g., Port 0x96).
 *
 * @param index The slot index to select (0-indexed).
 */
extern void mca_set_index(const uint8_t index);

/**
 * @brief Performs a POS read operation from the currently selected slot.
 *
 * @param port The I/O port address being accessed (typically 0x100-0x107).
 * @return uint8_t The register value, or 0xFF if the slot is empty/invalid.
 */
extern uint8_t mca_read(const uint16_t port);

/**
 * @brief Performs a POS read operation from a specific slot index.
 *
 * Useful for system-wide diagnostic tools or bus-mastering devices that bypass the current selection index.
 *
 * @param port  The I/O port address being accessed.
 * @param index The slot index to read from.
 * @return uint8_t The register value, or 0xFF if empty/invalid.
 */
extern uint8_t mca_read_index(const uint16_t port, const uint8_t index);

/**
 * @brief Performs a POS write operation to the currently selected slot.
 *
 * @param port The I/O port address being accessed.
 * @param val  The 8-bit value to write to the register.
 */
extern void mca_write(const uint16_t port, const uint8_t val);

/**
 * @brief Polls the "Card Selected Feedback" (CD SFDBK) status of the current slot.
 *
 * Used by the system board to verify that an adapter is responding at the 
 * currently selected POS address.
 *
 * @return uint8_t Returns 1 if the card is responding, 0 otherwise.
 */
extern uint8_t mca_feedb(void);

/**
 * @brief Returns the number of physical slots initialized on the bus.
 *
 * @return uint8_t Number of slots.
 */
extern uint8_t mca_get_nr_cards(void);

/**
 * @brief Triggers a hardware reset signal to all registered MCA adapters.
 */
extern void mca_reset(void);

/**
 * @brief Synchronizes or invalidates memory caches after DMA transfers.
 *
 * This function is called following PS/2 DMA I/O-to-Memory transfers to 
 * ensure data coherency. It prevents the CPU from reading stale data 
 * from its internal cache after the DMA controller has modified system 
 * RAM directly.
 */
/**
 * @brief Internal PS/2 cache maintenance hook.
 */
extern void ps2_cache_clean(void);

#endif /*EMU_MCA_H*/
