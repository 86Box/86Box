#include <stdio.h>
#include <stdint.h>
#include "ibm.h"
#include "disc.h"
#include "disc_fdi.h"
#include "fdi2raw.h"

static struct
{
        FILE *f;
        FDI *h;
        uint8_t track_data[2][4][256*1024];
        
        int sides;
        int tracklen[2][4];
        int trackindex[2][4];
        
        int lasttrack;
} fdi[2];

static uint8_t fdi_timing[256*1024];
#if 0
static int fdi_pos;
static int fdi_revs;

static int fdi_sector, fdi_track,   fdi_side,    fdi_drive, fdi_density, fdi_n;
static int fdi_inread, fdi_inwrite, fdi_readpos, fdi_inreadaddr;
#endif

static int fdi_pos[2];
static int fdi_revs[2];

static int fdi_sector[2], fdi_track[2],   fdi_side[2],    fdi_drive[2], fdi_density[2], fdi_n[2];
static int fdi_inread[2], fdi_inwrite[2], fdi_readpos[2], fdi_inreadaddr[2];

static uint16_t CRCTable[256];

static int pollbytesleft[2]={0, 0},pollbitsleft[2]={0, 0};

int fdi_realtrack(int drive, int track)
{
	return track;
}

static void fdi_setupcrc(uint16_t poly, uint16_t rvalue)
{
	int c = 256, bc;
	uint16_t crctemp;

	while(c--)
	{
		crctemp = c << 8;
		bc = 8;

		while(bc--)
		{
			if(crctemp & 0x8000)
			{
				crctemp = (crctemp << 1) ^ poly;
			}
			else
			{
				crctemp <<= 1;
			}
		}

		CRCTable[c] = crctemp;
	}
}

void fdi_init()
{
//        printf("FDI reset\n");
        memset(&fdi, 0, sizeof(fdi));
        fdi_setupcrc(0x1021, 0xcdb4);
}

int fdi_hole(int drive)
{
	switch (fdi2raw_get_bit_rate(fdi[drive].h))
	{
		case 1000:
			return 2;
		case 500:
			return 1;
		default:
			return 0;
	}
}

int fdi_byteperiod(int drive)
{
	switch (fdi2raw_get_bit_rate(fdi[drive].h))
	{
		case 1000:
			return 8;
		case 500:
			return 16;
		case 300:
			return 26;
		case 250:
			return 32;
		default:
			return 0;
	}
}

void fdi_load(int drive, char *fn)
{
        writeprot[drive] = fwriteprot[drive] = 1;
        fdi[drive].f = fopen(fn, "rb");
        if (!fdi[drive].f) return;
        fdi[drive].h = fdi2raw_header(fdi[drive].f);
//        if (!fdih[drive]) printf("Failed to load!\n");
        fdi[drive].lasttrack = fdi2raw_get_last_track(fdi[drive].h);
        fdi[drive].sides = fdi2raw_get_last_head(fdi[drive].h) + 1;
//        printf("Last track %i\n",fdilasttrack[drive]);
        drives[drive].seek        = fdi_seek;
        drives[drive].readsector  = fdi_readsector;
        drives[drive].writesector = fdi_writesector;
        drives[drive].readaddress = fdi_readaddress;
        drives[drive].hole        = fdi_hole;
        drives[drive].byteperiod  = fdi_byteperiod;
        drives[drive].poll        = fdi_poll;
        drives[drive].format      = fdi_format;
        drives[drive].stop        = fdi_stop;
	drives[drive].realtrack   = fdi_realtrack;
//        pclog("Loaded as FDI\n");
}

void fdi_close(int drive)
{
        if (fdi[drive].h)
                fdi2raw_header_free(fdi[drive].h);
        if (fdi[drive].f)
                fclose(fdi[drive].f);
        fdi[drive].f = NULL;
}

