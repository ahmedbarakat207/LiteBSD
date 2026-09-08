#include <stdint.h>

struct vfs_node;

#define S_IFREG 0x8000
#define S_IFDIR 0x4000

struct stat {
    uint32_t st_size;
    uint32_t st_mode;
    uint32_t st_ino;
};

struct vfs_node *vfs_open(const char *path, int flags);
int vfs_close(struct vfs_node *node);
int vfs_read(struct vfs_node *node, unsigned int offset, void *buffer, unsigned int count);
int vfs_write(struct vfs_node *node, unsigned int offset, const void *buffer, unsigned int count);
int vfs_stat(const char *path, struct stat *st);
int vfs_fstat(struct vfs_node *node, struct stat *st);
int vfs_unlink(const char *path);
int vfs_mkdir(const char *path);
int vfs_chdir(const char *path);
int vfs_getcwd(char *buffer, unsigned int size);
void vfs_resolve_path(const char *cwd, const char *path, char *out, unsigned int max_len);
struct vfs_node *vfs_find_node(const char *path);
int vfs_getdents(const char *path, void *buf, unsigned int bufsize);
int vfs_getdents_by_node(struct vfs_node *node, void *buf, unsigned int bufsize);
