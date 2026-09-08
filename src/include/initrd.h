#ifndef INITRD_H
#define INITRD_H

#include <stdint.h>

void initrd_load(uint32_t start, uint32_t end);

#endif