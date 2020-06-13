#include "codegen_backend_x86_defs.h"

#define BLOCK_SIZE 0x10000
#define BLOCK_MASK 0xffff
#define BLOCK_START 0

#define HASH_SIZE 0x20000
#define HASH_MASK 0x1ffff

#define HASH(l) ((l) & 0x1ffff)

#define BLOCK_MAX 0x3c0
