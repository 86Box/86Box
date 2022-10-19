#ifndef EMU_PPI_H
#define EMU_PPI_H

typedef struct PPI {
    int     s2;
    uint8_t pa, pb;
} PPI;

extern int ppispeakon;
extern PPI ppi;

extern void ppi_reset(void);

#endif /*EMU_PPI_H*/
