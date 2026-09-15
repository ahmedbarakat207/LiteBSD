#include "include/vfs.h"
#include "include/heap.h"
#include "include/sched.h"
#include "include/time.h"

struct vfs_node {
    char path[256];
    char *data;
    unsigned int size;
    unsigned int capacity;
    unsigned int mode;
    unsigned int ino;
    int refs;
    // timestamps: seconds (+nanoseconds) since boot, from the 100Hz PIT.
    // wall-clock accuracy arrives with an RTC driver; until then the clock
    // reads boot time, but ordering and explicit utimens values are exact.
    unsigned int atime;
    unsigned int atime_nsec;
    unsigned int mtime;
    unsigned int mtime_nsec;
    unsigned int ctime;
    unsigned int ctime_nsec;
    // shared data blob: NULL = private. Otherwise points at a kmalloc'd
    // refcount shared with every node holding the same bytes (hardlinks).
    int *data_refs;
    struct vfs_node *next;
};

static struct vfs_node root = {
    "/", NULL, 0, 0, S_IFDIR, 1, 1, 0, 0, 0, 0, 0, 0, NULL, NULL
};
static struct vfs_node *nodes; // can you send me nodes?
static unsigned int next_ino = 2;

// seconds (+nanoseconds, 10ms granularity) since boot, from the PIT.
static void vfs_now(unsigned int *sec, unsigned int *nsec){
    unsigned long ticks = timer_get_ticks();
    if (sec) *sec = (unsigned int)(ticks / 100);
    if (nsec) *nsec = (unsigned int)((ticks % 100) * 10000000UL);
}

static void stamp_create(struct vfs_node *node){
    unsigned int s, ns;
    vfs_now(&s, &ns);
    node->atime = s; node->atime_nsec = ns;
    node->mtime = s; node->mtime_nsec = ns;
    node->ctime = s; node->ctime_nsec = ns;
}

// forward declarations (defined further below)
static void path_parent_dir(const char *path, char *out, unsigned int max_len);
static struct vfs_node *find_node(const char *path);
static void bump_mtime(struct vfs_node *node);

// best-effort parent dir mtime/ctime bump (create/remove/rename).
static void bump_parent(const char *resolved){
    char parent[256];
    path_parent_dir(resolved, parent, sizeof(parent));
    struct vfs_node *p = find_node(parent);
    if (p) {
        unsigned int s, ns;
        vfs_now(&s, &ns);
        p->mtime = s; p->mtime_nsec = ns;
        p->ctime = s; p->ctime_nsec = ns;
    }
}

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

static int is_symlink_node(struct vfs_node *n){
    if (!n) return 0;
    return (n->mode & 0xF000) == (unsigned int)S_IFLNK;
}

static int is_dir_node(struct vfs_node *n){
    if (!n) return 0;
    return (n->mode & S_IFDIR) != 0 && !is_symlink_node(n);
}

static void path_parent_dir(const char *path, char *out, unsigned int max_len){
    if (!out || max_len < 2) return;
    unsigned int len = 0;
    while (path[len] && len < max_len - 1) len++;
    // strip trailing slash (except root)
    while (len > 1 && path[len - 1] == '/') len--;
    int last = -1;
    for (unsigned int i = 0; i < len; i++) {
        if (path[i] == '/') last = (int)i;
    }
    if (last <= 0) {
        out[0] = '/';
        out[1] = '\0';
        return;
    }
    unsigned int n = (unsigned int)last;
    if (n >= max_len) n = max_len - 1;
    for (unsigned int i = 0; i < n; i++) out[i] = path[i];
    out[n] = '\0';
}

