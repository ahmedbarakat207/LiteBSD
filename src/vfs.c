#include "include/vfs.h"
#include "include/heap.h"

struct vfs_node {
    char path[256];
    char *data;
    unsigned int size;
    unsigned int capacity;
    unsigned int mode;
    unsigned int ino;
    int refs;
    struct vfs_node *next;
};

static struct vfs_node root = {
    "/", NULL, 0, 0, S_IFDIR, 1, 1, NULL
};
static struct vfs_node *nodes; // can you send me nodes?
static unsigned int next_ino = 2;

// awful strcmp()
static int strings_equal(const char *left, const char *right){
    while (*left && *left == *right) {
        left++;
        right++;
    }
    return *left == *right;
}

// awful .length()
static int string_length(const char *value){
    int length = 0;
    while (value[length] && length < 255) length++;
    return length;
}

// where is the nu.. i mean nodes nodes yea!????
static struct vfs_node *find_node(const char *path){
    if (!path) return NULL;
    if (strings_equal(path, "/") || strings_equal(path, ".")) return &root;
    if (path[0] == '.' && path[1] == '/') path++;
    struct vfs_node *node = nodes;
    while (node) {
        if (strings_equal(node->path, path)) return node;
        node = node->next;
    }
    return NULL;
}

struct vfs_node *vfs_open(const char *path, int flags){
    if (!path) return NULL;
    if (strings_equal(path, ".")) path = "/";
    if (path[0] == '.' && path[1] == '/') path++;
    if (path[0] != '/') return NULL;
    struct vfs_node *node = find_node(path);
    if (node) {
        node->refs++;
        return node;
    }
    if (!(flags & 0x40)) return NULL;
    int length = string_length(path);
    if (length == 0 || length >= 256) return NULL;
    node = (struct vfs_node*)kmalloc(sizeof(struct vfs_node));
    if (!node) return NULL;
    for (int i = 0; i <= length; i++) node->path[i] = path[i];
    node->data = NULL;
    node->size = 0;
    node->capacity = 0;
    node->mode = S_IFREG;
    node->ino = next_ino++;
    node->refs = 1;
    node->next = nodes;
    nodes = node;
    return node;
}

int vfs_close(struct vfs_node *node){
    if (!node) return -1;
    if (node != &root && node->refs > 0) node->refs--;
    return 0;
}

// to read all your corn in ascii
int vfs_read(struct vfs_node *node, unsigned int offset, void *buffer, unsigned int count){
    if (!node || !buffer || (node->mode & S_IFDIR)) return -1;
    if (offset >= node->size) return 0;
    unsigned int available = node->size - offset;
    if (count > available) count = available;
    for (unsigned int i = 0; i < count; i++) ((char*)buffer)[i] = node->data[offset + i];
    return (int)count;
}

int vfs_write(struct vfs_node *node, unsigned int offset, const void *buffer, unsigned int count){
    if (!node || !buffer || (node->mode & S_IFDIR)) return -1;
    unsigned int required = offset + count;
    if (required < offset) return -1;
    if (required > node->capacity) {
        unsigned int capacity = node->capacity ? node->capacity : 256;
        while (capacity < required) capacity *= 2;
        char *data = (char*)kmalloc(capacity);
        if (!data) return -1;
        for (unsigned int i = 0; i < node->size; i++) data[i] = node->data[i];
        if (node->data) kfree(node->data);
        node->data = data;
        node->capacity = capacity;
    }
    for (unsigned int i = 0; i < count; i++) node->data[offset + i] = ((const char*)buffer)[i];
    if (required > node->size) node->size = required;
    return (int)count;
}

int vfs_stat(const char *path, struct stat *st){
    struct vfs_node *node = path ? find_node(path) : NULL;
    if (!node || !st) return -1;
    st->st_size = node->size;
    st->st_mode = node->mode;
    st->st_ino = node->ino;
    return 0;
}

int vfs_fstat(struct vfs_node *node, struct stat *st){
    if (!node || !st) return -1;
    st->st_size = node->size;
    st->st_mode = node->mode;
    st->st_ino = node->ino;
    return 0;
}

int vfs_unlink(const char *path){
    if (!path || strings_equal(path, "/")) return -1;
    struct vfs_node **link = &nodes;
    while (*link) {
        if (strings_equal((*link)->path, path)) {
            struct vfs_node *node = *link;
            *link = node->next;
            if (node->data) kfree(node->data);
            kfree(node);
            return 0;
        }
        link = &(*link)->next;
    }
    return -1;
}

int vfs_mkdir(const char *path){
    if (!path || find_node(path)) return -1;
    int length = string_length(path);
    if (path[0] != '/' || length == 0 || length >= 256) return -1;
    struct vfs_node *node = (struct vfs_node*)kmalloc(sizeof(struct vfs_node));
    if (!node) return -1;
    for (int i = 0; i <= length; i++) node->path[i] = path[i];
    node->data = NULL;
    node->size = 0;
    node->capacity = 0;
    node->mode = S_IFDIR;
    node->ino = next_ino++;
    node->refs = 1;
    node->next = nodes;
    nodes = node;
    return 0;
}

int vfs_chdir(const char *path){
    struct vfs_node *node = path ? find_node(path) : NULL;
    return node && (node->mode & S_IFDIR) ? 0 : -1;
}

int vfs_getcwd(char *buffer, unsigned int size){
    if (!buffer || size < 2) return -1;
    buffer[0] = '/';
    buffer[1] = '\0';
    return 1;
}
