#include "include/syscall.h"
#include "include/tty.h"
#include "include/keyboard.h"
#include "include/sched.h"
#include "include/idt.h"
#include "include/heap.h"
#include "include/vfs.h"
#include "include/time.h"
#include "include/fb.h"
#include <stdint.h>


#define ELF_PT_LOAD 1

static int sys_write(uint32_t fd, uint32_t buffer, uint32_t count);
static int sys_read(uint32_t fd, uint32_t buffer, uint32_t count);
static int sys_exit(uint32_t status, uint32_t unused1, uint32_t unused2);
static int sys_getpid(uint32_t unused1, uint32_t unused2, uint32_t unused3);
static int sys_fork(uint32_t unused1, uint32_t unused2, uint32_t unused3);
static int sys_execve(uint32_t path, uint32_t argv, uint32_t envp);
static int sys_wait4(uint32_t pid, uint32_t status, uint32_t unused1);
static int sys_getppid(uint32_t unused1, uint32_t unused2, uint32_t unused3);
static int sys_brk(uint32_t addr, uint32_t unused1, uint32_t unused2);
static int sys_mmap(uint32_t length, uint32_t unused1, uint32_t unused2);
static int sys_munmap(uint32_t addr, uint32_t unused1, uint32_t unused2);
static int sys_pipe(uint32_t fds, uint32_t unused1, uint32_t unused2);
static int sys_dup(uint32_t fd, uint32_t unused1, uint32_t unused2);
static int sys_dup2(uint32_t oldfd, uint32_t newfd, uint32_t unused1);
static int sys_kill(uint32_t pid, uint32_t sig, uint32_t unused1);
static int sys_ioctl(uint32_t fd, uint32_t request, uint32_t arg);
static int sys_open(uint32_t path, uint32_t flags, uint32_t mode);
static int sys_close(uint32_t fd, uint32_t unused1, uint32_t unused2);
static int sys_lseek(uint32_t fd, uint32_t offset, uint32_t whence);
static int sys_stat(uint32_t path, uint32_t st, uint32_t unused1);
static int sys_fstat(uint32_t fd, uint32_t st, uint32_t unused1);
static int sys_unlink(uint32_t path, uint32_t unused1, uint32_t unused2);
static int sys_mkdir(uint32_t path, uint32_t unused1, uint32_t unused2);
static int sys_chdir(uint32_t path, uint32_t unused1, uint32_t unused2);
static int sys_getcwd(uint32_t buffer, uint32_t size, uint32_t unused1);
static int sys_getdents(uint32_t fd, uint32_t buf, uint32_t bufsize);
static int sys_ftruncate(uint32_t fd, uint32_t length, uint32_t unused1);
static int sys_poll(uint32_t fds, uint32_t nfds, uint32_t timeout);
static int sys_uname(uint32_t buf, uint32_t unused1, uint32_t unused2);
static int sys_rename(uint32_t oldpath, uint32_t newpath, uint32_t unused1);
static int sys_rmdir(uint32_t path, uint32_t unused1, uint32_t unused2);
static int sys_symlink(uint32_t target, uint32_t linkpath, uint32_t unused1);
static int sys_readlink(uint32_t path, uint32_t buf, uint32_t bufsize);
static int sys_lstat(uint32_t path, uint32_t st, uint32_t unused1);
static int sys_truncate(uint32_t path, uint32_t length, uint32_t unused1);
static int sys_access(uint32_t path, uint32_t mode, uint32_t unused1);
static int sys_link(uint32_t oldpath, uint32_t newpath, uint32_t unused1);
static int sys_futimens(uint32_t fd, uint32_t times, uint32_t unused1);
static int sys_utimens(uint32_t path, uint32_t times, uint32_t flags);

static int sys_execve_impl(struct interrupt_frame *frame, uint32_t path, uint32_t argv_u, uint32_t envp_u);

#define EXEC_MAX_ARGS 64
#define EXEC_MAX_ENVS 64
#define EXEC_MAX_STRLEN 1024
#define EXEC_MAX_TOTAL 8192
#define EXEC_STACK_SIZE 16384

typedef int (*syscall_func_t)(uint32_t, uint32_t, uint32_t);

static const syscall_func_t syscall_table[] = {
    NULL,                       // 0
    sys_write,                  // 1
    sys_read,                   // 2
    sys_exit,                   // 3
    sys_getpid,                 // 4
    sys_fork,                   // 5
    sys_execve,                 // 6
    sys_wait4,                  // 7
    sys_getppid,                // 8
    sys_brk,                    // 9
    sys_mmap,                   // 10
    sys_munmap,                 // 11
    sys_pipe,                   // 12
    sys_dup,                    // 13
    sys_dup2,                   // 14
    sys_kill,                   // 15
    sys_ioctl,                  // 16
    sys_open,                   // 17
    sys_close,                  // 18
    sys_lseek,                  // 19
    sys_stat,                   // 20
    sys_fstat,                  // 21
    sys_unlink,                 // 22
    sys_mkdir,                  // 23
    sys_chdir,                  // 24
    sys_getcwd,               // 25
    sys_getdents,             // 26
    sys_ftruncate,            // 27
    sys_poll,                 // 28
    sys_uname,                // 29
    sys_rename,               // 30
    sys_rmdir,                // 31
    sys_symlink,              // 32
    sys_readlink,             // 33
    sys_lstat,                // 34
    sys_truncate,             // 35
    sys_access,               // 36
    sys_link,                 // 37
    sys_futimens,             // 38
    sys_utimens,              // 39
};

#define SYSCALL_COUNT (sizeof(syscall_table)/sizeof(syscall_table[0]))

static struct interrupt_frame *current_syscall_frame;

static int syscall_range_valid(uint32_t address, uint32_t length){
    task_t *task = scheduler_current_task();
    if (!task || !task->user) return address != 0 || length == 0;
    return scheduler_user_range_valid((const void*)address, length);
}

static int syscall_string_valid(uint32_t address){
    if (!syscall_range_valid(address, 1)) return 0;
    const char *string = (const char*)address;
    for (uint32_t i = 0; i < 256; i++) {
        if (!syscall_range_valid(address + i, 1)) return 0;
        if (string[i] == '\0') return 1;
    }
    return 0;
}