// follow symlinks up to 8 deep. in/out is a resolved absolute path (256 bytes).
// returns 0 on success, -1 on loop or bad target.
static int follow_symlinks(char *resolved){
    for (int depth = 0; depth < 8; depth++) {
        struct vfs_node *n = find_node(resolved);
        if (!n || !is_symlink_node(n)) return 0;
        if (!n->data) return -1;
        char target[256];
        unsigned int i = 0;
        while (i < sizeof(target) - 1 && n->data[i]) {
            target[i] = n->data[i];
            i++;
        }
        target[i] = '\0';
        if (target[0] == '\0') return -1;
        char next[256];
        if (target[0] == '/') {
            vfs_resolve_path("/", target, next, sizeof(next));
        } else {
            char parent[256];
            path_parent_dir(resolved, parent, sizeof(parent));
            // join parent + "/" + target then normalize
            char joined[512];
            unsigned int j = 0;
            unsigned int k = 0;
            while (parent[k] && j < sizeof(joined) - 1) joined[j++] = parent[k++];
            if (j == 0 || joined[j-1] != '/') {
                if (j < sizeof(joined) - 1) joined[j++] = '/';
            }
            k = 0;
            while (target[k] && j < sizeof(joined) - 1) joined[j++] = target[k++];
            joined[j] = '\0';
            vfs_resolve_path("/", joined, next, sizeof(next));
        }
        for (unsigned int t = 0; t < 256; t++) {
            resolved[t] = next[t];
            if (next[t] == '\0') break;
        }
    }
    return -1;
}

// drop our hold on a shared data blob, freeing it for the last user
static void data_release(struct vfs_node *node){
    if (!node || !node->data_refs) return;
    (*node->data_refs)--;
    if (*node->data_refs <= 0) {
        if (node->data) kfree(node->data);
        kfree(node->data_refs);
    }
    node->data = NULL;
    node->data_refs = NULL;
}

// copy-on-write: give this node a private data blob before mutating.
// shared empty blobs just detach (nothing to copy).
static int data_cow(struct vfs_node *node){
    if (!node) return -1;
    if (!node->data_refs || *node->data_refs <= 1) return 0;
    (*node->data_refs)--;
    char *data = NULL;
    if (node->capacity > 0) {
        data = (char*)kmalloc(node->capacity);
        if (!data) return -1;
        for (unsigned int i = 0; i < node->size; i++) data[i] = node->data[i];
    }
    int *refs = (int*)kmalloc(sizeof(int));
    if (!refs) {
        if (data) kfree(data);
        return -1;
    }
    *refs = 1;
    node->data = data;
    node->data_refs = refs;
    return 0;
}

// take shared ownership of target's data blob (target must be a file)
static int data_share(struct vfs_node *node, struct vfs_node *target){
    if (!node || !target || (target->mode & S_IFDIR)) return -1;
    if (!target->data_refs) {
        target->data_refs = (int*)kmalloc(sizeof(int));
        if (!target->data_refs) return -1;
        *target->data_refs = 1;
    }
    (*target->data_refs)++;
    node->data = target->data;
    node->size = target->size;
    node->capacity = target->capacity;
    node->mode = target->mode;
    node->data_refs = target->data_refs;
    return 0;
}

