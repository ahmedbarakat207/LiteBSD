#include "include/idt.h"
#include "include/tty.h"
#include "include/keyboard.h"
#include "include/sched.h"
#include "include/time.h"
#include "include/syscall.h"

extern const unsigned long isr_stub_table[256];

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

void isr_handler(struct interrupt_frame *frame);
struct interrupt_frame *irq_handler(unsigned int irq_num, struct interrupt_frame *frame);
static void serial_init(void);
static void serial_print(const char *s);

void idt_set_gate(unsigned char num, unsigned long base, unsigned short sel, unsigned char flags){
    idt[num].base_low = (base & 0xFFFF);
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

void idt_init(){
    serial_init();
    serial_print("[SERIAL] idt init\n");
    println("[IDT] Initializing IDT...", VGA_COLOR_WHITE);
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (unsigned int)&idt;

    for(int i = 0; i < 256; i++){
        unsigned char flags = 0x8E; // ring 0
        if (i == 0x80) {
            flags = 0xEE; // ring 3
        }
        idt_set_gate(i, isr_stub_table[i], 0x08, flags);
    }

    asm volatile("lidtl (%0)" : : "r"(&idtp));
    println("[IDT] IDT initialized.", VGA_COLOR_GREEN);
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

static void serial_init(void) {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x03);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}

static void serial_putc(char c) {
    while ((inb(0x3F8 + 5) & 0x20) == 0) { }
    outb(0x3F8, (unsigned char)c);
}

void serial_write(const char *s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s);
        s++;
    }
}

void serial_write_hex(unsigned int v) {
    const char *h = "0123456789abcdef";
    serial_putc('0');
    serial_putc('x');
    for (int i = 7; i >= 0; i--) serial_putc(h[(v >> (i * 4)) & 0xF]);
}

static void serial_print(const char *s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s);
        s++;
    }
}

static void serial_hex(unsigned int v) {
    const char *h = "0123456789abcdef";
    serial_print("0x");
    for (int i = 7; i >= 0; i--) serial_putc(h[(v >> (i * 4)) & 0xF]);
}

void isr_handler(struct interrupt_frame *frame){
    serial_print("[SERIAL] CPU Exception ");
    // no printf here so decimal the awful way
    {
        unsigned int n = frame->interrupt_number;
        char b[12]; int i = 0;
        if (n == 0) serial_putc('0');
        else { while (n && i < 11) { b[i++] = '0' + (n % 10); n /= 10; } while (i) serial_putc(b[--i]); }
    }
    {
        task_t *ct = scheduler_current_task();
        unsigned int pid = 0xFFFFFFFF;
        if (ct) pid = ct->pid;
        serial_print(" pid=");
        if (pid == 0xFFFFFFFF) serial_print("?");
        else {
            char b[12]; int i = 0;
            if (pid == 0) serial_putc('0');
            else { while (pid && i < 11) { b[i++] = '0' + (pid % 10); pid /= 10; } while (i) serial_putc(b[--i]); }
        }
    }
    serial_print(" err=");
    serial_hex(frame->error_code);
    serial_print(" eip=");
    serial_hex(frame->eip);
    serial_print(" cs=");
    serial_hex(frame->cs);
    serial_print(" ebp=");
    serial_hex(frame->ebp);
    serial_print(" esp=");
    serial_hex(frame->esp);
    serial_print(" useresp=");
    serial_hex(frame->useresp);
    // dump user stack around useresp cuz eip=7 wild ret bullshit
    {
        unsigned int uesp = frame->useresp;
        serial_print(" ustack:");
        for (int i = 0; i < 8; i++) {
            unsigned int addr = uesp + (unsigned int)(i * 4);
            unsigned int val = 0xDEADBEEF;
            // user stack is mapped so just read it directly lol
            val = *(volatile unsigned int*)addr;
            serial_print(" ");
            serial_hex(val);
        }
    }
    {
        task_t *ct = scheduler_current_task();
        if (ct) {
            serial_print(" ubase=");
            serial_hex((unsigned int)ct->user_stack_base);
            serial_print(" utop=");
            serial_hex(ct->user_stack_top);
            serial_print(" heap=");
            serial_hex(ct->heap_start);
            serial_print("-");
            serial_hex(ct->heap_brk);
            serial_print("-");
            serial_hex(ct->heap_end);
        }
    }
    if (frame->interrupt_number == 14) {
        unsigned int cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        serial_print(" cr2=");
        serial_hex(cr2);
    }
    serial_print("\n");
    print("[ERR] CPU Exception ", VGA_COLOR_RED);
    print_dec(frame->interrupt_number, VGA_COLOR_LIGHT_RED);
    print(" err=", VGA_COLOR_WHITE);
    print_hex(frame->error_code, VGA_COLOR_LIGHT_CYAN);
    print(" eip=", VGA_COLOR_WHITE);
    print_hex(frame->eip, VGA_COLOR_LIGHT_CYAN);
    print(" cs=", VGA_COLOR_WHITE);
    print_hex(frame->cs, VGA_COLOR_LIGHT_CYAN);
    if (frame->interrupt_number == 14) {
        unsigned int cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        print(" cr2=", VGA_COLOR_WHITE);
        print_hex(cr2, VGA_COLOR_LIGHT_CYAN);
    }
    new_line();
    {
        task_t *ct = scheduler_current_task();
        int is_user = ct && ct->user && frame->cs == 0x1B;
        if (is_user) {
            unsigned int killed_pid = ct->pid;
            unsigned int killed_ppid = ct->ppid;
            serial_print("[SERIAL] killing faulted pid=");
            {
                unsigned int pid = killed_pid;
                char b[12]; int i = 0;
                if (pid == 0) serial_putc('0');
                else { while (pid && i < 11) { b[i++] = '0' + (pid % 10); pid /= 10; } while (i) serial_putc(b[--i]); }
            }
            serial_print(" sig=11\n");
            println("[KERNEL] Faulted task killed (SIGSEGV).", VGA_COLOR_YELLOW);
            task_exit(128 + 11);
            // task_exit already woke dad up, run away lol
            // if init sh itself croaked bring it back so the box stays usable
            if (killed_ppid == 0) {
                extern void respawn_user_shell(void);
                respawn_user_shell();
            }
            return;
        }
    }
    while (1) {
        asm volatile("cli; hlt");
    }
}

struct interrupt_frame *isr_common_handler(void *raw_frame) {
    struct interrupt_frame *frame = (struct interrupt_frame*)raw_frame;
    if (frame->interrupt_number == 0x80) {
        return syscall_handler(frame);
    } else if (frame->interrupt_number >= 32 && frame->interrupt_number < 48) {
        return irq_handler(frame->interrupt_number - 32, frame);
    } else if (frame->interrupt_number < 32) {
        isr_handler(frame);
        // if we just killed someone gtfo from their frame lol
        task_t *now = scheduler_current_task();
        if (now && now->state == TASK_ZOMBIE) {
            return schedule(frame);
        }
    }
    return frame;
}

struct interrupt_frame *irq_handler(unsigned int irq_num, struct interrupt_frame *frame){
    if(irq_num == 0){
        timer_tick();
        frame = schedule(frame);
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
    return frame;
}