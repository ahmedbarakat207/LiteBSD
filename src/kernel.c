#include "include/tty.h"
#include "include/gdt.h"
#include "include/paging.h"
#include "include/idt.h"
#include "include/keyboard.h"
#include "include/sched.h"
#include "include/time.h"
#include "include/syscall.h"

static void user_init(void){
    char message[] = "userspace: parent\n";
    int child = syscall_fork();
    if (child == 0) {
        char child_message[] = "userspace: child\n";
        syscall_write(child_message, sizeof(child_message) - 1);
        syscall_exit(7);
    }
    if (child > 0) {
        int status = 0;
        syscall_wait4(child, &status);
        syscall_write(message, sizeof(message) - 1);
    }
    syscall_exit(0);
}

void kernel_main(){
    clear();
    gdt_init();
    idt_init();
    paging_init();

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