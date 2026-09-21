/*
 * 86Box D88/D77 sector-image support.
 *
 * D88 is a track/sector container widely used by Japanese PC emulators.
 * The loader feeds its sector records into 86Box's D86F floppy core so that
 * the real FDC still sees IDAM/DAM/CRC timing rather than a BIOS-level shim.
 */
#ifndef EMU_FDD_D88_H
#define EMU_FDD_D88_H

#ifdef __cplusplus
extern "C" {
#endif

void d88_load(int drive, char *fn);
void d88_close(int drive);

#ifdef __cplusplus
}
#endif

#endif /* EMU_FDD_D88_H */
