#include "include/gdt.h"
#include "include/tty.h"

// single gdt entry
struct gdt_entry {
    unsigned short limit_low;
    unsigned short base_low;
    unsigned char base_middle;
    unsigned char access;
    unsigned char granularity;
    unsigned char base_high;
} __attribute__((packed));

// gdt pointer
struct gdt_ptr {
    unsigned short limit;
    unsigned int base;
} __attribute__((packed));

struct gdt_entry gdt[5]; // NULL, kernel_code, kernel_data, user_code, user_data
struct gdt_ptr gp;

void gdt_set_gate(int num, unsigned long base, unsigned long limit, unsigned char access, unsigned char gran){
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;

    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;

    gdt[num].granularity |= gran & 0xF0; // set granularity
    gdt[num].access = access;
}

// stub to load gdt
extern void gdt_flush(unsigned int);

// initialize gdt
void gdt_init(){
    println("[GDT] Initializing GDT...", VGA_COLOR_WHITE);
    gp.limit = (sizeof(struct gdt_entry) * 5) - 1;
    gp.base = (unsigned int)&gdt;

    gdt_set_gate(0, 0, 0, 0, 0); // null segment
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); // kernel code segment
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // kernel data segment
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); // user code segment
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); // user data segment

    gdt_flush((unsigned int)&gp);
    println("[GDT] GDT initialized.", VGA_COLOR_GREEN);
}