#include "include/tty.h"
#include "include/gdt.h"
#include "include/paging.h"
#include "include/idt.h"
#include "include/keyboard.h"
#include "include/sched.h"
#include "include/time.h"


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

    asm volatile("sti");
    
    for(;;){
        asm volatile("hlt");
    }
}