static int sys_write(uint32_t fd, uint32_t buffer, uint32_t count){
    if (buffer == 0 || !syscall_range_valid(buffer, count)) return -1;
    task_t *task = scheduler_current_task();
    if (fd == 1 || fd == 2 || (task && (int)fd >= 0 && fd < MAX_FDS && task->fds[fd] && !task->fds[fd]->node && !task->fds[fd]->pipe)) {
        const char *buf = (const char *)buffer;
        for (uint32_t i = 0; i < count; i++) {
            print_char(buf[i], VGA_COLOR_WHITE);
        }
        return count;
    }

    if ((int)fd < 0 || fd >= MAX_FDS || !task->fds[fd]) {
        return -1;
    }
    struct file *f = task->fds[fd];
    if (f->pipe) {
        struct pipe *p = f->pipe;
        const char *buf = (const char *)buffer;
        uint32_t written = 0;
        while (written < count) {
            if (p->count >= p->size) {
                asm volatile("sti; hlt");
                continue;
            }
            p->buffer[p->write_pos] = buf[written];
            p->write_pos = (p->write_pos + 1) % p->size;
            p->count++;
            written++;
        }
        return (int)written;
    }
    if (f->node) {
        // O_APPEND: always write at EOF
        if (f->flags & 0x400) {
            struct stat st;
            if (vfs_fstat(f->node, &st) == 0) f->offset = st.st_size;
        }
        int written = vfs_write(f->node, f->offset, (const void*)buffer, count);
        if (written > 0) {
            f->offset += written;
        }
        return written;
    }
    return -1;
}

static int is_console_fd(task_t *task, uint32_t fd){
    if (!task || (int)fd < 0 || fd >= MAX_FDS || !task->fds[fd]) return 0;
    struct file *f = task->fds[fd];
    return f->node == NULL && f->pipe == NULL;
}

static int sys_read(uint32_t fd, uint32_t buffer, uint32_t count){
    if (buffer == 0 || count == 0 || !syscall_range_valid(buffer, count)) return -1;
    task_t *task = scheduler_current_task();
    if (fd == 0 || is_console_fd(task, fd)) {
        char *buf = (char *)buffer;
        if (!cons_is_canonical()) {
            // raw mode: single chars, no line editing; VMIN>0 blocks for the first byte
            uint32_t n = 0;
            if (cons_cc(CON_VMIN) > 0) {
                char c = getchar();
                buf[n++] = c;
                if (cons_echo_on()) print_char(c, VGA_COLOR_WHITE);
            }
            while (n < count) {
                int c = keyboard_trygetc();
                if (c < 0) break;
                buf[n++] = (char)c;
                if (cons_echo_on()) print_char((char)c, VGA_COLOR_WHITE);
            }
            return (int)n;
        }
        int echo = cons_echo_on();
        uint32_t i = 0;
        while (i < count - 1) {
            char c = getchar();
            if (c == '\n') {
                buf[i++] = '\n';
                if (echo) print_char('\n', VGA_COLOR_WHITE);
                break;
            } else if (c == '\b') {
                if (i > 0) {
                    i--;
                    if (echo) {
                        print_char('\b', VGA_COLOR_WHITE);
                        print_char(' ', VGA_COLOR_WHITE);
                        print_char('\b', VGA_COLOR_WHITE);
                    }
                }
                continue;
            } else {
                buf[i++] = c;
                if (echo) print_char(c, VGA_COLOR_WHITE);
            }
        }
        buf[i] = '\0';
        return i;
    }

    if ((int)fd < 0 || fd >= MAX_FDS || !task->fds[fd]) {
        return -1;
    }
    struct file *f = task->fds[fd];
    if (f->pipe) {
        struct pipe *p = f->pipe;
        char *buf = (char *)buffer;
        uint32_t read_count = 0;
        while (read_count < count) {
            if (p->count == 0) {
                if (p->write_ref <= 0) break;
                asm volatile("sti; hlt");
                continue;
            }
            buf[read_count] = p->buffer[p->read_pos];
            p->read_pos = (p->read_pos + 1) % p->size;
            p->count--;
            read_count++;
        }
        return (int)read_count;
    }
    if (f->node) {
        int read_bytes = vfs_read(f->node, f->offset, (void*)buffer, count);
        if (read_bytes > 0) {
            f->offset += read_bytes;
        }
        return read_bytes;
    }
    return -1;
}

static int sys_exit(uint32_t status, uint32_t unused1, uint32_t unused2){
    (void)unused1;
    (void)unused2;
    task_t *task = scheduler_current_task();
    uint32_t ppid = task ? task->ppid : 1;
    task_exit((int)status);
    // init exiting would leave no runnable task and the zombie would get
    // rescheduled into a ring-3 hlt fault, so bring a fresh shell up instead
    if (ppid == 0) {
        extern void respawn_user_shell(void);
        respawn_user_shell();
    }
    return 0;
}

static int sys_getpid(uint32_t unused1, uint32_t unused2, uint32_t unused3){
    (void)unused1;
    (void)unused2;
    (void)unused3;
    return (int)scheduler_current_pid();
}

static int sys_fork(uint32_t unused1, uint32_t unused2, uint32_t unused3){
    (void)unused1;
    (void)unused2;
    (void)unused3;
    return fork_task(current_syscall_frame);
}

static int sys_execve(uint32_t path, uint32_t argv, uint32_t envp){
    if (!syscall_string_valid(path)) return -1;
    if (argv && !syscall_range_valid(argv, sizeof(uint32_t))) return -1;
    if (envp && !syscall_range_valid(envp, sizeof(uint32_t))) return -1;
    return sys_execve_impl(current_syscall_frame, path, argv, envp);
}

// length of a user string, per-byte validated, bounded. 0 on success.
static int exec_user_strlen(uint32_t uaddr, uint32_t *out_len){
    for (uint32_t i = 0; i < EXEC_MAX_STRLEN; i++) {
        if (!syscall_range_valid(uaddr + i, 1)) return -1;
        if (((const char*)uaddr)[i] == '\0') {
            *out_len = i;
            return 0;
        }
    }
    return -1;
}

