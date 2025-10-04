#include <86box/vid_ati_eeprom.h>

typedef struct nmc93cxx_eeprom_t {
    ati_eeprom_t dev;
    uint8_t      addrbits;
    uint16_t     size;
    char         filename[1024];
} nmc93cxx_eeprom_t;

typedef struct nmc93cxx_eeprom_params_t {
    uint16_t  nwords;
    char     *filename;
    uint16_t *default_content;
} nmc93cxx_eeprom_params_t;

/* Read from the EEPROM. */
uint16_t nmc93cxx_eeprom_read(nmc93cxx_eeprom_t *eeprom);

/* Write to the EEPROM. */
void nmc93cxx_eeprom_write(nmc93cxx_eeprom_t *eeprom, int eecs, int eesk, int eedi);

/* Get EEPROM data array. */
uint16_t *nmc93cxx_eeprom_data(nmc93cxx_eeprom_t *eeprom);

extern const device_t nmc93cxx_device;
