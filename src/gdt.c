#include <stdint.h>
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

struct gdt_entry gdt[6]; // NULL, kernel/user code/data, TSS
struct gdt_ptr gp;

struct tss_entry {
    uint32_t previous_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

static struct tss_entry tss;

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
    gp.limit = (sizeof(struct gdt_entry) * 6) - 1;
    gp.base = (unsigned int)&gdt;

    gdt_set_gate(0, 0, 0, 0, 0); // null segment
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); // kernel code segment
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // kernel data segment
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); // user code segment
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); // user data segment
    gdt_set_gate(5, (unsigned long)&tss, sizeof(tss) - 1, 0x89, 0x40);

    tss.ss0 = 0x10;
    tss.esp0 = 0;
    tss.iomap_base = sizeof(tss);

    gdt_flush((unsigned int)&gp);
    asm volatile("ltr %%ax" : : "a"((unsigned short)0x28));
    println("[GDT] GDT initialized.", VGA_COLOR_GREEN);
}

void tss_set_kernel_stack(unsigned int stack_top){
    tss.esp0 = stack_top;
}