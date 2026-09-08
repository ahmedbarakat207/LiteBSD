#include "include/syscall.h"
#include "include/tty.h"
#include "include/keyboard.h"
#include "include/sched.h"
#include "include/idt.h"
#include <stdint.h>

// same declarations that the code won't want to compile without!!
static int sys_write(uint32_t fd, uint32_t buffer, uint32_t count);
static int sys_read(uint32_t fd, uint32_t buffer, uint32_t count);
static int sys_exit(uint32_t status, uint32_t unused1, uint32_t unused2);
static int sys_getpid(uint32_t unused1, uint32_t unused2, uint32_t unused3);

typedef int (*syscall_func_t)(uint32_t, uint32_t, uint32_t);

static const syscall_func_t syscall_table[] = {
    NULL,                       // 0
    sys_write,  // 1
    sys_read,   // 2
    sys_exit,   // 3
    sys_getpid, // 4
};

#define SYSCALL_COUNT (sizeof(syscall_table)/sizeof(syscall_table[0]))

static int sys_write(uint32_t fd, uint32_t buffer, uint32_t count){
    if (fd != 1 || buffer == 0) return -1; // only stdout
    const char *buf = (const char *)buffer;
    for (uint32_t i = 0; i < count && buf[i]; i++) {
        print_char(buf[i], VGA_COLOR_WHITE);
    }
    return count;
}

static int sys_read(uint32_t fd, uint32_t buffer, uint32_t count){
    if (fd != 0 || buffer == 0 || count == 0) return -1;
    char *buf = (char *)buffer;
    uint32_t i = 0;
    while (i < count - 1) {
        char c = getchar();
        if (c == '\n') {
            buf[i++] = '\n';
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

static int sys_exit(uint32_t status, uint32_t unused1, uint32_t unused2){
    (void)status;
    (void)unused1;
    (void)unused2;
    println("[KERNEL] Task exiting", VGA_COLOR_WHITE);
    while (1) asm volatile("hlt");
    return 0;
}

static int sys_getpid(uint32_t unused1, uint32_t unused2, uint32_t unused3){
    (void)unused1;
    (void)unused2;
    (void)unused3;
    return (int)scheduler_current_pid();
}


// now to the star of the show
void syscall_handler(struct interrupt_frame *frame){
    uint32_t syscall_num = frame->eax;
    uint32_t arg1 = frame->ebx;
    uint32_t arg2 = frame->ecx;
    uint32_t arg3 = frame->edx;

    if (syscall_num < SYSCALL_COUNT && syscall_table[syscall_num]) {
        int ret = syscall_table[syscall_num](arg1, arg2, arg3);
        frame->eax = ret;
    } else {
        println("[KERNEL] Unknown syscall", VGA_COLOR_RED);
        frame->eax = -1;
    }
}