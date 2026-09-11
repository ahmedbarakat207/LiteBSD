#include "include/initrd.h"
#include "include/vfs.h"
#include "include/tty.h"
#include "include/heap.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} tar_header_t;

static unsigned int oct_to_int(const char *s, size_t size) {
    unsigned int n = 0;
    for (size_t i = 0; i < size && s[i]; i++) {
        if (s[i] >= '0' && s[i] <= '7') {
            n = n * 8 + (s[i] - '0');
        }
    }
    return n;
}

static int normalize_path(const char *raw, char *out, size_t max_len, int is_dir) {
    if (!raw || !out || max_len < 2) return -1;

    // skip "./" or "."
    if (raw[0] == '.' && raw[1] == '/') raw += 2;
    else if (raw[0] == '.' && raw[1] == '\0') return -1;

    // skip "/"
    while (*raw == '/') raw++;

    if (*raw == '\0') return -1;

    size_t idx = 0;
    out[idx++] = '/';

    while (*raw && idx < max_len - 1) {
        out[idx++] = *raw++;
    }

    // strip trailing slash
    if (is_dir && idx > 1 && out[idx - 1] == '/') {
        idx--;
    }

    out[idx] = '\0';
    return 0;
}

static void ensure_parent_dirs(const char *path) {
    char temp[256];
    size_t i = 0;
    while (path[i] && i < sizeof(temp) - 1) {
        temp[i] = path[i];
        if (i > 0 && path[i] == '/') {
            temp[i] = '\0';
            vfs_mkdir(temp);
            temp[i] = '/';
        }
        i++;
    }
}

void initrd_load(uint32_t start, uint32_t end) {
    println("[INITRD] Loading initrd...", VGA_COLOR_WHITE);

    if (start >= end) {
        println("[INITRD] Invalid initrd memory range.", VGA_COLOR_RED);
        return;
    }

    uint32_t addr = start;
    unsigned int files_loaded = 0;
    unsigned int dirs_loaded = 0;
    unsigned int shared_bytes = 0;

    while (addr + 512 <= end) {
        tar_header_t *header = (tar_header_t*)addr;

        // end of archive
        if (header->name[0] == '\0') {
            break;
        }

        // a lot of magic is going on here
        if (header->magic[0] == 'u' && header->magic[1] == 's' &&
            header->magic[2] == 't' && header->magic[3] == 'a' &&
            header->magic[4] == 'r') {
                // c'mon do smth
        }

        unsigned int size = oct_to_int(header->size, 11);
        char type = header->typeflag;
        int is_dir = (type == '5');

        char path[256];
        if (normalize_path(header->name, path, sizeof(path), is_dir) == 0) {
            ensure_parent_dirs(path);

            if (is_dir) {
                if (vfs_mkdir(path) == 0) {
                    dirs_loaded++;
                    print("[INITRD] Created dir: ", VGA_COLOR_WHITE);
                    println(path, VGA_COLOR_LIGHT_CYAN);
                }
            } else if (type == '0' || type == '\0') {
                struct vfs_node *node = vfs_open(path, 0x40 | 0x02);
                if (node) {
                    if (size > 0 && (addr + 512 + size <= end)) {
                        vfs_write(node, 0, (const void*)(addr + 512), size);
                    }
                    vfs_close(node);
                    files_loaded++;
                    print("[INITRD] Added file: ", VGA_COLOR_WHITE);
                    println(path, VGA_COLOR_WHITE);
                } else {
                    print("[INITRD] Failed to create file: ", VGA_COLOR_RED);
                    println(path, VGA_COLOR_RED);
                }
            } else if (type == '1' || type == '2') {
                const char *lname = header->linkname;
                char target_path[256];
                if (lname[0] == '.' && lname[1] == '/') {
                    // strip "./" prefix 
                    target_path[0] = '/';
                    size_t li = 0;
                    while (lname[2 + li] && li < sizeof(target_path) - 2) {
                        target_path[1 + li] = lname[2 + li];
                        li++;
                    }
                    target_path[1 + li] = '\0';
                } else if (lname[0] != '/') {
                    target_path[0] = '/';
                    size_t li = 0;
                    while (lname[li] && li < sizeof(target_path) - 2) {
                        target_path[1 + li] = lname[li];
                        li++;
                    }
                    target_path[1 + li] = '\0';
                } else {
                    size_t li = 0;
                    while (lname[li] && li < sizeof(target_path) - 1) {
                        target_path[li] = lname[li];
                        li++;
                    }
                    target_path[li] = '\0';
                }

                struct vfs_node *target = vfs_find_node(target_path);
                if (target && vfs_link(path, target_path) == 0) {
                    struct stat tst;
                    if (vfs_fstat(target, &tst) == 0) shared_bytes += tst.st_size;
                    files_loaded++;
                    print("[INITRD] Linked file: ", VGA_COLOR_WHITE);
                    println(path, VGA_COLOR_WHITE);
                } else {
                    print("[INITRD] Link target not found: ", VGA_COLOR_YELLOW);
                    println(target_path, VGA_COLOR_YELLOW);
                }
            }
        }

        uint32_t data_blocks = (size + 511) / 512;
        addr += 512 + data_blocks * 512;
    }

    print("[INITRD] Initrd loaded: ", VGA_COLOR_GREEN);
    if (files_loaded == 0 && dirs_loaded == 0) {
        println("[INITRD] No entries found.", VGA_COLOR_YELLOW);
    } else {
        print("[INITRD] Ready. Shared ", VGA_COLOR_GREEN);
        print_dec(shared_bytes / 1024, VGA_COLOR_GREEN);
        println("K via hardlinks.", VGA_COLOR_GREEN);
    }
}