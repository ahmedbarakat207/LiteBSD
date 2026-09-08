#include "include/tty.h"
#include "include/gdt.h"
#include "include/paging.h"
#include "include/idt.h"
#include "include/keyboard.h"

void set_timer_frequency(int hz) {
    int divisor = 1193180 / hz; // 1.19318 mhz
    outb(PIT_COMMAND, 0x36);
    outb(PIT_DATA0, divisor & 0xFF);
    outb(PIT_DATA0, (divisor >> 8) & 0xFF);
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

    asm volatile("sti");
    shell();
    
    for(;;){
        asm volatile("hlt");
    }
}