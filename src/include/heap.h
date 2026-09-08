#include <stddef.h>

#define HEAP_START 0x400000 // 4mb
#define HEAP_SIZE 0x100000 // 1mb
#define HEAP_BLOCK_ALLOCATED 0x4C425344
#define HEAP_BLOCK_FREE 0x46524545

void *kmalloc(size_t size);
void kfree(void* ptr);