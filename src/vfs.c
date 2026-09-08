#include "include/vfs.h"
#include "include/heap.h"
#include "include/sched.h"

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

void vfs_resolve_path(const char *cwd, const char *path, char *out, unsigned int max_len) {
    if (!out || max_len < 2) return;
    if (!path || path[0] == '\0') {
        if (!cwd || cwd[0] == '\0') {
            out[0] = '/';
            out[1] = '\0';
        } else {
            unsigned int i = 0;
            while (cwd[i] && i < max_len - 1) {
                out[i] = cwd[i];
                i++;
            }
            out[i] = '\0';
        }
        return;
    }

    char temp[256];
    unsigned int len = 0;

    if (path[0] == '/') {
        temp[len++] = '/';
        temp[len] = '\0';
        path++;
    } else {
        if (!cwd || cwd[0] != '/') {
            temp[len++] = '/';
            temp[len] = '\0';
        } else {
            while (cwd[len] && len < sizeof(temp) - 2) {
                temp[len] = cwd[len];
                len++;
            }
            temp[len] = '\0';
        }
    }

    while (*path) {
        while (*path == '/') path++;
        if (*path == '\0') break;

        char comp[64];
        unsigned int clen = 0;
        while (*path && *path != '/' && clen < sizeof(comp) - 1) {
            comp[clen++] = *path++;
        }
        comp[clen] = '\0';

        if (clen == 1 && comp[0] == '.') {
            continue;
        } else if (clen == 2 && comp[0] == '.' && comp[1] == '.') {
            if (len > 1) {
                if (temp[len - 1] == '/') len--;
                while (len > 1 && temp[len - 1] != '/') len--;
                if (len > 1 && temp[len - 1] == '/') len--;
                temp[len] = '\0';
            }
        } else {
            if (len == 0 || temp[len - 1] != '/') {
                if (len < sizeof(temp) - 1) temp[len++] = '/';
            }
            for (unsigned int i = 0; i < clen && len < sizeof(temp) - 1; i++) {
                temp[len++] = comp[i];
            }
            temp[len] = '\0';
        }
    }

    if (len == 0) {
        temp[len++] = '/';
        temp[len] = '\0';
    }

    unsigned int i = 0;
    while (i < len && i < max_len - 1) {
        out[i] = temp[i];
        i++;
    }
    out[i] = '\0';
}
// where is the nu.. i mean nodes nodes yea!????
static struct vfs_node *find_node(const char *path){
    if (!path) return NULL;
    if (strings_equal(path, "/")) return &root;
    struct vfs_node *node = nodes;
    while (node) {
        if (strings_equal(node->path, path)) return node;
        node = node->next;
    }
    return NULL;
}

struct vfs_node *vfs_find_node(const char *path) {
    return find_node(path);
}

struct vfs_node *vfs_open(const char *path, int flags){
    if (!path) return NULL;
    task_t *task = scheduler_current_task();
    const char *cwd = (task && task->cwd[0]) ? task->cwd : "/";
    char resolved[256];
    vfs_resolve_path(cwd, path, resolved, sizeof(resolved));

    struct vfs_node *node = find_node(resolved);
    if (node) {
        node->refs++;
        return node;
    }
    if (!(flags & 0x40)) return NULL;
    int length = string_length(resolved);
    if (length == 0 || length >= 256) return NULL;
    node = (struct vfs_node*)kmalloc(sizeof(struct vfs_node));
    if (!node) return NULL;
    for (int i = 0; i <= length; i++) node->path[i] = resolved[i];
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
    if (!path || !st) return -1;
    task_t *task = scheduler_current_task();
    const char *cwd = (task && task->cwd[0]) ? task->cwd : "/";
    char resolved[256];
    vfs_resolve_path(cwd, path, resolved, sizeof(resolved));

    struct vfs_node *node = find_node(resolved);
    if (!node) return -1;
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
    if (!path) return -1;
    task_t *task = scheduler_current_task();
    const char *cwd = (task && task->cwd[0]) ? task->cwd : "/";
    char resolved[256];
    vfs_resolve_path(cwd, path, resolved, sizeof(resolved));

