#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <stdint.h>

struct mb_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms1;
    uint32_t syms2;
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t name;
    uint32_t video_mode;
    uint32_t video_lines;
    uint32_t video_cols;
    uint32_t video_depth;
};

struct mb_mod {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t string;
    uint32_t reserved;
};

#define MB_INFO_MODS 0x8
#define MULTIBOOT_INFO_MODS MB_INFO_MODS

typedef struct mb_info mb_info_t;
typedef struct mb_mod mb_mod_t;

#endif