// copy a user argv/envp vector into kmalloc'd kernel buffers.
// must run before the USER image is wiped, since the strings may live there.
static int exec_copy_vec(uint32_t uvec, char **kbufs, uint32_t *klens, int max, uint32_t *out_count, uint32_t *out_total){
    uint32_t count = 0;
    uint32_t total = 0;
    if (uvec == 0) {
        *out_count = 0;
        *out_total = 0;
        return 0;
    }
    for (int i = 0; i < max; i++) {
        if (!syscall_range_valid(uvec + (uint32_t)i * 4, 4)) goto fail;
        uint32_t a = ((uint32_t*)uvec)[i];
        if (a == 0) break;
        uint32_t len = 0;
        if (exec_user_strlen(a, &len) != 0) goto fail;
        if (total + len + 1 > EXEC_MAX_TOTAL) goto fail;
        char *k = (char*)kmalloc(len + 1);
        if (!k) goto fail;
        for (uint32_t j = 0; j <= len; j++) k[j] = ((const char*)a)[j];
        kbufs[count] = k;
        klens[count] = len;
        count++;
        total += len + 1;
    }
    if (count == (uint32_t)max) {
        // ran off the end without seeing NULL, malformed vector
        if (!syscall_range_valid(uvec + (uint32_t)max * 4, 4)) goto fail;
        if (((uint32_t*)uvec)[max] != 0) goto fail;
    }
    *out_count = count;
    *out_total = total;
    return 0;
fail:
    for (uint32_t i = 0; i < count; i++) kfree(kbufs[i]);
    return -1;
}

static void exec_free_vec(char **kbufs, uint32_t count){
    for (uint32_t i = 0; i < count; i++) kfree(kbufs[i]);
}

