#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "ibm.h"
#include "ide.h"
#include "hdd_image.h"

typedef struct
{
	FILE *file;
	uint32_t base;
	uint32_t last_sector;
	uint8_t type;
	uint8_t loaded;
} hdd_image_t;

hdd_image_t hdd_images[HDC_NUM];

static char empty_sector[512];
static char *empty_sector_1mb;

int hdd_image_do_log = 0;

void hdd_image_log(const char *format, ...)
{
#ifdef ENABLE_HDD_IMAGE_LOG
	if (hdd_image_do_log)
	{
		va_list ap;
		va_start(ap, format);
		vprintf(format, ap);
		va_end(ap);
		fflush(stdout);
	}
#endif
}

int image_is_hdi(const wchar_t *s)
{
	int len;
	wchar_t ext[5] = { 0, 0, 0, 0, 0 };
	char *ws = (char *) s;
	len = wcslen(s);
	if ((len < 4) || (s[0] == L'.'))
	{
		return 0;
	}
	memcpy(ext, ws + ((len - 4) << 1), 8);
	if (wcsicmp(ext, L".HDI") == 0)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

int image_is_hdx(const wchar_t *s, int check_signature)
{
	int len;
	FILE *f;
	uint64_t filelen;
	uint64_t signature;
	char *ws = (char *) s;
	wchar_t ext[5] = { 0, 0, 0, 0, 0 };
	len = wcslen(s);
	if ((len < 4) || (s[0] == L'.'))
	{
		return 0;
	}
	memcpy(ext, ws + ((len - 4) << 1), 8);
	if (wcsicmp(ext, L".HDX") == 0)
	{
		if (check_signature)
		{
			f = _wfopen(s, L"rb");
			if (!f)
			{
				return 0;
			}
			fseeko64(f, 0, SEEK_END);
			filelen = ftello64(f);
			fseeko64(f, 0, SEEK_SET);
			if (filelen < 44)
			{
				return 0;
			}
			fread(&signature, 1, 8, f);
			fclose(f);
			if (signature == 0xD778A82044445459ll)
			{
				return 1;
			}
			else
			{
				return 0;
			}
		}
		else
		{
			return 1;
		}
	}
	else
	{
		return 0;
	}
}

int hdd_image_load(int id)
{
	uint32_t sector_size = 512;
	uint32_t zero = 0;
	uint64_t signature = 0xD778A82044445459ll;
	uint64_t full_size = 0;
	uint64_t spt = 0, hpc = 0, tracks = 0;
	int c;
	uint64_t i = 0, s = 0, t = 0;
	wchar_t *fn = hdc[id].fn;
	int is_hdx[2] = { 0, 0 };

	memset(empty_sector, 0, sizeof(empty_sector));

	hdd_images[id].base = 0;

	if (hdd_images[id].loaded)
	{
		fclose(hdd_images[id].file);
		hdd_images[id].file = NULL;
		hdd_images[id].loaded = 0;
	}

	is_hdx[0] = image_is_hdx(fn, 0);
	is_hdx[1] = image_is_hdx(fn, 1);

	/* Try to open existing hard disk image */
	if (fn[0] == '.')
	{
		hdd_image_log("File name starts with .\n");
		memset(hdc[id].fn, 0, sizeof(hdc[id].fn));
		return 0;
	}
	hdd_images[id].file = _wfopen(fn, L"rb+");
	if (hdd_images[id].file == NULL)
	{
		/* Failed to open existing hard disk image */
		if (errno == ENOENT)
		{
			/* Failed because it does not exist,
			   so try to create new file */
			if (hdc[id].wp)
			{
				hdd_image_log("A write-protected image must exist\n");
				memset(hdc[id].fn, 0, sizeof(hdc[id].fn));
				return 0;
			}

			hdd_images[id].file = _wfopen(fn, L"wb+");
			if (hdd_images[id].file == NULL)
			{
				hdd_image_log("Unable to open image\n");
				memset(hdc[id].fn, 0, sizeof(hdc[id].fn));
				return 0;
			}
			else
			{
				if (image_is_hdi(fn))
				{
					full_size = hdc[id].spt * hdc[id].hpc * hdc[id].tracks * 512;
					hdd_images[id].base = 0x1000;
					fwrite(&zero, 1, 4, hdd_images[id].file);
					fwrite(&zero, 1, 4, hdd_images[id].file);
					fwrite(&(hdd_images[id].base), 1, 4, hdd_images[id].file);
					fwrite(&full_size, 1, 4, hdd_images[id].file);
					fwrite(&sector_size, 1, 4, hdd_images[id].file);
					fwrite(&(hdc[id].spt), 1, 4, hdd_images[id].file);
					fwrite(&(hdc[id].hpc), 1, 4, hdd_images[id].file);
					fwrite(&(hdc[id].tracks), 1, 4, hdd_images[id].file);
					for (c = 0; c < 0x3f8; c++)
					{
						fwrite(&zero, 1, 4, hdd_images[id].file);
					}
					hdd_images[id].type = 1;
				}
				else if (is_hdx[0])
				{
					full_size = hdc[id].spt * hdc[id].hpc * hdc[id].tracks * 512;
					hdd_images[id].base = 0x28;
					fwrite(&signature, 1, 8, hdd_images[id].file);
					fwrite(&full_size, 1, 8, hdd_images[id].file);
					fwrite(&sector_size, 1, 4, hdd_images[id].file);
					fwrite(&(hdc[id].spt), 1, 4, hdd_images[id].file);
					fwrite(&(hdc[id].hpc), 1, 4, hdd_images[id].file);
					fwrite(&(hdc[id].tracks), 1, 4, hdd_images[id].file);
					fwrite(&zero, 1, 4, hdd_images[id].file);
					fwrite(&zero, 1, 4, hdd_images[id].file);
					hdd_images[id].type = 2;
				}
				else
				{
					hdd_images[id].type = 0;
				}
				hdd_images[id].last_sector = 0;
			}

			s = full_size = hdc[id].spt * hdc[id].hpc * hdc[id].tracks * 512;

			goto prepare_new_hard_disk;
		}
		else
		{
			/* Failed for another reason */
			hdd_image_log("Failed for another reason\n");
			memset(hdc[id].fn, 0, sizeof(hdc[id].fn));
			return 0;
		}
	}
	else
	{
		if (image_is_hdi(fn))
		{
			fseeko64(hdd_images[id].file, 0x8, SEEK_SET);
			fread(&(hdd_images[id].base), 1, 4, hdd_images[id].file);
			fseeko64(hdd_images[id].file, 0xC, SEEK_SET);
			full_size = 0;
			fread(&full_size, 1, 4, hdd_images[id].file);
			fseeko64(hdd_images[id].file, 0x10, SEEK_SET);
			fread(&sector_size, 1, 4, hdd_images[id].file);
			if (sector_size != 512)
			{
				/* Sector size is not 512 */
				hdd_image_log("HDI: Sector size is not 512\n");
				fclose(hdd_images[id].file);
				hdd_images[id].file = NULL;
				memset(hdc[id].fn, 0, sizeof(hdc[id].fn));
				return 0;
			}
			fread(&spt, 1, 4, hdd_images[id].file);
			fread(&hpc, 1, 4, hdd_images[id].file);
			fread(&tracks, 1, 4, hdd_images[id].file);
			if (hdc[id].bus == HDD_BUS_SCSI_REMOVABLE)
			{
				if ((spt != hdc[id].spt) || (hpc != hdc[id].hpc) || (tracks != hdc[id].tracks))
				{
					hdd_image_log("HDI: Geometry mismatch\n");
					fclose(hdd_images[id].file);
					hdd_images[id].file = NULL;
					memset(hdc[id].fn, 0, sizeof(hdc[id].fn));
					return 0;
				}
			}
			hdc[id].spt = spt;
			hdc[id].hpc = hpc;
			hdc[id].tracks = tracks;
			hdd_images[id].type = 1;
		}
		else if (is_hdx[1])
		{
			hdd_images[id].base = 0x28;
			fseeko64(hdd_images[id].file, 8, SEEK_SET);
			fread(&full_size, 1, 8, hdd_images[id].file);
			fseeko64(hdd_images[id].file, 0x10, SEEK_SET);
			fread(&sector_size, 1, 4, hdd_images[id].file);
			if (sector_size != 512)
			{
				/* Sector size is not 512 */
				hdd_image_log("HDX: Sector size is not 512\n");
				fclose(hdd_images[id].file);
				hdd_images[id].file = NULL;
				memset(hdc[id].fn, 0, sizeof(hdc[id].fn));
				return 0;
			}
			fread(&spt, 1, 4, hdd_images[id].file);
			fread(&hpc, 1, 4, hdd_images[id].file);
			fread(&tracks, 1, 4, hdd_images[id].file);
			if (hdc[id].bus == HDD_BUS_SCSI_REMOVABLE)
			{
				if ((spt != hdc[id].spt) || (hpc != hdc[id].hpc) || (tracks != hdc[id].tracks))
				{
					hdd_image_log("HDX: Geometry mismatch\n");
					fclose(hdd_images[id].file);
					hdd_images[id].file = NULL;
					memset(hdc[id].fn, 0, sizeof(hdc[id].fn));
					return 0;
				}
			}
			hdc[id].spt = spt;
			hdc[id].hpc = hpc;
			hdc[id].tracks = tracks;
			fread(&(hdc[id].at_spt), 1, 4, hdd_images[id].file);
			fread(&(hdc[id].at_hpc), 1, 4, hdd_images[id].file);
			hdd_images[id].type = 2;
		}
		else
		{
			full_size = hdc[id].spt * hdc[id].hpc * hdc[id].tracks * 512;
			hdd_images[id].type = 0;
		}
	}

	fseeko64(hdd_images[id].file, 0, SEEK_END);
	if (ftello64(hdd_images[id].file) < (full_size + hdd_images[id].base))
	{
		s = (full_size + hdd_images[id].base) - ftello64(hdd_images[id].file);
prepare_new_hard_disk:
		s >>= 9;
		t = (s >> 11) << 11;
		s -= t;
		t >>= 11;

		empty_sector_1mb = (char *) malloc(1048576);
		memset(empty_sector_1mb, 0, 1048576);

		if (s > 0)
		{
			for (i = 0; i < s; i++)
			{
				fwrite(empty_sector, 1, 512, hdd_images[id].file);
			}
		}

		if (t > 0)
		{
			for (i = 0; i < t; i++)
			{
				fwrite(empty_sector_1mb, 1, 1045876, hdd_images[id].file);
			}
		}

		free(empty_sector_1mb);
	}

	hdd_images[id].last_sector = (uint32_t) (full_size >> 9) - 1;

	return 1;
	hdd_images[id].loaded = 1;
}

void hdd_image_seek(uint8_t id, uint32_t sector)
{
	uint64_t addr = sector;
	addr <<= 9;
	addr += hdd_images[id].base;

	fseeko64(hdd_images[id].file, addr, SEEK_SET);
}

void hdd_image_read(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer)
{
	count <<= 9;

	hdd_image_seek(id, sector);
	memset(buffer, 0, count);
	fread(buffer, 1, count, hdd_images[id].file);
}

uint32_t hdd_sectors(uint8_t id)
{
	fseeko64(hdd_images[id].file, 0, SEEK_END);
	return (uint32_t) (ftello64(hdd_images[id].file) >> 9);
}

int hdd_image_read_ex(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer)
{
	uint32_t transfer_sectors = count;
	uint32_t sectors = hdd_sectors(id);

	if ((sectors - sector) < transfer_sectors)
	{
		transfer_sectors = sectors - sector;
	}

	hdd_image_seek(id, sector);
	memset(buffer, 0, transfer_sectors << 9);
	fread(buffer, 1, transfer_sectors << 9, hdd_images[id].file);

	if (count != transfer_sectors)
	{
		return 1;
	}
	return 0;
}

void hdd_image_write(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer)
{
	count <<= 9;

	hdd_image_seek(id, sector);
	fwrite(buffer, 1, count, hdd_images[id].file);
}

int hdd_image_write_ex(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer)
{
	uint32_t transfer_sectors = count;
	uint32_t sectors = hdd_sectors(id);

	if ((sectors - sector) < transfer_sectors)
	{
		transfer_sectors = sectors - sector;
	}

	hdd_image_seek(id, sector);
	memset(buffer, 0, transfer_sectors << 9);
	fwrite(buffer, 1, transfer_sectors << 9, hdd_images[id].file);

	if (count != transfer_sectors)
	{
		return 1;
	}
	return 0;
}

void hdd_image_zero(uint8_t id, uint32_t sector, uint32_t count)
{
	int i = 0;
	uint8_t *b;

	b = (uint8_t *) malloc(512);
	memset(b, 0, 512);

	hdd_image_seek(id, sector);
	for (i = 0; i < count; i++)
	{
		fwrite(b, 1, 512, hdd_images[id].file);
	}

	free(b);
}

int hdd_image_zero_ex(uint8_t id, uint32_t sector, uint32_t count)
{
	int i = 0;
	uint8_t *b;

	uint32_t transfer_sectors = count;
	uint32_t sectors = hdd_sectors(id);

	if ((sectors - sector) < transfer_sectors)
	{
		transfer_sectors = sectors - sector;
	}

	b = (uint8_t *) malloc(512);
	memset(b, 0, 512);

	hdd_image_seek(id, sector);
	for (i = 0; i < transfer_sectors; i++)
	{
		fwrite(b, 1, 512, hdd_images[id].file);
	}

	if (count != transfer_sectors)
	{
		return 1;
	}
	return 0;
}

uint32_t hdd_image_get_last_sector(uint8_t id)
{
	return hdd_images[id].last_sector;
}

uint8_t hdd_image_get_type(uint8_t id)
{
	return hdd_images[id].type;
}

void hdd_image_specify(uint8_t id, uint64_t hpc, uint64_t spt)
{
	if (hdd_images[id].type == 2)
	{
		hdc[id].at_hpc = hpc;
		hdc[id].at_spt = spt;
		fseeko64(hdd_images[id].file, 0x20, SEEK_SET);
		fwrite(&(hdc[id].at_spt), 1, 4, hdd_images[id].file);
		fwrite(&(hdc[id].at_hpc), 1, 4, hdd_images[id].file);
	}
}

void hdd_image_unload(uint8_t id, int fn_preserve)
{
	if (wcslen(hdc[id].fn) == 0)
	{
		return;
	}

	if (hdd_images[id].loaded)
	{
		if (hdd_images[id].file != NULL)
		{
			fclose(hdd_images[id].file);
			hdd_images[id].file = NULL;
		}
	}

	hdd_images[id].last_sector = -1;

	memset(hdc[id].prev_fn, 0, sizeof(hdc[id].prev_fn));
	if (fn_preserve)
	{
		wcscpy(hdc[id].prev_fn, hdc[id].fn);
	}

	memset(hdc[id].fn, 0, sizeof(hdc[id].fn));
}

void hdd_image_close(uint8_t id)
{
	if (hdd_images[id].file != NULL)
	{
		fclose(hdd_images[id].file);
		hdd_images[id].file = NULL;
	}
}
