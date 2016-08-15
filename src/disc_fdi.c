/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include <stdio.h>
#include <stdint.h>
#include "ibm.h"
#include "disc.h"
#include "disc_img.h"
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
static int fdi_pos;
static int fdi_revs;

static int fdi_sector, fdi_track,   fdi_side,    fdi_drive, fdi_density, fdi_n;
static int fdi_inread, fdi_inwrite, fdi_readpos, fdi_inreadaddr;

static uint16_t CRCTable[256];

static int pollbytesleft=0,pollbitsleft=0;

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
			return 32;
	}
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

void fdi_load(int drive, char *fn)
{
	char header[26];

        writeprot[drive] = fwriteprot[drive] = 1;
        fdi[drive].f = fopen(fn, "rb");
        if (!fdi[drive].f) return;

	fread(header, 1, 25, fdi[drive].f);
	fseek(fdi[drive].f, 0, SEEK_SET);
	header[25] = 0;
	if (strcmp(header, "Formatted Disk Image file") != 0)
	{
		/* This is a Japanese FDI file. */
		pclog("fdi_load(): Japanese FDI file detected, redirecting to IMG loader\n");
		fclose(fdi[drive].f);
		img_load(drive, fn);
		return;
	}

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
                        fdi[drive].tracklen[1][density] = fdi[drive].tracklen[1][density] = 10000;
                }
        }
}

void fdi_writeback(int drive, int track)
{
        return;
}

void fdi_readsector(int drive, int sector, int track, int side, int rate, int sector_size)
{
        fdi_revs = 0;
        fdi_sector = sector;
        fdi_track  = track;
        fdi_side   = side;
	fdi_n = sector_size;
        fdi_drive  = drive;
        if (rate == 2)
                fdi_density = 1;
        if (rate == 0)
                fdi_density = 2;
        if (rate == 3)
                fdi_density = 3;

//        pclog("FDI Read sector %i %i %i %i %i\n",drive,side,track,sector, fdi_density);
//        if (pollbytesleft)
//                pclog("In the middle of a sector!\n");

        fdi_inread  = 1;
        fdi_inwrite = 0;
        fdi_inreadaddr = 0;
        fdi_readpos = 0;
}

void fdi_writesector(int drive, int sector, int track, int side, int rate, int sector_size)
{
        fdi_revs = 0;
        fdi_sector = sector;
        fdi_track  = track;
        fdi_side   = side;
	fdi_n = sector_size;
        fdi_drive  = drive;
        if (rate == 2)
                fdi_density = 1;
        if (rate == 0)
                fdi_density = 2;
        if (rate == 3)
                fdi_density = 3;
//        pclog("Write sector %i %i %i %i\n",drive,side,track,sector);

        fdi_inread  = 0;
        fdi_inwrite = 1;
        fdi_inreadaddr = 0;
        fdi_readpos = 0;
}

void fdi_readaddress(int drive, int track, int side, int rate)
{
        fdi_revs = 0;
        fdi_track = track;
        fdi_side  = side;
        fdi_drive = drive;
        if (rate == 2)
                fdi_density = 1;
        if (rate == 0)
                fdi_density = 2;
        if (rate == 3)
                fdi_density = 3;
//        pclog("Read address %i %i %i %i %i %p\n",drive,side,track, rate, fdi_density, &fdi_inreadaddr);

        fdi_inread  = 0;
        fdi_inwrite = 0;
        fdi_inreadaddr = 1;
        fdi_readpos    = 0;
}

void fdi_format(int drive, int track, int side, int rate, uint8_t fill)
{
        fdi_revs = 0;
        fdi_track = track;
        fdi_side  = side;
        fdi_drive = drive;
        if (rate == 2)
                fdi_density = 1;
        if (rate == 0)
                fdi_density = 2;
        if (rate == 3)
                fdi_density = 3;
//        pclog("Format %i %i %i\n",drive,side,track);

        fdi_inread  = 0;
        fdi_inwrite = 1;
        fdi_inreadaddr = 0;
        fdi_readpos = 0;
}

static uint16_t fdi_buffer;
static int readidpoll=0,readdatapoll=0,fdi_nextsector=0,inreadop=0;
static uint8_t fdi_sectordat[1026];
static int lastfdidat[2],sectorcrc[2];
static int sectorsize,fdc_sectorsize;
static int ddidbitsleft=0;

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
        fdi_inread = fdi_inwrite = fdi_inreadaddr = 0;
        fdi_nextsector = ddidbitsleft = pollbitsleft = 0;
}