static int sys_execve_impl(struct interrupt_frame *frame, uint32_t path, uint32_t argv_u, uint32_t envp_u) {
    print("[EXECVE] Executing: ", VGA_COLOR_LIGHT_GREEN);
    println((const char*)path, VGA_COLOR_LIGHT_GREEN);

    // snapshot args first: they may point into the image/stack we are about to replace
    char *arg_bufs[EXEC_MAX_ARGS];
    uint32_t arg_lens[EXEC_MAX_ARGS];
    char *env_bufs[EXEC_MAX_ENVS];
    uint32_t env_lens[EXEC_MAX_ENVS];
    uint32_t argc = 0, envc = 0, arg_total = 0, env_total = 0;
    if (exec_copy_vec(argv_u, arg_bufs, arg_lens, EXEC_MAX_ARGS, &argc, &arg_total) != 0) {
        println("[EXECVE] Bad argv", VGA_COLOR_RED);
        return -1;
    }
    if (exec_copy_vec(envp_u, env_bufs, env_lens, EXEC_MAX_ENVS, &envc, &env_total) != 0) {
        exec_free_vec(arg_bufs, argc);
        println("[EXECVE] Bad envp", VGA_COLOR_RED);
        return -1;
    }
    uint32_t nwords = 1 + (argc + 1) + (envc + 1);
    if (arg_total + env_total + nwords * 4 + 64 > EXEC_STACK_SIZE - 128) {
        exec_free_vec(arg_bufs, argc);
        exec_free_vec(env_bufs, envc);
        println("[EXECVE] Args too big", VGA_COLOR_RED);
        return -1;
    }

    // resolve script path into a kernel buffer (may be rewritten by #! handling)
    char exec_path[256];
    {
        unsigned int i = 0;
        const char *up = (const char*)path;
        while (i < sizeof(exec_path) - 1 && up[i]) {
            exec_path[i] = up[i];
            i++;
        }
        exec_path[i] = '\0';
    }

    struct vfs_node *node = NULL;
    // #! loop: allow script -> interpreter chains up to 4 deep
    for (int depth = 0; depth < 5; depth++) {
        if (node) { vfs_close(node); node = NULL; }
        node = vfs_open(exec_path, 0);
        if (!node) {
            println("[EXECVE] vfs_open failed!", VGA_COLOR_RED);
            exec_free_vec(arg_bufs, argc);
            exec_free_vec(env_bufs, envc);
            return -1;
        }
        unsigned char hbuf[128];
        int hlen = vfs_read(node, 0, hbuf, sizeof(hbuf));
        if (hlen >= 2 && hbuf[0] == 0x7F && hlen >= 4 && hbuf[1] == 'E' && hbuf[2] == 'L' && hbuf[3] == 'F') {
            break; // ELF, keep node open for loader below
        }
        if (hlen >= 2 && hbuf[0] == '#' && hbuf[1] == '!') {
            // parse interpreter line
            unsigned int i = 2;
            while (i < (unsigned int)hlen && (hbuf[i] == ' ' || hbuf[i] == '\t')) i++;
            unsigned int istart = i;
            while (i < (unsigned int)hlen && hbuf[i] != '\n' && hbuf[i] != '\r' && hbuf[i] != ' ' && hbuf[i] != '\t' && hbuf[i] != '\0') i++;
            unsigned int ilen = i - istart;
            while (i < (unsigned int)hlen && (hbuf[i] == ' ' || hbuf[i] == '\t')) i++;
            unsigned int astart = i;
            while (i < (unsigned int)hlen && hbuf[i] != '\n' && hbuf[i] != '\r' && hbuf[i] != '\0') i++;
            unsigned int alen = i - astart;
            while (alen > 0 && (hbuf[astart + alen - 1] == ' ' || hbuf[astart + alen - 1] == '\t')) alen--;
            if (ilen == 0 || ilen >= 128) {
                println("[EXECVE] Bad #! line", VGA_COLOR_RED);
                vfs_close(node);
                exec_free_vec(arg_bufs, argc);
                exec_free_vec(env_bufs, envc);
                return -1;
            }
            char interp[128];
            for (unsigned int k = 0; k < ilen; k++) interp[k] = (char)hbuf[istart + k];
            interp[ilen] = '\0';
            char optarg[128];
            for (unsigned int k = 0; k < alen && k < sizeof(optarg) - 1; k++) optarg[k] = (char)hbuf[astart + k];
            optarg[alen < sizeof(optarg) ? alen : sizeof(optarg) - 1] = '\0';
            uint32_t need = argc + 1 + (alen > 0 ? 1 : 0);
            if (need > EXEC_MAX_ARGS) {
                println("[EXECVE] #! argv overflow", VGA_COLOR_RED);
                vfs_close(node);
                node = NULL;
                exec_free_vec(arg_bufs, argc);
                exec_free_vec(env_bufs, envc);
                return -1;
            }
            // build new argv: [interp, opt?, exec_path, old_argv[1..]]
            char *new_bufs[EXEC_MAX_ARGS];
            uint32_t new_lens[EXEC_MAX_ARGS];
            uint32_t nargc = 0;
            uint32_t ntotal = 0;
            char *ib = (char*)kmalloc(ilen + 1);
            if (!ib) {
                vfs_close(node);
                exec_free_vec(arg_bufs, argc);
                exec_free_vec(env_bufs, envc);
                return -1;
            }
            for (unsigned int k = 0; k <= ilen; k++) ib[k] = interp[k];
            new_bufs[nargc] = ib; new_lens[nargc] = ilen; nargc++; ntotal += ilen + 1;
            if (alen > 0) {
                char *ob = (char*)kmalloc(alen + 1);
                if (!ob) {
                    kfree(ib);
                    vfs_close(node);
                    exec_free_vec(arg_bufs, argc);
                    exec_free_vec(env_bufs, envc);
                    return -1;
                }
                for (unsigned int k = 0; k <= alen; k++) ob[k] = optarg[k];
                new_bufs[nargc] = ob; new_lens[nargc] = alen; nargc++; ntotal += alen + 1;
            }
            // script path itself
            unsigned int plen = 0;
            while (exec_path[plen] && plen < sizeof(exec_path) - 1) plen++;
            char *pb = (char*)kmalloc(plen + 1);
            if (!pb) {
                for (uint32_t k = 0; k < nargc; k++) kfree(new_bufs[k]);
                vfs_close(node);
                exec_free_vec(arg_bufs, argc);
                exec_free_vec(env_bufs, envc);
                return -1;
            }
            for (unsigned int k = 0; k <= plen; k++) pb[k] = exec_path[k];
            new_bufs[nargc] = pb; new_lens[nargc] = plen; nargc++; ntotal += plen + 1;
            // old argv[1..]
            for (uint32_t k = 1; k < argc; k++) {
                new_bufs[nargc] = arg_bufs[k];
                new_lens[nargc] = arg_lens[k];
                nargc++; ntotal += arg_lens[k] + 1;
            }
            // free old argv[0] (replaced by interp+script), keep env
            if (argc > 0) kfree(arg_bufs[0]);
            for (uint32_t k = 0; k < nargc; k++) {
                arg_bufs[k] = new_bufs[k];
                arg_lens[k] = new_lens[k];
            }
            argc = nargc;
            arg_total = ntotal;
            // next iteration loads the interpreter
            for (unsigned int k = 0; k <= ilen && k < sizeof(exec_path) - 1; k++) {
                exec_path[k] = interp[k];
                if (interp[k] == '\0') break;
            }
            exec_path[sizeof(exec_path) - 1] = '\0';
            print("[EXECVE] #! -> ", VGA_COLOR_LIGHT_GREEN);
            println(exec_path, VGA_COLOR_LIGHT_GREEN);
            continue;
        }
        // neither ELF nor #!
        println("[EXECVE] Not an ELF binary", VGA_COLOR_RED);
        vfs_close(node);
        node = NULL;
        exec_free_vec(arg_bufs, argc);
        exec_free_vec(env_bufs, envc);
        return -1;
    }
    if (!node) {
        println("[EXECVE] vfs_open failed!", VGA_COLOR_RED);
        exec_free_vec(arg_bufs, argc);
        exec_free_vec(env_bufs, envc);
        return -1;
    }
    // re-check stack budget after #! expansion
    nwords = 1 + (argc + 1) + (envc + 1);
    if (arg_total + env_total + nwords * 4 + 64 > EXEC_STACK_SIZE - 128) {
        println("[EXECVE] Args too big", VGA_COLOR_RED);
        vfs_close(node);
        exec_free_vec(arg_bufs, argc);
        exec_free_vec(env_bufs, envc);
        return -1;
    }

    uint32_t *stack = (uint32_t*)kmalloc(EXEC_STACK_SIZE);
    if (!stack) {
        vfs_close(node);
        exec_free_vec(arg_bufs, argc);
        exec_free_vec(env_bufs, envc);
        return -1;
    }
    for (int i = 0; i < EXEC_STACK_SIZE / 4; i++) stack[i] = 0;
    uint32_t stack_top = (uint32_t)stack + EXEC_STACK_SIZE - 64;
    stack_top &= ~0x0F;

    unsigned char header[52];
    if (vfs_read(node, 0, header, 52) != 52) {
        println("[EXECVE] Failed to read header", VGA_COLOR_RED);
        vfs_close(node);
        kfree(stack);
        exec_free_vec(arg_bufs, argc);
        exec_free_vec(env_bufs, envc);
        return -1;
    }

    uint32_t entry = *(uint32_t*)&header[24];
    uint32_t phoff = *(uint32_t*)&header[28];
    uint16_t phentsize = *(uint16_t*)&header[42];
    uint16_t phnum = *(uint16_t*)&header[44];

    // clear only the range the segments will occupy, not the whole 4M window
    uint32_t clear_end = 0;
    for (uint16_t i = 0; i < phnum; i++) {
        unsigned char ph[32];
        if (vfs_read(node, phoff + i * phentsize, ph, 32) != 32) {
            vfs_close(node);
            kfree(stack);
            exec_free_vec(arg_bufs, argc);
            exec_free_vec(env_bufs, envc);
            return -1;
        }
        if (*(uint32_t*)&ph[0] != ELF_PT_LOAD) continue;
        uint32_t seg_end = *(uint32_t*)&ph[8] - USER_LOAD_ADDR + *(uint32_t*)&ph[20];
        if (seg_end > clear_end) clear_end = seg_end;
    }
    if (clear_end > 0x400000) clear_end = 0x400000;
    if (clear_end == 0) {
        println("[EXECVE] No load segments", VGA_COLOR_RED);
        vfs_close(node);
        kfree(stack);
        exec_free_vec(arg_bufs, argc);
        exec_free_vec(env_bufs, envc);
        return -1;
    }
    task_t *cur_t = scheduler_current_task();
    if (cur_t && cur_t->ppid != 0 && !cur_t->img_snapshot) {
        task_t *p = find_task(cur_t->ppid);
        if (p && p->img_size > 0 && p->img_size <= 0x400000) {
            void *snap = kmalloc(p->img_size);
            if (snap) {
                for (uint32_t i = 0; i < p->img_size; i++)
                    ((char*)snap)[i] = ((char*)USER_LOAD_ADDR)[i];
                cur_t->img_snapshot = snap;
                cur_t->img_size = p->img_size;
            }
        }
    }

    for (uint32_t i = 0; i < clear_end; i++) {
        ((char*)USER_LOAD_ADDR)[i] = 0;
    }

    for (uint16_t i = 0; i < phnum; i++) {
        unsigned char ph[32];
        if (vfs_read(node, phoff + i * phentsize, ph, 32) != 32) {
            vfs_close(node);
            kfree(stack);
            exec_free_vec(arg_bufs, argc);
            exec_free_vec(env_bufs, envc);
            return -1;
        }
        uint32_t p_type = *(uint32_t*)&ph[0];
        uint32_t p_offset = *(uint32_t*)&ph[4];
        uint32_t p_vaddr = *(uint32_t*)&ph[8];
        uint32_t p_filesz = *(uint32_t*)&ph[16];
        uint32_t p_memsz = *(uint32_t*)&ph[20];

        if (p_type != ELF_PT_LOAD) continue;

        // go back to the userspace bitch
        uint32_t base = USER_LOAD_ADDR;
        void *dest = (void*)(base + (p_vaddr - USER_LOAD_ADDR));
        if (p_filesz > 0) {
            vfs_read(node, p_offset, dest, p_filesz);
        }
        if (p_memsz > p_filesz) {
            for (uint32_t j = p_filesz; j < p_memsz; j++) {
                ((char*)dest)[j] = 0;
            }
        }
    }

    vfs_close(node);

    // standard i386 layout the crt0 already expects:
    // [esp]=argc, [esp+4]=argv[0..argc,NULL], then envp[0..envc,NULL], strings above
    uint32_t arg_addrs[EXEC_MAX_ARGS];
    uint32_t env_addrs[EXEC_MAX_ENVS];
    uint32_t sp = stack_top;
    for (int i = (int)argc - 1; i >= 0; i--) {
        sp -= arg_lens[i] + 1;
        for (uint32_t j = 0; j <= arg_lens[i]; j++) ((char*)sp)[j] = arg_bufs[i][j];
        arg_addrs[i] = sp;
    }
    for (int i = (int)envc - 1; i >= 0; i--) {
        sp -= env_lens[i] + 1;
        for (uint32_t j = 0; j <= env_lens[i]; j++) ((char*)sp)[j] = env_bufs[i][j];
        env_addrs[i] = sp;
    }
    exec_free_vec(arg_bufs, argc);
    exec_free_vec(env_bufs, envc);
    sp -= nwords * 4;
    sp &= ~0x0F;
    if (sp < (uint32_t)stack + 64) {
        println("[EXECVE] Stack overflow", VGA_COLOR_RED);
        kfree(stack);
        return -1;
    }
    {
        uint32_t *w = (uint32_t*)sp;
        w[0] = argc;
        for (uint32_t i = 0; i < argc; i++) w[1 + i] = arg_addrs[i];
        w[1 + argc] = 0;
        for (uint32_t i = 0; i < envc; i++) w[1 + argc + 1 + i] = env_addrs[i];
        w[1 + argc + 1 + envc] = 0;
    }

    task_t *task = scheduler_current_task();
    if (task) {
        task->img_size = clear_end;
        // publish identity for /proc: comm = basename of the final image,
        // cmdline = NUL-joined argv snapshot (capped, Linux-style)
        {
            unsigned int bl = 0, last = 0;
            while (bl < sizeof(exec_path) && exec_path[bl]) {
                if (exec_path[bl] == '/') last = bl + 1;
                bl++;
            }
            unsigned int ci = 0;
            while (exec_path[last + ci] && ci < sizeof(task->comm) - 1) {
                task->comm[ci] = exec_path[last + ci];
                ci++;
            }
            task->comm[ci] = '\0';
            if (ci == 0) {
                task->comm[0] = '?';
                task->comm[1] = '\0';
            }
        }
        {
            unsigned int o = 0;
            for (unsigned int k = 0; k < sizeof(task->cmdline); k++)
                task->cmdline[k] = '\0';
            for (uint32_t i = 0; i < argc && o < sizeof(task->cmdline) - 1; i++) {
                for (uint32_t j = 0; j <= arg_lens[i] && o < sizeof(task->cmdline) - 1; j++)
                    task->cmdline[o++] = arg_bufs[i][j];
            }
            task->cmdline[o] = '\0';
        }
        // stack might be dads so dont free shared shit, just ditch it
        void *old_base = task->user_stack_base;
        int shared = 0;
        if (old_base && task->ppid != 0) {
            task_t *parent = find_task(task->ppid);
            if (parent && parent->user_stack_base == old_base) shared = 1;
        }
        if (old_base && !shared) kfree(old_base);
        task->user_stack_base = stack;
        task->user_stack_top = stack_top;
        // dad stays blocked until kid exits and its image is restored
    } else {
        // no task?? that shouldnt happen lol, dont leak
        kfree(stack);
        return -1;
    }

    frame->eip = entry;
    frame->esp = sp;
    frame->useresp = sp;
    frame->ebp = 0;
    frame->eax = 0;

    return 0;
}
static int sys_wait4(uint32_t pid, uint32_t status, uint32_t options){
    if (status && !syscall_range_valid(status, sizeof(int))) return -1;
    return wait4((int)pid, (int*)status, (int)options);
}

