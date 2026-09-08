section .multiboot
align 4
    dd 0x1BADB002
    dd 0x00 ; x86 arch
    dd -(0x1BADB002)

section .text
global start
extern kernel_main

start:
    cli
    mov esp, kernel_stack_top
    xor ebp, ebp
    push gdt_descriptor
    call gdt_flush
    add esp, 4
    call kernel_main

halt:
    cli
    hlt
    jmp halt

section .bss
align 16
kernel_stack_bottom:
resb 8192 ; 8kb stack
global kernel_stack_top
kernel_stack_top:

section .rodata
align 8
gdt_start:
    dq 0x0000000000000000 ; null descriptor
    dq 0x00CF9A000000FFFF ; kernel code: base 0, limit 4gb, ring 0
    dq 0x00CF92000000FFFF ; kernel data: base 0, limit 4gb, ring 0
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

section .text
global gdt_flush
gdt_flush:
    mov eax, [esp+4]    ; pointer to the gdt descriptor
    lgdt [eax]          ; load gdt

    mov ax, 0x10        ; 0x10 is the offset of the kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    jmp 0x08:.flush     ; 0x08 is the offset of the kernel code segment
.flush:
    ret

extern isr_common_handler

%macro ISR_NOERRCODE 1
[GLOBAL isr%1]
isr%1:
    cli
    push dword 0         ; dummy error code
    push dword %1        ; interrupt number
    jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
[GLOBAL isr%1]
isr%1:
    cli
    push dword %1  
    jmp isr_common_stub
%endmacro

; 32 cpu shit
ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_ERRCODE   30
ISR_NOERRCODE 31

; remaining 224 interrupts
%assign i 32
%rep 224
    ISR_NOERRCODE i
%assign i i+1
%endrep

section .rodata
align 4
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 256
    dd isr%+i
%assign i i+1
%endrep

isr_common_stub:
    pusha
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10         ; kernel data segment descriptor
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp             ; pointer to stack frame to C
    call isr_common_handler
    add esp, 4
    mov esp, eax         ; switch to the selected task's frame

    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8           ; clean bitch
    iret