void fdi_seek(int drive, int track)
{
        int c;
        int density;
        
        if (!fdi[drive].f)
                return;
//        printf("Track start %i\n",track);
        if (track < 0)
                track = 0;
        if (track > fdi[drive].lasttrack)
                track = fdi[drive].lasttrack - 1;
                
        for (density = 0; density < 4; density++)
        {
                int c = fdi2raw_loadtrack(fdi[drive].h, (uint16_t *)fdi[drive].track_data[0][density],
                              (uint16_t *)fdi_timing,
                              track * fdi[drive].sides,
                              &fdi[drive].tracklen[0][density],
                              &fdi[drive].trackindex[0][density], NULL, density);
                if (!c)
                        memset(fdi[drive].track_data[0][density], 0, fdi[drive].tracklen[0][density]);

                if (fdi[drive].sides == 2)
                {
                        c = fdi2raw_loadtrack(fdi[drive].h, (uint16_t *)fdi[drive].track_data[1][density],
                                      (uint16_t *)fdi_timing,
                                      (track * fdi[drive].sides) + 1,
                                      &fdi[drive].tracklen[1][density],
                                      &fdi[drive].trackindex[1][density], NULL, density);
                        if (!c)
                                memset(fdi[drive].track_data[1][density], 0, fdi[drive].tracklen[1][density]);
                }
                else
                {
                        memset(fdi[drive].track_data[1][density], 0, 65536);
                        fdi[drive].tracklen[0][density] = fdi[drive].tracklen[1][density] = 10000;
                }
        }
}

void fdi_writeback(int drive, int track)
{
        return;
}

void fdi_readsector(int drive, int sector, int track, int side, int rate, int sector_size)
{
        fdi_revs[drive] = 0;
        fdi_sector[drive] = sector;
        fdi_track[drive]  = track;
        fdi_side[drive]   = side;
	fdi_n[drive] = sector_size;
        // fdi_drive  = drive;
        if (rate == 2)
                fdi_density[drive] = 1;
        if (rate == 0)
                fdi_density[drive] = 2;
        if (rate == 3)
                fdi_density[drive] = 3;

//        pclog("FDI Read sector %i %i %i %i %i\n",drive,side,track,sector, fdi_density);
//        if (pollbytesleft)
//                pclog("In the middle of a sector!\n");

        fdi_inread[drive]  = 1;
        fdi_inwrite[drive] = 0;
        fdi_inreadaddr[drive] = 0;
        fdi_readpos[drive] = 0;
}

void fdi_writesector(int drive, int sector, int track, int side, int rate, int sector_size)
{
        fdi_revs[drive] = 0;
        fdi_sector[drive] = sector;
        fdi_track[drive]  = track;
        fdi_side[drive]   = side;
	fdi_n[drive] = sector_size;
        // fdi_drive  = drive;
        if (rate == 2)
                fdi_density[drive] = 1;
        if (rate == 0)
                fdi_density[drive] = 2;
        if (rate == 3)
                fdi_density[drive] = 3;
//        pclog("Write sector %i %i %i %i\n",drive,side,track,sector);

        fdi_inread[drive]  = 0;
        fdi_inwrite[drive] = 1;
        fdi_inreadaddr[drive] = 0;
        fdi_readpos[drive] = 0;
}

void fdi_readaddress(int drive, int track, int side, int rate)
{
        fdi_revs[drive] = 0;
        fdi_track[drive] = track;
        fdi_side[drive]  = side;
        // fdi_drive = drive;
        if (rate == 2)
                fdi_density[drive] = 1;
        if (rate == 0)
                fdi_density[drive] = 2;
        if (rate == 3)
                fdi_density[drive] = 3;
//        pclog("Read address %i %i %i %i %i %p\n",drive,side,track, rate, fdi_density, &fdi_inreadaddr);

        fdi_inread[drive]  = 0;
        fdi_inwrite[drive] = 0;
        fdi_inreadaddr[drive] = 1;
        fdi_readpos[drive]    = 0;
}

void fdi_format(int drive, int track, int side, int rate, uint8_t fill)
{
        fdi_revs[drive] = 0;
        fdi_track[drive] = track;
        fdi_side[drive]  = side;
        // fdi_drive = drive;
        if (rate == 2)
                fdi_density[drive] = 1;
        if (rate == 0)
                fdi_density[drive] = 2;
        if (rate == 3)
                fdi_density[drive] = 3;
//        pclog("Format %i %i %i\n",drive,side,track);

        fdi_inread[drive]  = 0;
        fdi_inwrite[drive] = 1;
        fdi_inreadaddr[drive] = 0;
        fdi_readpos[drive] = 0;
}

