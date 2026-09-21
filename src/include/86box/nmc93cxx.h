/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header of the emulation of the National Semiconductors NMC93Cxx EEPROMs
 *          (16 bits or 8 bits).
 *
 * Authors: Cacodemon345
 *
 *          Copyright 2023 Cacodemon345
 */
#pragma once

/* Forward declaration to hide internal device state from users. */
typedef struct nmc93cxx_eeprom_t nmc93cxx_eeprom_t;

/* EEPROM device type used to specify the size of data array. */
typedef enum nmc93cxx_eeprom_type {
    /*
     * Standard 93CX6 class of 16-bit EEPROMs.
     *
     * Type / Bits per cell / Number of cells
     */
    NMC_93C06_x16_16,
    NMC_93C46_x16_64,
    NMC_93C56_x16_128,
    NMC_93C57_x16_128,
    NMC_93C66_x16_256,
    NMC_93C76_x16_512,
    NMC_93C86_x16_1024,

   /*
    * Some manufacturers use pin 6 as an "ORG" pin which,
    * when pulled low, configures memory for 8-bit accesses.
    *
    * Type / Bits per cell / Number of cells
    */
    NMC_93C46_x8_128,
    NMC_93C56_x8_256,
    NMC_93C57_x8_256,
    NMC_93C66_x8_512,
    NMC_93C76_x8_1024,
    NMC_93C86_x8_2048,
} nmc93cxx_eeprom_type;

/* EEPROM device parameters. */
typedef struct nmc93cxx_eeprom_params_t {
    /* Device type */
    nmc93cxx_eeprom_type type;
    /* Name of EEPROM image file */
    const char *filename;
    /*
     * Optional pointer to the default data buffer.
     * The buffer size should match the size of EEPROM data array specified by nmc93cxx_eeprom_type.
     */
    const void *default_content;
} nmc93cxx_eeprom_params_t;

/* Read the state of the data output (DO) line. */
bool nmc93cxx_eeprom_read(nmc93cxx_eeprom_t *dev);

/* Set the state of the input lines. */
void nmc93cxx_eeprom_write(nmc93cxx_eeprom_t *dev, bool eecs, bool eesk, bool eedi);

/* Returns pointer to the current EEPROM data array. */
const uint16_t *nmc93cxx_eeprom_data(nmc93cxx_eeprom_t *dev);

extern const device_t nmc93cxx_device;
