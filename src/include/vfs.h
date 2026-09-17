#ifndef VFS_H
#define VFS_H

#include <stdint.h>

struct vfs_node;

#define S_IFCHR 0x2000
#define S_IFDIR 0x4000
#define S_IFBLK 0x6000
#define S_IFREG 0x8000
#define S_IFLNK 0xA000


// for utimens(2): only these flag bits are accepted
#define VFS_AT_SYMLINK_NOFOLLOW 0x100
#define VFS_UTIME_NOW ((long)((1UL << 30) - 1UL))
#define VFS_UTIME_OMIT ((long)((1UL << 30) - 2UL))

// layout must match libc struct stat for the fields the kernel fills:
// size/mode/ino at 0/4/8, timespec triples at 40..63. The middle words
// (dev, nlink, uid, ...) read back as zero.
struct stat {
    uint32_t st_size;
    uint32_t st_mode;
    uint32_t st_ino;
    uint32_t _reserved[7];
    uint32_t st_atim_sec;
    uint32_t st_atim_nsec;
    uint32_t st_mtim_sec;
    uint32_t st_mtim_nsec;
    uint32_t st_ctim_sec;
    uint32_t st_ctim_nsec;
};

_Static_assert(__builtin_offsetof(struct stat, st_atim_sec) == 40, "stat atime offset");
_Static_assert(__builtin_offsetof(struct stat, st_mtim_sec) == 48, "stat mtime offset");
_Static_assert(__builtin_offsetof(struct stat, st_ctim_sec) == 56, "stat ctime offset");
_Static_assert(sizeof(struct stat) == 64, "stat size");

struct vfs_node *vfs_open(const char *path, int flags);
int vfs_close(struct vfs_node *node);
int vfs_read(struct vfs_node *node, unsigned int offset, void *buffer, unsigned int count);
int vfs_write(struct vfs_node *node, unsigned int offset, const void *buffer, unsigned int count);
int vfs_stat(const char *path, struct stat *st);
int vfs_fstat(struct vfs_node *node, struct stat *st);
int vfs_truncate(struct vfs_node *node, unsigned int length);
int vfs_unlink(const char *path);
int vfs_mkdir(const char *path);
// hardlink: new path shares the target's data blob (copy-on-write)
int vfs_link(const char *path, const char *target_path);
int vfs_chdir(const char *path);
int vfs_getcwd(char *buffer, unsigned int size);
int vfs_rename(const char *oldpath, const char *newpath);
int vfs_rmdir(const char *path);
int vfs_symlink(const char *target, const char *linkpath);
int vfs_readlink(const char *path, char *buf, unsigned int bufsize);
int vfs_lstat(const char *path, struct stat *st);
int vfs_truncate_path(const char *path, unsigned int length);
int vfs_access(const char *path, int mode);
// times[2] is two (sec, nsec) pairs like struct timespec; NULL means "now".
// flags is 0 or VFS_AT_SYMLINK_NOFOLLOW.
int vfs_utimens(const char *path, const long times[4], int flags);
int vfs_futimens(struct vfs_node *node, const long times[4]);
void vfs_resolve_path(const char *cwd, const char *path, char *out, unsigned int max_len);
struct vfs_node *vfs_find_node(const char *path);
int vfs_getdents(const char *path, void *buf, unsigned int bufsize);
int vfs_getdents_by_node(struct vfs_node *node, void *buf, unsigned int bufsize);
int vfs_is_fb0(struct vfs_node *node);

#endif /* VFS_H */

