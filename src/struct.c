#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    uint8_t a;
    uint8_t c;
    uint32_t b;
} test1_t;

typedef struct
{
    uint32_t b;
    uint8_t c;
    uint8_t a;
} test2_t;

int main(int argc, char *argv[])
{
    printf("sizeof(test1_t) = %i\n", sizeof(test1_t));
    printf("sizeof(test2_t) = %i\n", sizeof(test2_t));

    return 0;
}