    if (strings_equal(resolved, "/")) return -1;
    struct vfs_node **link = &nodes;
    while (*link) {
        if (strings_equal((*link)->path, resolved)) {
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
    if (!path) return -1;
    task_t *task = scheduler_current_task();
    const char *cwd = (task && task->cwd[0]) ? task->cwd : "/";
    char resolved[256];
    vfs_resolve_path(cwd, path, resolved, sizeof(resolved));

    if (find_node(resolved)) return -1;
    int length = string_length(resolved);
    if (resolved[0] != '/' || length == 0 || length >= 256) return -1;
    struct vfs_node *node = (struct vfs_node*)kmalloc(sizeof(struct vfs_node));
    if (!node) return -1;
    for (int i = 0; i <= length; i++) node->path[i] = resolved[i];
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
    if (!path) return -1;
    task_t *task = scheduler_current_task();
    const char *cwd = (task && task->cwd[0]) ? task->cwd : "/";
    char resolved[256];
    vfs_resolve_path(cwd, path, resolved, sizeof(resolved));

    struct vfs_node *node = find_node(resolved);
    if (node && (node->mode & S_IFDIR)) {
        if (task) {
            unsigned int i = 0;
            while (resolved[i] && i < sizeof(task->cwd) - 1) {
                task->cwd[i] = resolved[i];
                i++;
            }
            task->cwd[i] = '\0';
        }
        return 0;
    }
    return -1;
}

int vfs_getcwd(char *buffer, unsigned int size){
    if (!buffer || size < 2) return -1;
    task_t *task = scheduler_current_task();
    const char *cwd = (task && task->cwd[0]) ? task->cwd : "/";
    unsigned int i = 0;
    while (cwd[i] && i < size - 1) {
        buffer[i] = cwd[i];
        i++;
    }
    buffer[i] = '\0';
    return (int)i;
}

int vfs_getdents(const char *path, void *buf, unsigned int bufsize) {
    if (!path || !buf || bufsize == 0) return -1;
    task_t *task = scheduler_current_task();
    const char *cwd = (task && task->cwd[0]) ? task->cwd : "/";
    char dir_path[256];
    vfs_resolve_path(cwd, path, dir_path, sizeof(dir_path));

    // must exist and be a directory
    struct vfs_node *dir = find_node(dir_path);
    if (!dir || !(dir->mode & S_IFDIR)) return -1;

    unsigned int dir_len = 0;
    while (dir_path[dir_len]) dir_len++;
    // remove / if not root
    if (dir_len > 1 && dir_path[dir_len - 1] == '/') {
        dir_path[--dir_len] = '\0';
    }

    unsigned int written = 0;
    char *out = (char *)buf;
    {
        unsigned int name_len = 1;
        unsigned int reclen = 4 + 4 + name_len + 1;
        if (written + reclen <= bufsize) {
            *((unsigned int*)(out + written)) = dir->ino;
            *((unsigned int*)(out + written + 4)) = reclen;
            out[written + 8] = '.';
            out[written + 9] = '\0';
            written += reclen;
        }
    }
    // ..
    {
        unsigned int name_len = 2;
        unsigned int reclen = 4 + 4 + name_len + 1;
        unsigned int parent_ino = 1; // default to root ino 
        // find parent directory
        char parent[256];
        unsigned int pi = dir_len;
        while (pi > 1 && dir_path[pi - 1] != '/') pi--;
        if (pi > 1) pi--;
        for (unsigned int i = 0; i < pi; i++) parent[i] = dir_path[i];
        parent[pi] = '\0';
        struct vfs_node *p = find_node(pi == 0 ? "/" : parent);
        if (p) parent_ino = p->ino;
        if (written + reclen <= bufsize) {
            *((unsigned int*)(out + written)) = parent_ino;
            *((unsigned int*)(out + written + 4)) = reclen;
            out[written + 8] = '.';
            out[written + 9] = '.';
            out[written + 10] = '\0';
            written += reclen;
        }
    }
    
    struct vfs_node *n = nodes;
    while (n) {
        const char *np = n->path;
        int is_child = 0;
        const char *name_start = NULL;
        if (dir_len == 1 && dir_path[0] == '/') {
            // root: any node /foo (depth 1)
            if (np[0] == '/' && np[1] != '\0') {
                name_start = np + 1; // skip leading slash 
                is_child = 1;
            }
        } else {
            int match = 1;
            for (unsigned int i = 0; i < dir_len; i++) {
                if (np[i] != dir_path[i]) { match = 0; break; }
            }
            if (match && np[dir_len] == '/') {
                name_start = np + dir_len + 1;
                is_child = 1;
            }
        }
        if (is_child && name_start) {
            int is_direct = 1;
            unsigned int name_len = 0;
            while (name_start[name_len]) {
                if (name_start[name_len] == '/') { is_direct = 0; break; }
                name_len++;
            }
            if (is_direct && name_len > 0) {
                unsigned int reclen = 4 + 4 + name_len + 1;
                if (written + reclen <= bufsize) {
                    *((unsigned int*)(out + written)) = n->ino;
                    *((unsigned int*)(out + written + 4)) = reclen;
                    for (unsigned int i = 0; i < name_len; i++) {
                        out[written + 8 + i] = name_start[i];
                    }
                    out[written + 8 + name_len] = '\0';
                    written += reclen;
                }
            }
        }
        n = n->next;
    }

    return (int)written;
}

int vfs_getdents_by_node(struct vfs_node *node, void *buf, unsigned int bufsize) {
    if (!node) return -1;
    return vfs_getdents(node->path, buf, bufsize);
}