static uint16_t fdi_buffer[2];
static int readidpoll[2]={0, 0},readdatapoll[2]={0, 0},fdi_nextsector[2]={0, 0},inreadop[2]={0, 0};
static uint8_t fdi_sectordat[2][1026];
static int lastfdidat[2][2],sectorcrc[2][2];
static int sectorsize[2],fdc_sectorsize[2];
static int ddidbitsleft[2]={0, 0};

static uint8_t decodefm(uint16_t dat)
{
        uint8_t temp;
        temp = 0;
        if (dat & 0x0001) temp |= 1;
        if (dat & 0x0004) temp |= 2;
        if (dat & 0x0010) temp |= 4;
        if (dat & 0x0040) temp |= 8;
        if (dat & 0x0100) temp |= 16;
        if (dat & 0x0400) temp |= 32;
        if (dat & 0x1000) temp |= 64;
        if (dat & 0x4000) temp |= 128;
        return temp;
}

void fdi_stop(int drive)
{
//        pclog("fdi_stop\n");
        fdi_inread[drive] = fdi_inwrite[drive] = fdi_inreadaddr[drive] = 0;
        fdi_nextsector[drive] = ddidbitsleft[drive] = pollbitsleft[drive] = 0;
}

static uint16_t crc[2];

static void calccrc(int drive, uint8_t byte)
{
	crc[drive] = (crc[drive] << 8) ^ CRCTable[(crc[drive] >> 8)^byte];
}