static int sys_getppid(uint32_t unused1, uint32_t unused2, uint32_t unused3){
    (void)unused1;
    (void)unused2;
    (void)unused3;
    task_t *task = scheduler_current_task();
    return task ? (int)task->ppid : -1;
}

static int sys_brk(uint32_t addr, uint32_t unused1, uint32_t unused2){
    (void)unused1;
    (void)unused2;
    task_t *task = scheduler_current_task();
    if (!task) {
        return -1;
    }
    if (addr == 0) {
        return (int)task->heap_brk;
    }
    if (addr < task->heap_start || addr > task->heap_end) {
        return (int)task->heap_brk;
    }
    task->heap_brk = addr;
    return (int)task->heap_brk;
}

static int sys_mmap(uint32_t length, uint32_t fd, uint32_t offset){
    task_t *task = scheduler_current_task();
    if (task && (int)fd >= 0 && fd < MAX_FDS && task->fds[fd] && task->fds[fd]->node) {
        if (vfs_is_fb0(task->fds[fd]->node)) {
            if (!fb_is_active()) return -1;
            uint32_t fb_size = fb_get_height() * fb_get_pitch();
            if (offset >= fb_size) return -1;
            return (int)(fb_get_addr() + offset);
        }
    }
    void *ptr = kmalloc(length);
    return (int)ptr;
}

