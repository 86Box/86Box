#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "rom.h"
#include "ps2_nvr.h"


typedef struct ps2_nvr_t
{
        int addr;
        uint8_t ram[8192];
} ps2_nvr_t;


static uint8_t ps2_nvr_read(uint16_t port, void *p)
{
        ps2_nvr_t *nvr = (ps2_nvr_t *)p;
        
        switch (port)
        {
                case 0x74:
                return nvr->addr & 0xff;
                case 0x75:
                return nvr->addr >> 8;
                case 0x76:
                return nvr->ram[nvr->addr];
        }
        
        return 0xff;
}

static void ps2_nvr_write(uint16_t port, uint8_t val, void *p)
{
        ps2_nvr_t *nvr = (ps2_nvr_t *)p;
        
        switch (port)
        {
                case 0x74:
                nvr->addr = (nvr->addr & 0x1f00) | val;
                break;
                case 0x75:
                nvr->addr = (nvr->addr & 0xff) | ((val & 0x1f) << 8);
                break;
                case 0x76:
                nvr->ram[nvr->addr] = val;
                break;
        }
}

static void *ps2_nvr_init()
{
        ps2_nvr_t *nvr = (ps2_nvr_t *)malloc(sizeof(ps2_nvr_t));
        FILE *f = NULL;

        memset(nvr, 0, sizeof(ps2_nvr_t));
        
        io_sethandler(0x0074, 0x0003, ps2_nvr_read, NULL, NULL, ps2_nvr_write, NULL, NULL, nvr);

        switch (romset)
        {
                case ROM_IBMPS2_M80:  f = nvrfopen(L"ibmps2_m80_sec.nvr", L"rb"); break;
        }
        if (f)
        {
                fread(nvr->ram, 8192, 1, f);
                fclose(f);
        }
        else
                memset(nvr->ram, 0xFF, 8192);
        
        return nvr;
}

static void ps2_nvr_close(void *p)
{
        ps2_nvr_t *nvr = (ps2_nvr_t *)p;
        FILE *f = NULL;

        switch (romset)
        {
                case ROM_IBMPS2_M80:  f = nvrfopen(L"ibmps2_m80_sec.nvr", L"wb"); break;
        }
        if (f)
        {
                fwrite(nvr->ram, 8192, 1, f);
                fclose(f);
        }
        
        free(nvr);
}

device_t ps2_nvr_device =
{
        "PS/2 NVRRAM",
        0,
        ps2_nvr_init,
        ps2_nvr_close,
        NULL,
        NULL,
        NULL,
        NULL
};