static uint16_t crc;

static void calccrc(uint8_t byte)
{
	crc = (crc << 8) ^ CRCTable[(crc >> 8)^byte];
}

static int fdi_indextime_blank = 6250 * 8;
void fdi_poll()
{
        int tempi, c;
        int bitcount;
        
        for (bitcount = 0; bitcount < 16; bitcount++)
        {
        if (fdi_pos >= fdi[fdi_drive].tracklen[fdi_side][fdi_density])
        {
                fdi_pos = 0;
                if (fdi[fdi_drive].tracklen[fdi_side][fdi_density]) 
                        fdc_indexpulse();
                else
                {
                        fdi_indextime_blank--;
                        if (!fdi_indextime_blank)
                        {
                                fdi_indextime_blank = 6250 * 8;
                                fdc_indexpulse();
                        }
                }
        }
        tempi = fdi[fdi_drive].track_data[fdi_side][fdi_density][((fdi_pos >> 3) & 0xFFFF) ^ 1] & (1 << (7 - (fdi_pos & 7)));
        fdi_pos++;
        fdi_buffer <<= 1;
        fdi_buffer |= (tempi ? 1 : 0);
        if (fdi_inwrite)
        {
                fdi_inwrite = 0;
                fdc_writeprotect();
                return;
        }
        if (!fdi_inread && !fdi_inreadaddr)
                return;
        if (fdi_pos == fdi[fdi_drive].trackindex[fdi_side][fdi_density])
        {
                fdi_revs++;
                if (fdi_revs == 3)
                {
//                        pclog("Not found!\n");
                        fdc_notfound();
                        fdi_inread = fdi_inreadaddr = 0;
                        return;
                }
                if (fdi_sector == SECTOR_FIRST)
                        fdi_sector = SECTOR_NEXT;
        }
        if (pollbitsleft)
        {
                pollbitsleft--;
                if (!pollbitsleft)
                {
                        pollbytesleft--;
                        if (pollbytesleft) pollbitsleft = 16; /*Set up another word if we need it*/
                        if (readidpoll)
                        {
                                fdi_sectordat[5 - pollbytesleft] = decodefm(fdi_buffer);
                                if (fdi_inreadaddr && !fdc_sectorid)// && pollbytesleft > 1) 
                                {
//                                        rpclog("inreadaddr - %02X\n", fdi_sectordat[5 - pollbytesleft]);
                                        fdc_data(fdi_sectordat[5 - pollbytesleft]);
                                }
                                if (!pollbytesleft)
                                {
//                                        pclog("Header over %i,%i %i,%i\n", fdi_sectordat[0], fdi_sectordat[2], fdi_track, fdi_sector);
                                        if ((fdi_sectordat[0] == fdi_track && (fdi_sectordat[3] == fdi_n) && (fdi_sectordat[2] == fdi_sector || fdi_sector == SECTOR_NEXT)) || fdi_inreadaddr)
                                        {
                                                crc = (fdi_density) ? 0xcdb4 : 0xffff;
                                                calccrc(0xFE);
                                                for (c = 0; c < 4; c++) 
                                                        calccrc(fdi_sectordat[c]);

                                                if ((crc >> 8) != fdi_sectordat[4] || (crc & 0xFF) != fdi_sectordat[5])
                                                {
//                                                        pclog("Header CRC error : %02X %02X %02X %02X\n",crc>>8,crc&0xFF,fdi_sectordat[4],fdi_sectordat[5]);
//                                                        dumpregs();
//                                                        exit(-1);
                                                        inreadop = 0;
                                                        if (fdi_inreadaddr)
                                                        {
//                                                                rpclog("inreadaddr - %02X\n", fdi_sector);
//                                                                fdc_data(fdi_sector);
                                                                if (fdc_sectorid)
                                                                   fdc_sectorid(fdi_sectordat[0], fdi_sectordat[1], fdi_sectordat[2], fdi_sectordat[3], fdi_sectordat[4], fdi_sectordat[5]);
                                                                else
                                                                   fdc_finishread();
                                                        }
                                                        else             fdc_headercrcerror();
                                                        return;
                                                }
//                                                pclog("Sector %i,%i %i,%i\n", fdi_sectordat[0], fdi_sectordat[2], fdi_track, fdi_sector);
                                                if (fdi_sectordat[0] == fdi_track && (fdi_sectordat[2] == fdi_sector || fdi_sector == SECTOR_NEXT) && fdi_inread && !fdi_inreadaddr)
                                                {
                                                        fdi_nextsector = 1;
                                                        readidpoll = 0;
                                                        sectorsize = (1 << (fdi_sectordat[3] + 7)) + 2;
                                                        fdc_sectorsize = fdi_sectordat[3];
                                                }
                                                if (fdi_inreadaddr)
                                                {
                                                        if (fdc_sectorid)
                                                           fdc_sectorid(fdi_sectordat[0], fdi_sectordat[1], fdi_sectordat[2], fdi_sectordat[3], fdi_sectordat[4], fdi_sectordat[5]);
                                                        else
                                                           fdc_finishread();
                                                        fdi_inreadaddr = 0;
                                                }
                                        }
                                }
                        }
                        if (readdatapoll)
                        {
//                                pclog("readdatapoll %i %02x\n", pollbytesleft, decodefm(fdi_buffer));
                                if (pollbytesleft > 1)
                                {
                                        calccrc(decodefm(fdi_buffer));
                                }
                                else
                                   sectorcrc[1 - pollbytesleft] = decodefm(fdi_buffer);
                                if (!pollbytesleft)
                                {
                                        fdi_inread = 0;
//#if 0
                                        if ((crc >> 8) != sectorcrc[0] || (crc & 0xFF) != sectorcrc[1])// || (fditrack==79 && fdisect==4 && fdc_side&1))
                                        {
//                                                pclog("Data CRC error : %02X %02X %02X %02X %i %04X %02X%02X\n",crc>>8,crc&0xFF,sectorcrc[0],sectorcrc[1],fdi_pos,crc,sectorcrc[0],sectorcrc[1]);
                                                inreadop = 0;
                                                fdc_data(decodefm(lastfdidat[1]));
                                                fdc_finishread();
                                                fdc_datacrcerror();
                                                readdatapoll = 0;
                                                return;
                                        }
//#endif
//                                        pclog("End of FDI read %02X %02X %02X %02X\n",crc>>8,crc&0xFF,sectorcrc[0],sectorcrc[1]);
                                        fdc_data(decodefm(lastfdidat[1]));
                                        fdc_finishread();
                                }
                                else if (lastfdidat[1] != 0)
                                   fdc_data(decodefm(lastfdidat[1]));
                                lastfdidat[1] = lastfdidat[0];
                                lastfdidat[0] = fdi_buffer;
                                if (!pollbytesleft)
                                   readdatapoll = 0;
                        }
                }
        }
        if (fdi_buffer == 0x4489 && fdi_density)
        {
//                rpclog("Found sync\n");
                ddidbitsleft = 17;
        }

        if (fdi_buffer == 0xF57E && !fdi_density)
        {
                pollbytesleft = 6;
                pollbitsleft  = 16;
                readidpoll    = 1;
        }
        if ((fdi_buffer == 0xF56F || fdi_buffer == 0xF56A) && !fdi_density)
        {
                if (fdi_nextsector)
                {
                        pollbytesleft  = sectorsize;
                        pollbitsleft   = 16;
                        readdatapoll   = 1;
                        fdi_nextsector = 0;
                        crc = 0xffff;
                        if (fdi_buffer == 0xF56A) calccrc(0xF8);
                        else                      calccrc(0xFB);
                        lastfdidat[0] = lastfdidat[1] = 0;
                }
        }
        if (ddidbitsleft)
        {
                ddidbitsleft--;
                if (!ddidbitsleft && !readdatapoll)
                {
//                        printf("ID bits over %04X %02X %i\n",fdibuffer,decodefm(fdibuffer),fdipos);
                        if (decodefm(fdi_buffer) == 0xFE)
                        {
//                                printf("Sector header %i %i\n", fdi_inread, fdi_inreadaddr);
                                pollbytesleft = 6;
                                pollbitsleft  = 16;
                                readidpoll    = 1;
                        }
                        else if (decodefm(fdi_buffer) == 0xFB)
                        {
//                                printf("Data header %i %i\n", fdi_inread, fdi_inreadaddr);
                                if (fdi_nextsector)
                                {
                                        pollbytesleft  = sectorsize;
                                        pollbitsleft   = 16;
                                        readdatapoll   = 1;
                                        fdi_nextsector = 0;
                                        crc = 0xcdb4;
                                        if (fdi_buffer == 0xF56A) calccrc(0xF8);
                                        else                      calccrc(0xFB);
                                        lastfdidat[0] = lastfdidat[1] = 0;
                                }
                        }
                }
        }
        }
}