static int sys_munmap(uint32_t addr, uint32_t unused1, uint32_t unused2){
    (void)unused1;
    (void)unused2;
    if (fb_is_active() && addr >= fb_get_addr() && addr < fb_get_addr() + fb_get_height() * fb_get_pitch()) {
        return 0;
    }
    kfree((void*)addr);
    return 0;
}


static int sys_pipe(uint32_t fds, uint32_t unused1, uint32_t unused2){
    (void)unused1;
    (void)unused2;
    int *user_fds = (int*)fds;
    if (!user_fds || !syscall_range_valid(fds, sizeof(int) * 2)) {
        return -1;
    }

    struct pipe *p = (struct pipe*)kmalloc(sizeof(struct pipe));
    if (!p) {
        return -1;
    }
    p->size = 4096;
    p->buffer = (char*)kmalloc(p->size);
    if (!p->buffer) {
        kfree(p);
        return -1;
    }
    p->read_pos = 0;
    p->write_pos = 0;
    p->count = 0;
    p->read_ref = 1;
    p->write_ref = 1;

    struct file *read_file = (struct file*)kmalloc(sizeof(struct file));
    struct file *write_file = (struct file*)kmalloc(sizeof(struct file));
    if (!read_file || !write_file) {
        if (read_file) kfree(read_file);
        if (write_file) kfree(write_file);
        kfree(p->buffer);
        kfree(p);
        return -1;
    }
    read_file->node = NULL;
    read_file->offset = 0;
    read_file->flags = 0;
    read_file->ref_count = 1;
    read_file->pipe = p;
    read_file->pipe_end = 0;

    write_file->node = NULL;
    write_file->offset = 0;
    write_file->flags = 0;
    write_file->ref_count = 1;
    write_file->pipe = p;
    write_file->pipe_end = 1;

    task_t *task = scheduler_current_task();
    int read_fd = alloc_fd(task, read_file);
    int write_fd = alloc_fd(task, write_file);
    if (read_fd < 0 || write_fd < 0) {
        if (read_fd >= 0) release_fd(task, read_fd);
        if (write_fd >= 0) release_fd(task, write_fd);
        return -1;
    }

    user_fds[0] = read_fd;
    user_fds[1] = write_fd;
    return 0;
}

static int sys_dup(uint32_t fd, uint32_t unused1, uint32_t unused2){
    (void)unused1;
    (void)unused2;
    task_t *task = scheduler_current_task();
    if ((int)fd < 0 || fd >= MAX_FDS || !task->fds[fd]) {
        return -1;
    }
    int new_fd = alloc_fd(task, task->fds[fd]);
    if (new_fd >= 0) {
        task->fds[fd]->ref_count++;
    }
    return new_fd;
}

static int sys_dup2(uint32_t oldfd, uint32_t newfd, uint32_t unused1){
    (void)unused1;
    task_t *task = scheduler_current_task();
    if ((int)oldfd < 0 || oldfd >= MAX_FDS || !task->fds[oldfd]) {
        return -1;
    }
    if ((int)newfd < 0 || newfd >= MAX_FDS) {
        return -1;
    }
    if (task->fds[newfd]) {
        release_fd(task, newfd);
    }
    task->fds[newfd] = task->fds[oldfd];
    task->fds[newfd]->ref_count++;
    return (int)newfd;
}

// how to kill a child and a parent (process ofcourse)
static int sys_kill(uint32_t pid, uint32_t sig, uint32_t unused1){
    (void)unused1;
    task_t *target = find_task(pid);
    if (!target) {
        return -1;
    }
    if (sig == 9) {
        target->state = TASK_ZOMBIE;
        target->exit_code = 128 + 9;
        return 0;
    }
    return -1;
}

static int sys_ioctl(uint32_t fd, uint32_t request, uint32_t arg){
    task_t *task = scheduler_current_task();
    if (task && (int)fd >= 0 && fd < MAX_FDS && task->fds[fd] && task->fds[fd]->node) {
        if (vfs_is_fb0(task->fds[fd]->node)) {
            return fb_ioctl(request, arg);
        }
    }
    if (!is_console_fd(task, fd)) return -1; // only the console is a tty
    switch (request) {

        case CON_TCGETS:
            if (arg == 0 || !syscall_range_valid(arg, sizeof(struct con_termios))) return -1;
            cons_tcget((struct con_termios*)arg);
            return 0;
        case CON_TCSETS:
        case CON_TCSETSW:
        case CON_TCSETSF:
            if (arg == 0 || !syscall_range_valid(arg, sizeof(struct con_termios))) return -1;
            cons_tcset((const struct con_termios*)arg);
            if (request == CON_TCSETSF) keyboard_flush();
            return 0;
        case CON_TIOCGWINSZ:
            if (arg == 0 || !syscall_range_valid(arg, sizeof(struct con_winsize))) return -1;
            cons_ws_get((struct con_winsize*)arg);
            return 0;
        case CON_TIOCSWINSZ:
            if (arg == 0 || !syscall_range_valid(arg, sizeof(struct con_winsize))) return -1;
            cons_ws_set((const struct con_winsize*)arg);
            return 0;
        case CON_FIONREAD:
            if (arg == 0 || !syscall_range_valid(arg, sizeof(int))) return -1;
            *(int*)arg = keyboard_available();
            return 0;
        default:
            return -1;
    }
}