static void fill_stat(struct vfs_node *node, struct stat *st){
    // zero the whole 64-byte libc-compatible struct (most middle words read
    // as 0: single-user system, no device numbers, no block accounting)
    for (unsigned int i = 0; i < sizeof(struct stat); i++) ((char*)st)[i] = 0;
    st->st_size = node->size;
    st->st_mode = node->mode;
    st->st_ino = node->ino;
    st->_reserved[1] = 1; // nlink: every node has at least itself
    st->st_atim_sec = node->atime;
    st->st_atim_nsec = node->atime_nsec;
    st->st_mtim_sec = node->mtime;
    st->st_mtim_nsec = node->mtime_nsec;
    st->st_ctim_sec = node->ctime;
    st->st_ctim_nsec = node->ctime_nsec;
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
    // follow symlinks on open (like Linux without O_NOFOLLOW)
    if (node && is_symlink_node(node)) {
        char followed[256];
        for (unsigned int i = 0; i < 256; i++) {
            followed[i] = resolved[i];
            if (resolved[i] == '\0') break;
        }
        if (follow_symlinks(followed) == 0) {
            struct vfs_node *t = find_node(followed);
            if (t) node = t;
            else {
                // dangling symlink: only allow if creating (O_CREAT handled below for the link path itself)
                // for open without O_CREAT, fail like ENOENT
                if (!(flags & 0x40)) return NULL;
                // fall through to create at the link path below? No - create at resolved link path.
                // keep node as the symlink itself so caller gets the link; higher layers decide.
            }
        } else {
            return NULL;
        }
    }
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
    node->data_refs = NULL;
    stamp_create(node);
    bump_parent(resolved);
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
    if (is_symlink_node(node)) return -1;
    // reads bump atime even on EOF, like Linux
    vfs_now(&node->atime, &node->atime_nsec);
    // /dev/null -> EOF, /dev/zero -> NUL bytes
    if (strings_equal(node->path, "/dev/null")) return 0;
    if (strings_equal(node->path, "/dev/zero")) {
        for (unsigned int i = 0; i < count; i++) ((char*)buffer)[i] = 0;
        return (int)count;
    }
    if (offset >= node->size) return 0;
    unsigned int available = node->size - offset;
    if (count > available) count = available;
    for (unsigned int i = 0; i < count; i++) ((char*)buffer)[i] = node->data[offset + i];
    return (int)count;
}

int vfs_write(struct vfs_node *node, unsigned int offset, const void *buffer, unsigned int count){
    if (!node || !buffer || (node->mode & S_IFDIR)) return -1;
    if (is_symlink_node(node)) return -1;
    // /dev/null and /dev/zero discard writes
    if (strings_equal(node->path, "/dev/null")) return (int)count;
    if (strings_equal(node->path, "/dev/zero")) return (int)count;
    if (data_cow(node) != 0) return -1;
    if (data_cow(node) != 0) return -1;
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
    bump_mtime(node);
    return (int)count;
}

int vfs_stat(const char *path, struct stat *st){
    if (!path || !st) return -1;
    task_t *task = scheduler_current_task();
    const char *cwd = (task && task->cwd[0]) ? task->cwd : "/";
    char resolved[256];
    vfs_resolve_path(cwd, path, resolved, sizeof(resolved));
    if (follow_symlinks(resolved) != 0) return -1;

    struct vfs_node *node = find_node(resolved);
    if (!node) return -1;
    fill_stat(node, st);
    // /dev/null and /dev/zero report as char devices for isatty-style checks
    if (strings_equal(node->path, "/dev/null") || strings_equal(node->path, "/dev/zero")) {
        st->st_size = 0;
    }
    return 0;
}

int vfs_fstat(struct vfs_node *node, struct stat *st){
    if (!node || !st) return -1;
    fill_stat(node, st);
    return 0;
}

static void bump_mtime(struct vfs_node *node){
    vfs_now(&node->mtime, &node->mtime_nsec);
    node->ctime = node->mtime;
    node->ctime_nsec = node->mtime_nsec;
}

int vfs_truncate(struct vfs_node *node, unsigned int length){
    if (!node || (node->mode & S_IFDIR) || is_symlink_node(node)) return -1;
    if (data_cow(node) != 0) return -1;
    if (length < node->size) {
        node->size = length;
        bump_mtime(node);
        return 0;
    }
    if (length == node->size) return 0;
    if (length > node->capacity) {
        unsigned int capacity = node->capacity ? node->capacity : 256;
        while (capacity < length) capacity *= 2;
        char *data = (char*)kmalloc(capacity);
        if (!data) return -1;
        for (unsigned int i = 0; i < node->size; i++) data[i] = node->data[i];
        if (node->data) kfree(node->data);
        node->data = data;
        node->capacity = capacity;
    }
    for (unsigned int i = node->size; i < length; i++) node->data[i] = 0;
    node->size = length;
    bump_mtime(node);
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
            int had_refs = (node->data_refs != NULL);
            char *data = node->data;
            data_release(node);
            if (!had_refs && data) kfree(data);
            kfree(node);
            bump_parent(resolved);
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
    if (resolved[0] != '/' || length == 0 || length >= 256) return -1;    struct vfs_node *node = (struct vfs_node*)kmalloc(sizeof(struct vfs_node));
    if (!node) return -1;
    for (int i = 0; i <= length; i++) node->path[i] = resolved[i];
    node->data = NULL;
    node->size = 0;
    node->capacity = 0;
    node->mode = S_IFDIR;
    node->ino = next_ino++;
    node->refs = 1;
    node->data_refs = NULL;
    stamp_create(node);
    bump_parent(resolved);
    node->next = nodes;
    nodes = node;
    return 0;
}

