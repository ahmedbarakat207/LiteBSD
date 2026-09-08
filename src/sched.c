#include "include/sched.h"
#include "include/tty.h"
#include "include/idt.h"
#include "include/heap.h"

static task_t *current_task = NULL;
static task_t *ready_queue = NULL;
static uint32_t next_pid = 1; // pid = 0

static void add_task(task_t *task){
    if(!ready_queue){
        ready_queue = task;
        task->next  = task;
    } else {
        task->next = ready_queue->next;
        ready_queue->next = task;
        ready_queue = task;
    }
}

// remember when a task manager was just a to do app for your day?!
int create_task(void (*entry)(void)){
    uint32_t *stack = (uint32_t*)kmalloc(4096); // 4kb

    // error handling stack
    if (!stack) {
        println("[SCHED] Failed to allocate the stack.", VGA_COLOR_RED);
        return -1;
    }

    uint32_t stack_top = (uint32_t)stack + 4096;
    stack_top &= ~0x0F;
    stack_top -= sizeof(struct interrupt_frame);
    struct interrupt_frame *frame = (struct interrupt_frame*)stack_top;
    frame->gs = 0x10;
    frame->fs = 0x10;
    frame->es = 0x10;
    frame->ds = 0x10;
    frame->edi = 0;
    frame->esi = 0;
    frame->ebp = 0;
    frame->esp = 0;
    frame->ebx = 0;
    frame->edx = 0;
    frame->ecx = 0;
    frame->eax = 0;
    frame->interrupt_number = 32;
    frame->error_code = 0;
    frame->eip = (uint32_t)entry;
    frame->cs = 0x08;
    frame->eflags = 0x202;

    task_t *task = (task_t*)kmalloc(sizeof(task_t));
    
    // error handling task
    if (!task) {
        kfree(stack);
        println("[SCHED] Failed to allocate the task struct.", VGA_COLOR_RED);
        return -1;
    }

    task->pid = next_pid++;
    task->frame = frame;
    task->stack_base = stack;
    task->next = NULL;

    add_task(task);

    println("[SCHED] Created task with PID: ", VGA_COLOR_WHITE);
    
    return task->pid;
}

int create_user_task(void (*entry)(void)) {
    uint32_t *stack = (uint32_t*)kmalloc(4096);
    if (!stack) {
        println("[SCHED] Failed to allocate user stack.", VGA_COLOR_RED);
        return -1;
    }

    uint32_t stack_top = (uint32_t)stack + 4096;
    stack_top &= ~0x0F;
    stack_top -= sizeof(struct interrupt_frame);
    struct interrupt_frame *frame = (struct interrupt_frame*)stack_top;

    // same stuff as create_task but for the userspace
    frame->gs = 0x23;      // user data segment selector | 3
    frame->fs = 0x23;
    frame->es = 0x23;
    frame->ds = 0x23;
    frame->edi = 0;
    frame->esi = 0;
    frame->ebp = 0;
    frame->esp = stack_top; // user stack pointer
    frame->ebx = 0;
    frame->edx = 0;
    frame->ecx = 0;
    frame->eax = 0;
    frame->interrupt_number = 0;
    frame->error_code = 0;
    frame->eip = (uint32_t)entry;
    frame->cs = 0x1B;      // user code segment selector | 3 (0x18 | 3)
    frame->eflags = 0x202; // interrupts enabled

    task_t *task = (task_t*)kmalloc(sizeof(task_t));
    if (!task) {
        kfree(stack);
        println("[SCHED] Failed to allocate task struct.", VGA_COLOR_RED);
        return -1;
    }

    task->pid = next_pid++;
    task->frame = frame;
    task->stack_base = stack;
    task->next = NULL;
    task->user = 1; // 1 = user | 0 = kernel

    add_task(task);
    println("[SCHED] Created user task", VGA_COLOR_WHITE);
    return task->pid;
}

// i should schedule my time too
struct interrupt_frame *schedule(struct interrupt_frame *frame){
    if (!ready_queue){
        return frame;
    }

    if (!current_task){
        current_task = ready_queue->next;
        return current_task->frame;
    }

    current_task->frame = frame;

    current_task = current_task->next;
    return current_task->frame;
}

void sched_init() {
    println("[SCHED] Initializing scheduler...", VGA_COLOR_WHITE);
    // let's pretend that this thing does something 
    println("[SCHED] Scheduler ready.", VGA_COLOR_GREEN);
}

uint32_t scheduler_current_pid(void){
    return current_task ? current_task->pid : 0;
}