static int sys_open(uint32_t path, uint32_t flags, uint32_t mode){
    (void)mode;
    if (!syscall_string_valid(path)) return -1;
    // O_EXCL|O_CREAT must fail if the file already exists
    if ((flags & 0x40) && (flags & 0x80)) {
        struct stat st;
        if (vfs_stat((const char*)path, &st) == 0) return -1;
    }
    struct vfs_node *node = vfs_open((const char*)path, (int)flags);
    if (!node) {
        return -1;
    }
    // O_TRUNC: empty regular files on open
    if (flags & 0x200) {
        vfs_truncate(node, 0);
    }
    struct file *f = (struct file*)kmalloc(sizeof(struct file));
    if (!f) {
        vfs_close(node);
        return -1;
    }
    f->node = node;
    f->offset = 0;
    // O_APPEND starts at EOF
    if (flags & 0x400) {
        struct stat st;
        if (vfs_fstat(node, &st) == 0) f->offset = st.st_size;
    }
    f->flags = (int)flags;
    f->ref_count = 1;
    f->pipe = NULL;
    f->pipe_end = 0;

    task_t *task = scheduler_current_task();
    int fd = alloc_fd(task, f);
    if (fd < 0) {
        vfs_close(node);
        kfree(f);
        return -1;
    }
    return fd;
}

static int sys_close(uint32_t fd, uint32_t unused1, uint32_t unused2){
    (void)unused1;
    (void)unused2;
    task_t *task = scheduler_current_task();
    if ((int)fd < 0 || fd >= MAX_FDS || !task->fds[fd]) {
        return -1;
    }
    release_fd(task, fd);
    return 0;
}

static int sys_lseek(uint32_t fd, uint32_t offset, uint32_t whence){
    task_t *task = scheduler_current_task();
    if ((int)fd < 0 || fd >= MAX_FDS || !task->fds[fd]) {
        return -1;
    }
    struct file *f = task->fds[fd];
    if (whence == 0) {
        f->offset = offset;
    } else if (whence == 1) {
        f->offset += offset;
    } else if (whence == 2) {
        struct stat st;
        if (f->node && vfs_fstat(f->node, &st) == 0) {
            f->offset = st.st_size + offset;
        }
    }
    return (int)f->offset;
}

static int sys_stat(uint32_t path, uint32_t st, uint32_t unused1){
    (void)unused1;
    if (!syscall_string_valid(path) || !syscall_range_valid(st, sizeof(struct stat))) return -1;
    return vfs_stat((const char*)path, (struct stat*)st);
}

static int sys_fstat(uint32_t fd, uint32_t st, uint32_t unused1){
    (void)unused1;
    if (!syscall_range_valid(st, sizeof(struct stat))) return -1;
    task_t *task = scheduler_current_task();
    if ((int)fd < 0 || fd >= MAX_FDS || !task->fds[fd] || !task->fds[fd]->node) {
        return -1;
    }
    return vfs_fstat(task->fds[fd]->node, (struct stat*)st);
}

static int sys_unlink(uint32_t path, uint32_t unused1, uint32_t unused2){
    (void)unused1;
    (void)unused2;
    if (!syscall_string_valid(path)) return -1;
    // Linux: unlink(2) refuses directories (use rmdir)
    struct stat st;
    if (vfs_lstat((const char*)path, &st) == 0) {
        if ((st.st_mode & 0x4000) != 0 && (st.st_mode & 0xF000) != (uint32_t)0xA000) return -1;
    }
    return vfs_unlink((const char*)path);
}

static int sys_mkdir(uint32_t path, uint32_t unused1, uint32_t unused2){
    (void)unused1;
    (void)unused2;
    if (!syscall_string_valid(path)) return -1;
    return vfs_mkdir((const char*)path);
}

static int sys_chdir(uint32_t path, uint32_t unused1, uint32_t unused2){
    (void)unused1;
    (void)unused2;
    if (!syscall_string_valid(path)) return -1;
    return vfs_chdir((const char*)path);
}

static int sys_getcwd(uint32_t buffer, uint32_t size, uint32_t unused1){
    (void)unused1;
    if (buffer == 0 || size == 0 || !syscall_range_valid(buffer, size)) return -1;
    return vfs_getcwd((char*)buffer, size);
}

static int sys_getdents(uint32_t fd, uint32_t buf, uint32_t bufsize){
    if (buf == 0 || bufsize == 0 || !syscall_range_valid(buf, bufsize)) return -1;
    task_t *task = scheduler_current_task();
    if (!task) return -1;
    if ((int)fd < 0 || fd >= MAX_FDS || !task->fds[fd]) return -1;
    struct file *f = task->fds[fd];
    if (!f->node) return -1;
    return vfs_getdents_by_node(f->node, (void*)buf, bufsize);
}

static int sys_ftruncate(uint32_t fd, uint32_t length, uint32_t unused1){
    (void)unused1;
    task_t *task = scheduler_current_task();
    if (!task) return -1;
    if ((int)fd < 0 || fd >= MAX_FDS || !task->fds[fd]) return -1;
    struct file *f = task->fds[fd];
    if (!f->node || f->pipe) return -1;
    return vfs_truncate(f->node, length);
}

// struct pollfd layout mirrors libc (int fd; short events; short revents)
#define CON_POLLIN 0x0001
#define CON_POLLOUT 0x0004
#define CON_POLLNVAL 0x0020

// layout mirrors libc struct utsname: 5 x 65-byte NUL-terminated fields
#define UTS_LEN 65

struct utsname_k {
    char sysname[UTS_LEN];
    char nodename[UTS_LEN];
    char release[UTS_LEN];
    char version[UTS_LEN];
    char machine[UTS_LEN];
};