int vfs_link(const char *path, const char *target_path){
    if (!path || !target_path) return -1;
    task_t *task = scheduler_current_task();
    const char *cwd = (task && task->cwd[0]) ? task->cwd : "/";
    char resolved[256];
    char target_resolved[256];
    vfs_resolve_path(cwd, path, resolved, sizeof(resolved));
    vfs_resolve_path(cwd, target_path, target_resolved, sizeof(target_resolved));

    struct vfs_node *target = find_node(target_resolved);
    if (!target || (target->mode & S_IFDIR)) return -1;
    if (find_node(resolved)) return -1;
    int length = string_length(resolved);
    if (resolved[0] != '/' || length == 0 || length >= 256) return -1;
    struct vfs_node *node = (struct vfs_node*)kmalloc(sizeof(struct vfs_node));
    if (!node) return -1;
    for (int i = 0; i <= length; i++) node->path[i] = resolved[i];
    node->data = NULL;
    node->size = 0;
    node->capacity = 0;
    node->mode = target->mode;
    node->ino = next_ino++;
    node->refs = 1;
    node->data_refs = NULL;
    if (data_share(node, target) != 0) {
        kfree(node);
        return -1;
    }
    stamp_create(node);
    // linking bumps the target's ctime, like Linux
    vfs_now(&target->ctime, &target->ctime_nsec);
    bump_parent(resolved);
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
    if (follow_symlinks(resolved) != 0) return -1;

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

int vfs_lstat(const char *path, struct stat *st){
    if (!path || !st) return -1;
    task_t *task = scheduler_current_task();
    const char *cwd = (task && task->cwd[0]) ? task->cwd : "/";
    char resolved[256];
    vfs_resolve_path(cwd, path, resolved, sizeof(resolved));
    struct vfs_node *node = find_node(resolved);
    if (!node) return -1;
    fill_stat(node, st);
    return 0;
}

int vfs_symlink(const char *target, const char *linkpath){
    if (!target || !linkpath) return -1;
    task_t *task = scheduler_current_task();
    const char *cwd = (task && task->cwd[0]) ? task->cwd : "/";
    char resolved[256];
    char target_buf[256];
    vfs_resolve_path(cwd, linkpath, resolved, sizeof(resolved));
    if (find_node(resolved)) return -1;
    int rlen = string_length(resolved);
    if (resolved[0] != '/' || rlen == 0 || rlen >= 256) return -1;
    // copy target verbatim (may be absolute or relative)
    unsigned int tlen = 0;
    while (target[tlen] && tlen < sizeof(target_buf) - 1) {
        target_buf[tlen] = target[tlen];
        tlen++;
    }
    target_buf[tlen] = '\0';
    if (tlen == 0) return -1;
    struct vfs_node *node = (struct vfs_node*)kmalloc(sizeof(struct vfs_node));
    if (!node) return -1;
    for (int i = 0; i <= rlen; i++) node->path[i] = resolved[i];
    node->capacity = tlen + 1;
    node->data = (char*)kmalloc(node->capacity);
    if (!node->data) {
        kfree(node);
        return -1;
    }
    for (unsigned int i = 0; i <= tlen; i++) node->data[i] = target_buf[i];
    node->size = tlen;
    node->mode = S_IFLNK;
    node->ino = next_ino++;
    node->refs = 1;
    node->data_refs = NULL;
    stamp_create(node);
    bump_parent(resolved);
    node->next = nodes;
    nodes = node;
    return 0;
}

int vfs_readlink(const char *path, char *buf, unsigned int bufsize){
    if (!path || !buf || bufsize == 0) return -1;
    task_t *task = scheduler_current_task();
    const char *cwd = (task && task->cwd[0]) ? task->cwd : "/";
    char resolved[256];
    vfs_resolve_path(cwd, path, resolved, sizeof(resolved));
    struct vfs_node *node = find_node(resolved);
    if (!node || !is_symlink_node(node) || !node->data) return -1;
    unsigned int len = node->size;
    if (len > bufsize) len = bufsize;
    for (unsigned int i = 0; i < len; i++) buf[i] = node->data[i];
    return (int)len;
}

int vfs_rename(const char *oldpath, const char *newpath){
    if (!oldpath || !newpath) return -1;
    task_t *task = scheduler_current_task();
    const char *cwd = (task && task->cwd[0]) ? task->cwd : "/";
    char old_r[256];
    char new_r[256];
    vfs_resolve_path(cwd, oldpath, old_r, sizeof(old_r));
    vfs_resolve_path(cwd, newpath, new_r, sizeof(new_r));
    if (strings_equal(old_r, "/") || strings_equal(new_r, "/")) return -1;
    if (strings_equal(old_r, new_r)) return 0;
    struct vfs_node *node = find_node(old_r);
    if (!node) return -1;
    if (find_node(new_r)) return -1;
    int was_dir = is_dir_node(node) || (node->mode & S_IFDIR) != 0;
    // move the node itself
    unsigned int nl = 0;
    while (new_r[nl] && nl < sizeof(node->path) - 1) {
        node->path[nl] = new_r[nl];
        nl++;
    }
    node->path[nl] = '\0';
    vfs_now(&node->ctime, &node->ctime_nsec);
    bump_parent(old_r);
    bump_parent(new_r);
    // if renaming a dir, rewrite children prefixes
    if (was_dir) {
        unsigned int old_len = 0;
        while (old_r[old_len]) old_len++;
        struct vfs_node *n = nodes;
        while (n) {
            if (n != node) {
                unsigned int i = 0;
                int match = 1;
                for (i = 0; i < old_len; i++) {
                    if (n->path[i] != old_r[i]) { match = 0; break; }
                }
                if (match && n->path[old_len] == '/') {
                    // new_path + rest
                    char tmp[256];
                    unsigned int j = 0;
                    while (new_r[j] && j < sizeof(tmp) - 1) {
                        tmp[j] = new_r[j];
                        j++;
                    }
                    unsigned int k = old_len;
                    while (n->path[k] && j < sizeof(tmp) - 1) {
                        tmp[j++] = n->path[k++];
                    }
                    tmp[j] = '\0';
                    for (unsigned int t = 0; t <= j && t < 256; t++) n->path[t] = tmp[t];
                }
            }
            n = n->next;
        }
    }
    return 0;
}

int vfs_rmdir(const char *path){
    if (!path) return -1;
    task_t *task = scheduler_current_task();
    const char *cwd = (task && task->cwd[0]) ? task->cwd : "/";
    char resolved[256];
    vfs_resolve_path(cwd, path, resolved, sizeof(resolved));
    if (strings_equal(resolved, "/")) return -1;
    struct vfs_node *node = find_node(resolved);
    if (!node || !(node->mode & S_IFDIR) || is_symlink_node(node)) return -1;
    // must be empty: no child with prefix resolved + '/'
    unsigned int len = 0;
    while (resolved[len]) len++;
    struct vfs_node *n = nodes;
    while (n) {
        unsigned int i = 0;
        int match = 1;
        for (i = 0; i < len; i++) {
            if (n->path[i] != resolved[i]) { match = 0; break; }
        }
        if (match && n->path[len] == '/') return -1;
        n = n->next;
    }
    return vfs_unlink(path);
}

int vfs_truncate_path(const char *path, unsigned int length){
    if (!path) return -1;
    task_t *task = scheduler_current_task();
    const char *cwd = (task && task->cwd[0]) ? task->cwd : "/";
    char resolved[256];
    vfs_resolve_path(cwd, path, resolved, sizeof(resolved));
    if (follow_symlinks(resolved) != 0) return -1;
    struct vfs_node *node = find_node(resolved);
    if (!node || (node->mode & S_IFDIR) || is_symlink_node(node)) return -1;
    if (strings_equal(node->path, "/dev/null") || strings_equal(node->path, "/dev/zero")) return 0;
    return vfs_truncate(node, length);
}

int vfs_access(const char *path, int mode){
    (void)mode;
    if (!path) return -1;
    task_t *task = scheduler_current_task();
    const char *cwd = (task && task->cwd[0]) ? task->cwd : "/";
    char resolved[256];
    vfs_resolve_path(cwd, path, resolved, sizeof(resolved));
    if (follow_symlinks(resolved) != 0) return -1;
    return find_node(resolved) ? 0 : -1;
}

// times holds two (sec, nsec) pairs: atime then mtime. NULL means "now".
// each nsec may be VFS_UTIME_NOW, VFS_UTIME_OMIT, or 0..999999999.
static int apply_utimens(struct vfs_node *node, const long times[4]){
    unsigned int now_s, now_ns;
    vfs_now(&now_s, &now_ns);
    long vals[4];
    if (!times) {
        vals[0] = now_s; vals[1] = now_ns;
        vals[2] = now_s; vals[3] = now_ns;
    } else {
        for (int i = 0; i < 4; i++) vals[i] = times[i];
        for (int f = 0; f < 2; f++) {
            long ns = vals[f * 2 + 1];
            if (ns == VFS_UTIME_OMIT || ns == VFS_UTIME_NOW) continue;
            if (ns < 0 || ns > 999999999L) return -1;
            if (vals[f * 2] < 0) return -1;
        }
    }
    // atime
    if (!times || times[1] == VFS_UTIME_NOW) {
        node->atime = now_s; node->atime_nsec = now_ns;
    } else if (times[1] != VFS_UTIME_OMIT) {
        node->atime = (unsigned int)times[0];
        node->atime_nsec = (unsigned int)times[1];
    }
    // mtime
    if (!times || times[3] == VFS_UTIME_NOW) {
        node->mtime = now_s; node->mtime_nsec = now_ns;
    } else if (times[3] != VFS_UTIME_OMIT) {
        node->mtime = (unsigned int)times[2];
        node->mtime_nsec = (unsigned int)times[3];
    }
    // setting times always bumps ctime
    vfs_now(&node->ctime, &node->ctime_nsec);
    return 0;
}

int vfs_futimens(struct vfs_node *node, const long times[4]){
    if (!node) return -1;
    if (strings_equal(node->path, "/dev/null") || strings_equal(node->path, "/dev/zero")) return 0;
    return apply_utimens(node, times);
}

int vfs_utimens(const char *path, const long times[4], int flags){
    if (!path) return -1;
    if (flags != 0 && flags != VFS_AT_SYMLINK_NOFOLLOW) return -1;
    task_t *task = scheduler_current_task();
    const char *cwd = (task && task->cwd[0]) ? task->cwd : "/";
    char resolved[256];
    vfs_resolve_path(cwd, path, resolved, sizeof(resolved));
    struct vfs_node *node;
    if (flags == VFS_AT_SYMLINK_NOFOLLOW) {
        node = find_node(resolved);
    } else {
        if (follow_symlinks(resolved) != 0) return -1;
        node = find_node(resolved);
    }
    if (!node) return -1;
    if (strings_equal(node->path, "/dev/null") || strings_equal(node->path, "/dev/zero")) return 0;
    return apply_utimens(node, times);
}
