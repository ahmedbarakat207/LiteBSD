#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <stdint.h>

#ifndef _MODE_T_DECLARED
typedef unsigned int mode_t;
#define _MODE_T_DECLARED
#endif

struct stat {
    uint32_t st_size;
    uint32_t st_mode;
    uint32_t st_ino;
};

int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);
int mkdir(const char *path, mode_t mode);

#endif
