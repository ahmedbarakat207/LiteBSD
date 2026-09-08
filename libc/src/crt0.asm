[bits 32]
section .text
global _start
extern main
extern exit
extern environ

_start:
    xor ebp, ebp            ; clear frame ptr

    ; extract argc, argv, envp 
    mov eax, [esp]          ; argc
    test eax, eax
    jle .no_args

    lea edx, [esp + 4]      ; argv
    lea ecx, [esp + eax*4 + 8] ; envp
    mov [environ], ecx

    push ecx                ; envp
    push edx                ; argv
    push eax                ; argc
    call main
    jmp .done

.no_args:
    push 0                  ; envp = NULL
    push 0                  ; argv = NULL
    push 0                  ; argc = 0
    call main

.done:
    push eax                ; exit
    call exit

.halt:
    hlt
    jmp .halt
