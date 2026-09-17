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
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint8_t  color_info[6];
} __attribute__((packed));

struct mb_mod {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t string;
    uint32_t reserved;
};

#define MB_INFO_MEM         (1 << 0)
#define MB_INFO_BOOTDEV     (1 << 1)
#define MB_INFO_CMDLINE     (1 << 2)
#define MB_INFO_MODS        (1 << 3)
#define MB_INFO_AOUT_SYMS   (1 << 4)
#define MB_INFO_ELF_SECS    (1 << 5)
#define MB_INFO_MMAP        (1 << 6)
#define MB_INFO_DRIVES      (1 << 7)
#define MB_INFO_CONFIG      (1 << 8)
#define MB_INFO_LOADER_NAME (1 << 9)
#define MB_INFO_APM         (1 << 10)
#define MB_INFO_VBE         (1 << 11)
#define MB_INFO_FRAMEBUFFER (1 << 12)

#define MULTIBOOT_INFO_MODS MB_INFO_MODS

typedef struct mb_info mb_info_t;
typedef struct mb_mod mb_mod_t;

#endif

