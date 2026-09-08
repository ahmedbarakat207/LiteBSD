#include "include/paging.h"
#include "include/tty.h"
unsigned int page_dir[1024] __attribute__((aligned(4096)));
unsigned int page_table[1024] __attribute__((aligned(4096)));

void paging_init(){
    println("[PAGING] Initializing paging...", VGA_COLOR_WHITE);
    int i;
    for(i = 0; i < 1024; i++){
        page_table[i] = (i * 0x1000) | 0x00000003; // r/w | present
    }
    page_dir[0] = (unsigned int)page_table | 0x00000003;
    for (i = 1; i < 1024; i++) {
        page_dir[i] = ((unsigned int)i << 22) | 0x00000083; // 4mb page, r/w, present
    }

    // load page_dir into cr3
    asm volatile("mov %0, %%cr3" : : "r"(page_dir));

    unsigned int cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= 0x00000010; // enable 4 MiB pages (CR4.PSE)
    asm volatile("mov %0, %%cr4" : : "r"(cr4));

    unsigned int cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; // set paging bit
    asm volatile("mov %0, %%cr0" : : "r"(cr0));
    println("[PAGING] Paging initialized.", VGA_COLOR_GREEN);
}
