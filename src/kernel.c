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
    char *argv[] = { "/bin/sh", NULL };
    syscall_execve("/bin/sh", argv, NULL);

    char *bb_argv[] = { "/bin/busybox", "sh", NULL };
    syscall_execve("/bin/busybox", bb_argv, NULL);

    char err_msg[] = "[INIT] Failed to exec /bin/sh or /bin/busybox\n";
    syscall_write(err_msg, sizeof(err_msg) - 1);
    syscall_exit(1);
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
    create_task(shell);
    create_user_task(user_init);

    asm volatile("sti");
    
    for(;;){
        asm volatile("hlt");
    }
}