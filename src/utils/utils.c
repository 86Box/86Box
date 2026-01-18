#include "mds.h"

u32 freadU32(FILE *f)
{
    u8 val[4] = {0, 0, 0, 0};
    fread(val, 4, 1, f);
    return getU32(val);
}

u64 freadU64(FILE *f)
{
    u8 val[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    fread(val, 8, 1, f);
    return getU64(val);
}

void printHex(void *data, int num)
{
    u8 *m = (u8 *)data;
    while(num > 0)
    {
        printf("%02x", *m);
        m++;
        num--;
    }
    printf("\n");
}
