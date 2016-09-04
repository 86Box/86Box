/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
void disc_sector_reset(int drive, int side);
void disc_sector_add(int drive, int side, uint8_t c, uint8_t h, uint8_t r, uint8_t n, int rate, uint8_t *data);
void disc_sector_readsector(int drive, int sector, int track, int side, int density, int sector_size);
void disc_sector_writesector(int drive, int sector, int track, int side, int density, int sector_size);
void disc_sector_readaddress(int drive, int sector, int side, int density);
void disc_sector_format(int drive, int sector, int side, int density, uint8_t fill);
void disc_sector_stop();
void disc_sector_poll();
void disc_sector_stop();

extern void (*disc_sector_writeback[2])(int drive);
void disc_sector_prepare_track_layout(int drive, int side, int track);

#define length_gap0	80
#define length_gap1	50
#define length_sync	12
#define length_am	4
#define length_crc	2

#define IBM
#define MFM
#ifdef IBM
#define pre_gap1	length_gap0 + length_sync + length_am
#else
#define pre_gap1	0
#endif
  
#define pre_track	pre_gap1 + length_gap1
#define pre_gap		length_sync + length_am + 4 + length_crc
#define pre_data	length_sync + length_am
#define post_gap	length_crc

extern int raw_tsize[2];
extern int gap2_size[2];
extern int gap3_size[2];
// extern int gap4_size[2];
