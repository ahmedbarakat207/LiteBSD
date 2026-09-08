#include "include/idt.h"
#include "include/tty.h"
#include "include/keyboard.h"

extern const unsigned long isr_stub_table[256];
volatile unsigned int timer_ticks = 0;

struct idt_entry {
    unsigned short base_low;
    unsigned short sel;        // kernel segment selector
    unsigned char always0;
    unsigned char flags;       // flags (present, dbl, type)
    unsigned short base_high;
} __attribute__((packed));

struct idt_ptr {
    unsigned short limit;
    unsigned int base;
} __attribute__((packed));

struct idt_entry idt[256];
struct idt_ptr idtp;

void isr_handler(void);
void irq_handler(unsigned int irq_num);

void idt_set_gate(unsigned char num, unsigned long base, unsigned short sel, unsigned char flags){
    idt[num].base_low = (base & 0xFFFF);
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

void idt_init(){
    println("[IDT] Initializing IDT...", VGA_COLOR_WHITE);
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (unsigned int)&idt;

    for(int i = 0; i < 256; i++){
        idt_set_gate(i, isr_stub_table[i], 0x08, 0x8E);
    }

    asm volatile("lidtl (%0)" : : "r"(&idtp));
    println("[IDT] IDT initialized.", VGA_COLOR_GREEN);
}

struct interrupt_frame {
    unsigned int gs;
    unsigned int fs;
    unsigned int es;
    unsigned int ds;
    unsigned int edi;
    unsigned int esi;
    unsigned int ebp;
    unsigned int saved_esp;
    unsigned int ebx;
    unsigned int edx;
    unsigned int ecx;
    unsigned int eax;
    unsigned int interrupt_number;
    unsigned int error_code;
};

void isr_common_handler(void *raw_frame){
    struct interrupt_frame *frame = raw_frame;

    if(frame->interrupt_number >= 32 && frame->interrupt_number < 48){
        irq_handler(frame->interrupt_number - 32);
    } else if(frame->interrupt_number < 32){
        isr_handler();
    }
}

void pic_remap(){
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);

    // remap
    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28); 

    // tell pic1 about pic2
    outb(PIC1_DATA, 0x04); 
    outb(PIC2_DATA, 0x02);

    // set 8086 mode
    outb(PIC1_DATA, ICW4_8086); 
    outb(PIC2_DATA, ICW4_8086);

    // mask
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}
void outb(unsigned short port, unsigned char data) {
    asm volatile ("outb %0, %1" : : "a"(data), "Nd"(port));
}
static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void isr_handler(){
    err("CPU Exception occurred.");
}

void irq_handler(unsigned int irq_num){
    if(irq_num == 0){
        timer_ticks++;
        // test
        //if (timer_ticks % 100 == 0) println("[TIMER] Timer tick.", VGA_COLOR_CYAN);

    } else if (irq_num == 1){
        keyboard_handler();
        // test
        //println("[KEYBOARD] Key pressed.", VGA_COLOR_CYAN);
    }
    if (irq_num >= 8){
        outb(PIC2_COMMAND, 0x20);
    }
    outb(PIC1_COMMAND, 0x20);
}