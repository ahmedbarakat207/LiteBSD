#include "include/tty.h"
#include "include/gdt.h"
#include "include/paging.h"
#include "include/idt.h"
#include "include/keyboard.h"
#include "include/sched.h"
#include "include/time.h"
#include "include/syscall.h"
#include "include/initrd.h"
#include "include/multiboot.h"

static void user_init(void){
    println("[USER_INIT] Starting user_init...", VGA_COLOR_LIGHT_CYAN);
    char *argv[] = { "sh", "-i", NULL };
    char *envp[] = {
        "PATH=/bin:/sbin:/usr/bin:/usr/sbin",
        "HOME=/root",
        "USER=root",
        "TERM=vt100",
        "PS1=LiteBSD:\\w# ",
        NULL
    };
    syscall_execve("/bin/sh", argv, envp);

    char *bb_argv[] = { "busybox", "sh", "-i", NULL };
    syscall_execve("/bin/busybox", bb_argv, envp);

    char err_msg[] = "[INIT] Failed to exec /bin/sh or /bin/busybox\n";
    syscall_write(err_msg, sizeof(err_msg) - 1);
    syscall_exit(1);
}
void respawn_user_shell(void) {
    // sh croaked, bring it back lol
    println("[INIT] Respawning shell...", VGA_COLOR_YELLOW);
    // serial copy too cuz vga doesnt show on headless lol
    {
        const char *s = "[SERIAL] respawning shell\n";
        while (*s) {
            unsigned char lsr;
            do { asm volatile("inb %1, %0" : "=a"(lsr) : "Nd"((unsigned short)(0x3F8 + 5))); } while ((lsr & 0x20) == 0);
            asm volatile("outb %0, %1" : : "a"((unsigned char)*s), "Nd"((unsigned short)0x3F8));
            s++;
        }
    }
    create_user_task(user_init);
}

void kernel_main(struct mb_info *info){
    clear();
    gdt_init();
    idt_init();
    paging_init();

    if (info && (info->flags & MB_INFO_MODS)) {
        uint32_t mods_count = info->mods_count;
        uint32_t mods_addr = info->mods_addr;
        struct mb_mod *mods = (struct mb_mod*)mods_addr;
        if (mods_count >= 1) {
            uint32_t start = mods[0].mod_start;
            uint32_t end = mods[0].mod_end;
            initrd_load(start, end);
        } else {
            println("[KERNEL] No initrd module found.", VGA_COLOR_RED);
        }
    } else {
        println("[KERNEL] No multiboot modules present.", VGA_COLOR_RED);
    }


    pic_remap();
    outb(PIC1_DATA, 0xFC); 
    outb(PIC2_DATA, 0xFF); 

    set_timer_frequency(100);
    keyboard_init();

    println("[KERNEL] Kernel initialized.", VGA_COLOR_GREEN);
    new_line();
    new_line();
    println("Welcome to LiteBSD!!!", VGA_COLOR_WHITE);

    sched_init();
    // only hush owns the keyboard lol
    // the kernel shell kept stealing input and racing fork+exec
    // which corrupted dad after a failed exec and dumped u at the wrong prompt
    // create_task(shell);
    (void)shell;
    create_user_task(user_init);

    asm volatile("sti");
    
    for(;;){
        asm volatile("hlt");
    }
}