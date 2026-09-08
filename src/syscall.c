#include "include/syscall.h"
#include "include/tty.h"
#include "include/keyboard.h"
#include "include/sched.h"
#include "include/idt.h"
#include "include/heap.h"
#include "include/vfs.h"
#include <stdint.h>

#define ELF_PT_LOAD 1

// same declarations that the code won't want to compile without!!
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

static int sys_execve_impl(struct interrupt_frame *frame, uint32_t path);

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
    sys_getcwd,                 // 25
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
    if (fd == 1 || fd == 2) { // stdout and stderr
        const char *buf = (const char *)buffer;
        for (uint32_t i = 0; i < count; i++) {
            print_char(buf[i], VGA_COLOR_WHITE);
        }
        return count;
    }

    task_t *task = scheduler_current_task();
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
                asm volatile("hlt");
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
        int written = vfs_write(f->node, f->offset, (const void*)buffer, count);
        if (written > 0) {
            f->offset += written;
        }
        return written;
    }
    return -1;
}

static int sys_read(uint32_t fd, uint32_t buffer, uint32_t count){
    if (buffer == 0 || count == 0 || !syscall_range_valid(buffer, count)) return -1;
    if (fd == 0) {
        char *buf = (char *)buffer;
        uint32_t i = 0;
        while (i < count - 1) {
            char c = getchar();
            if (c == '\n') {
                buf[i++] = '\n';
                print_char('\n', VGA_COLOR_WHITE);
                break;
            } else if (c == '\b') {
                if (i > 0) {
                    i--;
                    print_char('\b', VGA_COLOR_WHITE);
                    print_char(' ', VGA_COLOR_WHITE);
                    print_char('\b', VGA_COLOR_WHITE);
                }
                continue;
            } else {
                buf[i++] = c;
                print_char(c, VGA_COLOR_WHITE);
            }
        }
        buf[i] = '\0';
        return i;
    }

    task_t *task = scheduler_current_task();
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
                asm volatile("hlt");
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
    println("[KERNEL] Task exiting", VGA_COLOR_WHITE);
    task_exit((int)status);
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
    return sys_execve_impl(current_syscall_frame, path);
}

static int sys_execve_impl(struct interrupt_frame *frame, uint32_t path) {
    print("[EXECVE] Executing: ", VGA_COLOR_LIGHT_GREEN);
    println((const char*)path, VGA_COLOR_LIGHT_GREEN);

    struct vfs_node *node = vfs_open((const char*)path, 0);
    if (!node) {
        println("[EXECVE] vfs_open failed!", VGA_COLOR_RED);
        return -1;
    }

    unsigned char header[52];
    if (vfs_read(node, 0, header, 52) != 52) {
        println("[EXECVE] Failed to read header", VGA_COLOR_RED);
        vfs_close(node);
        return -1;
    }
    if (header[0] != 0x7F || header[1] != 'E' || header[2] != 'L' || header[3] != 'F') {
        println("[EXECVE] Not an ELF binary", VGA_COLOR_RED);
        vfs_close(node);
        return -1;
    }

    uint32_t entry = *(uint32_t*)&header[24];
    uint32_t phoff = *(uint32_t*)&header[28];
    uint16_t phentsize = *(uint16_t*)&header[42];
    uint16_t phnum = *(uint16_t*)&header[44];

    // clear the userspace
    for (uint32_t i = 0; i < 0x400000; i++) {
        ((char*)USER_LOAD_ADDR)[i] = 0;
    }

    for (uint16_t i = 0; i < phnum; i++) {
        unsigned char ph[32];
        if (vfs_read(node, phoff + i * phentsize, ph, 32) != 32) {
            vfs_close(node); return -1;
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
    uint32_t *stack = (uint32_t*)kmalloc(16384);
    if (!stack) return -1;
    for (int i = 0; i < 16384 / 4; i++) stack[i] = 0;
    uint32_t stack_top = (uint32_t)stack + 16384 - 64;
    stack_top &= ~0x0F;

    task_t *task = scheduler_current_task();
    if (task->user_stack_base) kfree(task->user_stack_base);
    task->user_stack_base = stack;
    task->user_stack_top = stack_top;

    frame->eip = entry;
    frame->esp = stack_top;
    frame->useresp = stack_top;
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

static int sys_mmap(uint32_t length, uint32_t unused1, uint32_t unused2){
    (void)unused1;
    (void)unused2;
    void *ptr = kmalloc(length);
    return (int)ptr;
}

static int sys_munmap(uint32_t addr, uint32_t unused1, uint32_t unused2){
    (void)unused1;
    (void)unused2;
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
    (void)fd;
    (void)request;
    (void)arg;
    return -1;
}

static int sys_open(uint32_t path, uint32_t flags, uint32_t mode){
    (void)mode;
    if (!syscall_string_valid(path)) return -1;
    struct vfs_node *node = vfs_open((const char*)path, (int)flags);
    if (!node) {
        return -1;
    }
    struct file *f = (struct file*)kmalloc(sizeof(struct file));
    if (!f) {
        vfs_close(node);
        return -1;
    }
    f->node = node;
    f->offset = 0;
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
    task_t *task = scheduler_current_task();
    int result = vfs_chdir((const char*)path);
    if (result == 0) {
        const char *p = (const char*)path;
        int i = 0;
        while (p[i] && i < 255) {
            task->cwd[i] = p[i];
            i++;
        }
        task->cwd[i] = '\0';
    }
    return result;
}

static int sys_getcwd(uint32_t buffer, uint32_t size, uint32_t unused1){
    (void)unused1;
    if (buffer == 0 || size == 0 || !syscall_range_valid(buffer, size)) return -1;
    task_t *task = scheduler_current_task();
    char *buf = (char*)buffer;
    unsigned int i = 0;
    while (task->cwd[i] && i < size - 1) {
        buf[i] = task->cwd[i];
        i++;
    }
    buf[i] = '\0';
    return (int)i;
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
