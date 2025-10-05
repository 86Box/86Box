#ifndef EMU_MACHINE_STATUS_H
#define EMU_MACHINE_STATUS_H

typedef struct dev_status_empty_active_t {
    atomic_bool_t empty;
    atomic_bool_t active;
    atomic_bool_t write_active;
    atomic_bool_t write_prot;
} dev_status_empty_active_t;

typedef struct dev_status_active_t {
    atomic_bool_t active;
    atomic_bool_t write_active;
} dev_status_active_t;

typedef struct dev_status_empty_t {
    atomic_bool_t empty;
} dev_status_empty_t;

typedef struct machine_status_t {
    dev_status_empty_active_t fdd[FDD_NUM];
    dev_status_empty_active_t cdrom[CDROM_NUM];
    dev_status_empty_active_t rdisk[RDISK_NUM];
    dev_status_empty_active_t mo[MO_NUM];
    dev_status_empty_active_t cassette;
    dev_status_active_t       hdd[HDD_BUS_USB];
    dev_status_empty_active_t net[NET_CARD_MAX];
    dev_status_empty_t        cartridge[2];
} machine_status_t;

extern machine_status_t machine_status;

extern void machine_status_init(void);

#endif /*EMU_MACHINE_STATUS_H*/