static int fdi_indextime_blank[2] = {6250 * 8, 6250 * 8};
int fdi_poll(int drive)
{
        int tempi, c;
        int bitcount;
        
        for (bitcount = 0; bitcount < 16; bitcount++)
        {
        if (fdi_pos[drive] >= fdi[drive].tracklen[fdi_side[drive]][fdi_density[drive]])
        {
                fdi_pos[drive] = 0;
                if (fdi[drive].tracklen[fdi_side[drive]][fdi_density[drive]]) 
                        fdc_indexpulse();
                else
                {
                        fdi_indextime_blank[drive]--;
                        if (!fdi_indextime_blank[drive])
                        {
                                fdi_indextime_blank[drive] = 6250 * 8;
                                fdc_indexpulse();
                        }
                }
        }
        tempi = fdi[drive].track_data[fdi_side[drive]][fdi_density[drive]][((fdi_pos[drive] >> 3) & 0xFFFF) ^ 1] & (1 << (7 - (fdi_pos[drive] & 7)));
        fdi_pos[drive]++;
        fdi_buffer[drive] <<= 1;
        fdi_buffer[drive] |= (tempi ? 1 : 0);
        if (fdi_inwrite[drive])
        {
                fdi_inwrite[drive] = 0;
                fdc_writeprotect();
                return 1;
        }
        if (!fdi_inread[drive] && !fdi_inreadaddr[drive])
                return 1;
        if (fdi_pos[drive] == fdi[drive].trackindex[fdi_side[drive]][fdi_density[drive]])
        {
                fdi_revs[drive]++;
                if (fdi_revs[drive] == 3)
                {
//                        pclog("Not found!\n");
                        fdc_notfound();
                        fdi_inread[drive] = fdi_inreadaddr[drive] = 0;
                        return 1;
                }
                if (fdi_sector[drive] == SECTOR_FIRST)
                        fdi_sector[drive] = SECTOR_NEXT;
        }
        if (pollbitsleft[drive])
        {
                pollbitsleft[drive]--;
                if (!pollbitsleft[drive])
                {
                        pollbytesleft[drive]--;
                        if (pollbytesleft[drive]) pollbitsleft[drive] = 16; /*Set up another word if we need it*/
                        if (readidpoll[drive])
                        {
                                fdi_sectordat[drive][5 - pollbytesleft[drive]] = decodefm(fdi_buffer[drive]);
                                if (fdi_inreadaddr[drive] && !fdc_sectorid)// && pollbytesleft[drive] > 1) 
                                {
//                                        rpclog("inreadaddr - %02X\n", fdi_sectordat[drive][5 - pollbytesleft][drive]);
                                        fdc_data(fdi_sectordat[drive][5 - pollbytesleft[drive]]);
                                }
                                if (!pollbytesleft[drive])
                                {
//                                        pclog("Header over %i,%i %i,%i\n", fdi_sectordat[drive][0], fdi_sectordat[drive][2], fdi_track[drive], fdi_sector[drive]);
                                        if ((fdi_sectordat[drive][0] == fdi_track[drive] && (fdi_sectordat[drive][3] == fdi_n[drive]) && (fdi_sectordat[drive][2] == fdi_sector[drive] || fdi_sector[drive] == SECTOR_NEXT)) || fdi_inreadaddr[drive])
                                        {
                                                crc[drive] = (fdi_density) ? 0xcdb4 : 0xffff;
                                                calccrc(drive, 0xFE);
                                                for (c = 0; c < 4; c++) 
                                                        calccrc(drive, fdi_sectordat[drive][c]);

                                                if ((crc[drive] >> 8) != fdi_sectordat[drive][4] || (crc[drive] & 0xFF) != fdi_sectordat[drive][5])
                                                {
//                                                        pclog("Header CRC error : %02X %02X %02X %02X\n",crc[drive]>>8,crc[drive]&0xFF,fdi_sectordat[drive][4],fdi_sectordat[drive][5]);
//                                                        dumpregs();
//                                                        exit(-1);
                                                        inreadop[drive] = 0;
                                                        if (fdi_inreadaddr[drive])
                                                        {
//                                                                rpclog("inreadaddr - %02X\n", fdi_sector[drive]);
//                                                                fdc_data(fdi_sector[drive]);
                                                                if (fdc_sectorid)
                                                                   fdc_sectorid(fdi_sectordat[drive][0], fdi_sectordat[drive][1], fdi_sectordat[drive][2], fdi_sectordat[drive][3], fdi_sectordat[drive][4], fdi_sectordat[drive][5]);
                                                                else
                                                                   fdc_finishread(drive);
                                                        }
                                                        else             fdc_headercrcerror();
                                                        return 1;
                                                }
//                                                pclog("Sector %i,%i %i,%i\n", fdi_sectordat[drive][0], fdi_sectordat[drive][2], fdi_track[drive], fdi_sector[drive]);
                                                if (fdi_sectordat[drive][0] == fdi_track[drive] && (fdi_sectordat[drive][2] == fdi_sector[drive] || fdi_sector[drive] == SECTOR_NEXT) && fdi_inread[drive] && !fdi_inreadaddr[drive])
                                                {
                                                        fdi_nextsector[drive] = 1;
                                                        readidpoll[drive] = 0;
                                                        sectorsize[drive] = (1 << (fdi_sectordat[drive][3] + 7)) + 2;
                                                        fdc_sectorsize[drive] = fdi_sectordat[drive][3];
                                                }
                                                if (fdi_inreadaddr[drive])
                                                {
                                                        if (fdc_sectorid)
                                                           fdc_sectorid(fdi_sectordat[drive][0], fdi_sectordat[drive][1], fdi_sectordat[drive][2], fdi_sectordat[drive][3], fdi_sectordat[drive][4], fdi_sectordat[drive][5]);
                                                        else
                                                           fdc_finishread(drive);
                                                        fdi_inreadaddr[drive] = 0;
                                                }
                                        }
                                }
                        }
                        if (readdatapoll[drive])
                        {
//                                pclog("readdatapoll %i %02x\n", pollbytesleft[drive], decodefm(fdi_buffer[drive]));
                                if (pollbytesleft[drive] > 1)
                                {
                                        calccrc(drive, decodefm(fdi_buffer[drive]));
                                }
                                else
                                   sectorcrc[drive][1 - pollbytesleft[drive]] = decodefm(fdi_buffer[drive]);
                                if (!pollbytesleft[drive])
                                {
                                        fdi_inread[drive] = 0;
//#if 0
                                        if ((crc[drive] >> 8) != sectorcrc[drive][0] || (crc[drive] & 0xFF) != sectorcrc[drive][1])// || (fditrack[drive]==79 && fdisect[drive]==4 && fdc_side[drive]&1))
                                        {
//                                                pclog("Data CRC error : %02X %02X %02X %02X %i %04X %02X%02X\n",crc[drive]>>8,crc[drive]&0xFF,sectorcrc[0],sectorcrc[1],fdi_pos,crc,sectorcrc[0],sectorcrc[1]);
                                                inreadop[drive] = 0;
                                                fdc_data(decodefm(lastfdidat[drive][1]));
                                                fdc_finishread(drive);
                                                fdc_datacrcerror();
                                                readdatapoll[drive] = 0;
                                                return 1;
                                        }
//#endif
//                                        pclog("End of FDI read %02X %02X %02X %02X\n",crc[drive]>>8,crc[drive]&0xFF,sectorcrc[0],sectorcrc[1]);
                                        fdc_data(decodefm(lastfdidat[drive][1]));
                                        fdc_finishread(drive);
                                }
                                else if (lastfdidat[drive][1] != 0)
                                   fdc_data(decodefm(lastfdidat[drive][1]));
                                lastfdidat[drive][1] = lastfdidat[drive][0];
                                lastfdidat[drive][0] = fdi_buffer[drive];
                                if (!pollbytesleft[drive])
                                   readdatapoll[drive] = 0;
                        }
                }
        }
        if (fdi_buffer[drive] == 0x4489 && fdi_density[drive])
        {
//                rpclog("Found sync\n");
                ddidbitsleft[drive] = 17;
        }

        if (fdi_buffer[drive] == 0xF57E && !fdi_density[drive])
        {
                pollbytesleft[drive] = 6;
                pollbitsleft[drive]  = 16;
                readidpoll[drive]    = 1;
        }
        if ((fdi_buffer[drive] == 0xF56F || fdi_buffer[drive] == 0xF56A) && !fdi_density[drive])
        {
                if (fdi_nextsector[drive])
                {
                        pollbytesleft[drive]  = sectorsize[drive];
                        pollbitsleft[drive]   = 16;
                        readdatapoll[drive]   = 1;
                        fdi_nextsector[drive] = 0;
                        crc[drive] = 0xffff;
                        if (fdi_buffer[drive] == 0xF56A) calccrc(drive, 0xF8);
                        else                      calccrc(drive, 0xFB);
                        lastfdidat[drive][0] = lastfdidat[drive][1] = 0;
                }
        }
        if (ddidbitsleft[drive])
        {
                ddidbitsleft[drive]--;
                if (!ddidbitsleft[drive] && !readdatapoll[drive])
                {
//                        printf("ID bits over %04X %02X %i\n",fdibuffer[drive],decodefm(fdibuffer[drive]),fdipos[drive]);
                        if (decodefm(fdi_buffer[drive]) == 0xFE)
                        {
//                                printf("Sector header %i %i\n", fdi_inread[drive], fdi_inreadaddr[drive]);
                                pollbytesleft[drive] = 6;
                                pollbitsleft[drive]  = 16;
                                readidpoll[drive]    = 1;
                        }
                        else if (decodefm(fdi_buffer[drive]) == 0xFB)
                        {
//                                printf("Data header %i %i\n", fdi_inread[drive], fdi_inreadaddr[drive]);
                                if (fdi_nextsector[drive])
                                {
                                        pollbytesleft[drive]  = sectorsize[drive];
                                        pollbitsleft[drive]   = 16;
                                        readdatapoll[drive]   = 1;
                                        fdi_nextsector[drive] = 0;
                                        crc[drive] = 0xcdb4;
                                        if (fdi_buffer[drive] == 0xF56A) calccrc(drive, 0xF8);
                                        else                      calccrc(drive, 0xFB);
                                        lastfdidat[drive][0] = lastfdidat[drive][1] = 0;
                                }
                        }
                }
        }
        }
}
