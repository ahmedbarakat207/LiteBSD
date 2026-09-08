#include "include/heap.h"
#include "include/tty.h"

static unsigned int heap_begin = HEAP_START;
static unsigned int heap_end = HEAP_START + HEAP_SIZE;

struct heap_block {
    unsigned int size;
    unsigned int magic;
    struct heap_block *next;
};

static struct heap_block *free_list;

static unsigned int align_size(size_t size){
    return ((unsigned int)size + 3) & ~3U;
}

void *kmalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    if (size > HEAP_SIZE - sizeof(struct heap_block)) {
        err("Allocation too large.");
        return NULL;
    }

    unsigned int requested = align_size(size);
    struct heap_block **link = &free_list;
    struct heap_block *block = free_list;

    while (block != NULL) {
        if (block->size >= requested) {
            *link = block->next;
            block->next = NULL;
            block->magic = HEAP_BLOCK_ALLOCATED;
            return (void *)(block + 1);
        }
        link = &block->next;
        block = block->next;
    }

    unsigned int total = sizeof(struct heap_block) + requested;
    if (heap_begin > heap_end - total) {
        err("Out of memory.");
    }

    block = (struct heap_block *)heap_begin;
    heap_begin += total;
    block->size = requested;
    block->magic = HEAP_BLOCK_ALLOCATED;
    block->next = NULL;
    return (void *)(block + 1);
}

void kfree(void *ptr){
    if (ptr == NULL) {
        return;
    }

    struct heap_block *block = ((struct heap_block *)ptr) - 1;
    if (block->magic != HEAP_BLOCK_ALLOCATED ||
        (unsigned int)block < HEAP_START ||
        (unsigned int)block >= heap_begin) {
        return;
    }

    struct heap_block **link = &free_list;
    while (*link != NULL && (unsigned int)*link < (unsigned int)block) {
        link = &(*link)->next;
    }

    block->magic = HEAP_BLOCK_FREE;
    block->next = *link;
    *link = block;

    if (block->next != NULL &&
        (unsigned int)(block + 1) + block->size == (unsigned int)block->next) {
        block->size += sizeof(struct heap_block) + block->next->size;
        block->next = block->next->next;
    }

    if (link != &free_list) {
        struct heap_block *previous = free_list;
        while (previous->next != block) {
            previous = previous->next;
        }
        if ((unsigned int)(previous + 1) + previous->size == (unsigned int)block) {
            previous->size += sizeof(struct heap_block) + block->size;
            previous->next = block->next;
        }
    }
}