static void uts_copy_field(char *dst, const char *src){
    uint32_t i = 0;
    while (src[i] && i < UTS_LEN - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int sys_uname(uint32_t buf, uint32_t unused1, uint32_t unused2){
    (void)unused1;
    (void)unused2;
    if (buf == 0 || !syscall_range_valid(buf, sizeof(struct utsname_k))) return -1;
    struct utsname_k *u = (struct utsname_k*)buf;
    uts_copy_field(u->sysname, "LiteBSD");
    uts_copy_field(u->nodename, "lite");
    uts_copy_field(u->release, "Release");
    uts_copy_field(u->version, "2.0");
    uts_copy_field(u->machine, "i386 (8086 mode)");
    return 0;
}

struct pollfd_k {
    int fd;
    short events;
    short revents;
};

static int sys_poll(uint32_t fds, uint32_t nfds, uint32_t timeout_u){
    int timeout = (int)timeout_u;
    if (nfds > 16) return -1;
    if (nfds > 0 && (fds == 0 || !syscall_range_valid(fds, nfds * sizeof(struct pollfd_k)))) return -1;
    task_t *task = scheduler_current_task();
    unsigned long start = timer_get_ticks();
    for (;;) {
        int nready = 0;
        for (uint32_t i = 0; i < nfds; i++) {
            struct pollfd_k *p = &((struct pollfd_k*)fds)[i];
            short rev = 0;
            int fd = p->fd;
            if (fd < 0) {
                p->revents = 0;
                continue;
            }
            if (!task || fd >= MAX_FDS || !task->fds[fd]) {
                rev = CON_POLLNVAL;
            } else {
                struct file *f = task->fds[fd];
                if (f->node == NULL && f->pipe == NULL) {
                    // console: input ready iff keys are buffered, output always ready
                    if ((p->events & CON_POLLIN) && keyboard_available() > 0) rev |= CON_POLLIN;
                    if (p->events & CON_POLLOUT) rev |= CON_POLLOUT;
                } else if (f->pipe) {
                    if ((p->events & CON_POLLIN) && f->pipe->count > 0) rev |= CON_POLLIN;
                    if ((p->events & CON_POLLOUT) && f->pipe->count < f->pipe->size) rev |= CON_POLLOUT;
                } else {
                    // regular files are always ready
                    if (p->events & CON_POLLIN) rev |= CON_POLLIN;
                    if (p->events & CON_POLLOUT) rev |= CON_POLLOUT;
                }
            }
            p->revents = rev;
            if (rev) nready++;
        }
        if (nready > 0) return nready;
        if (timeout == 0) return 0;
        // PIT runs at 100Hz, so 1 tick = 10ms
        if (timeout > 0 && timer_get_ticks() - start >= (unsigned long)((timeout + 9) / 10)) return 0;
        asm volatile("sti; hlt");
    }
}

static int sys_rename(uint32_t oldpath, uint32_t newpath, uint32_t unused1){
    (void)unused1;
    if (!syscall_string_valid(oldpath) || !syscall_string_valid(newpath)) return -1;
    return vfs_rename((const char*)oldpath, (const char*)newpath);
}

static int sys_rmdir(uint32_t path, uint32_t unused1, uint32_t unused2){
    (void)unused1;
    (void)unused2;
    if (!syscall_string_valid(path)) return -1;
    return vfs_rmdir((const char*)path);
}

static int sys_symlink(uint32_t target, uint32_t linkpath, uint32_t unused1){
    (void)unused1;
    if (!syscall_string_valid(target) || !syscall_string_valid(linkpath)) return -1;
    return vfs_symlink((const char*)target, (const char*)linkpath);
}

static int sys_readlink(uint32_t path, uint32_t buf, uint32_t bufsize){
    if (!syscall_string_valid(path)) return -1;
    if (buf == 0 || bufsize == 0 || !syscall_range_valid(buf, bufsize)) return -1;
    return vfs_readlink((const char*)path, (char*)buf, bufsize);
}

static int sys_lstat(uint32_t path, uint32_t st, uint32_t unused1){
    (void)unused1;
    if (!syscall_string_valid(path) || !syscall_range_valid(st, sizeof(struct stat))) return -1;
    return vfs_lstat((const char*)path, (struct stat*)st);
}

static int sys_truncate(uint32_t path, uint32_t length, uint32_t unused1){
    (void)unused1;
    if (!syscall_string_valid(path)) return -1;
    return vfs_truncate_path((const char*)path, length);
}

static int sys_access(uint32_t path, uint32_t mode, uint32_t unused1){
    (void)unused1;
    if (!syscall_string_valid(path)) return -1;
    return vfs_access((const char*)path, (int)mode);
}

static int sys_link(uint32_t oldpath, uint32_t newpath, uint32_t unused1){
    (void)unused1;
    if (!syscall_string_valid(oldpath) || !syscall_string_valid(newpath)) return -1;
    return vfs_link((const char*)newpath, (const char*)oldpath);
}

// times is NULL (set both to now) or a user pointer to two (sec, nsec)
// pairs in struct timespec layout. nsec validation happens in the VFS.
static int sys_futimens(uint32_t fd, uint32_t times, uint32_t unused1){
    (void)unused1;
    if (times != 0 && !syscall_range_valid(times, 16)) return -1;
    task_t *task = scheduler_current_task();
    if (!task) return -1;
    if ((int)fd < 0 || fd >= MAX_FDS || !task->fds[fd]) return -1;
    struct file *f = task->fds[fd];
    if (!f->node || f->pipe) return -1;
    return vfs_futimens(f->node, (const long*)times);
}

static int sys_utimens(uint32_t path, uint32_t times, uint32_t flags){
    if (!syscall_string_valid(path)) return -1;
    if (times != 0 && !syscall_range_valid(times, 16)) return -1;
    if (flags != 0 && flags != 0x100) return -1;
    return vfs_utimens((const char*)path, (const long*)times, (int)flags);
}

struct interrupt_frame *syscall_handler(struct interrupt_frame *frame){
    uint32_t syscall_num = frame->eax;
    uint32_t arg1 = frame->ebx;
    uint32_t arg2 = frame->ecx;
    uint32_t arg3 = frame->edx;
    current_syscall_frame = frame;

    if (syscall_num < SYSCALL_COUNT && syscall_table[syscall_num]) {
        int ret = syscall_table[syscall_num](arg1, arg2, arg3);
        if (syscall_num == 7 && ret == -2) {
            frame->eax = syscall_num;
            frame->eip -= 2;
            frame = schedule(frame);
            return frame;
        }
        frame->eax = ret;
        // dad is napping so run the kid first before dad touches shared shit again
        if (syscall_num == 5 && ret > 0) {
            frame = schedule(frame);
            return frame;
        }
    } else {
        println("[KERNEL] Unknown syscall", VGA_COLOR_RED);
        frame->eax = -1;
    }

    task_t *task = scheduler_current_task();
    if (task && task->state == TASK_ZOMBIE) {
        frame = schedule(frame);
    }
    return frame;
}

