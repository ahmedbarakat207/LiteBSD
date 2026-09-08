#include "include/sched.h"
#include "include/tty.h"
#include "include/idt.h"
#include "include/heap.h"
#include "include/vfs.h"
#include "include/gdt.h"

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

static void remove_task(task_t *target){
    if (!ready_queue) return;
    if (ready_queue->next == ready_queue && ready_queue == target){
        ready_queue = NULL;
        return;
    }
    task_t *prev = ready_queue;
    do {
        if (prev->next == target){
            prev->next = target->next;
            if (ready_queue == target){
                ready_queue = prev;
            }
            return;
        }
        prev = prev->next;
    } while (prev != ready_queue);
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
    task->user_stack_base = NULL;
    task->user_stack_top = 0;
    task->next = NULL;
    task->user = 0; // kernel task
    task->ppid = 0;
    task->state = TASK_RUNNING;
    task->exit_code = 0;
    for (int i = 0; i < 3; i++) {
        struct file *f = (struct file*)kmalloc(sizeof(struct file));
        if (f) {
            f->node = NULL;
            f->offset = 0;
            f->flags = (i == 0) ? 0 : 1;
            f->ref_count = 1;
            f->pipe = NULL;
            f->pipe_end = 0;
            task->fds[i] = f;
        } else {
            task->fds[i] = NULL;
        }
    }
    for (int i = 3; i < MAX_FDS; i++){
        task->fds[i] = NULL;
    }
    task->cwd[0] = '/';
    task->cwd[1] = '\0';
    {
        unsigned int user_heap = (unsigned int)kmalloc(0x10000);
        task->heap_start = user_heap;
        task->heap_brk = user_heap;
        task->heap_end = user_heap + 0x10000;
    }

    add_task(task);

    println("[SCHED] Created task with PID: ", VGA_COLOR_WHITE);
    
    return task->pid;
}

int create_user_task(void (*entry)(void)) {
    uint32_t *kernel_stack = (uint32_t*)kmalloc(4096);
    uint32_t *user_stack = (uint32_t*)kmalloc(4096);
    if (!kernel_stack || !user_stack) {
        if (kernel_stack) kfree(kernel_stack);
        if (user_stack) kfree(user_stack);
        println("[SCHED] Failed to allocate user stack.", VGA_COLOR_RED);
        return -1;
    }

    uint32_t kernel_top = ((uint32_t)kernel_stack + 4096) & ~0x0F;
    kernel_top -= sizeof(struct interrupt_frame);
    struct interrupt_frame *frame = (struct interrupt_frame*)kernel_top;

    // same stuff as create_task but for the userspace
    frame->gs = 0x23;      // user data segment selector | 3
    frame->fs = 0x23;
    frame->es = 0x23;
    frame->ds = 0x23;
    frame->edi = 0;
    frame->esi = 0;
    frame->ebp = 0;
    frame->esp = ((uint32_t)user_stack + 4096) & ~0x0F;
    frame->ebx = 0;
    frame->edx = 0;
    frame->ecx = 0;
    frame->eax = 0;
    frame->interrupt_number = 0;
    frame->error_code = 0;
    frame->eip = (uint32_t)entry;
    frame->cs = 0x1B;      // user code segment selector | 3 (0x18 | 3)
    frame->eflags = 0x202; // interrupts enabled
    frame->useresp = frame->esp;
    frame->ss = 0x23;

    task_t *task = (task_t*)kmalloc(sizeof(task_t));
    if (!task) {
        kfree(kernel_stack);
        kfree(user_stack);
        println("[SCHED] Failed to allocate task struct.", VGA_COLOR_RED);
        return -1;
    }

    task->pid = next_pid++;
    task->frame = frame;
    task->stack_base = kernel_stack;
    task->user_stack_base = user_stack;
    task->user_stack_top = frame->useresp;
    task->next = NULL;
    task->user = 1; // 1 = user | 0 = kernel
    task->ppid = 0;
    task->state = TASK_RUNNING;
    task->exit_code = 0;
    for (int i = 0; i < 3; i++) {
        struct file *f = (struct file*)kmalloc(sizeof(struct file));
        if (f) {
            f->node = NULL;
            f->offset = 0;
            f->flags = (i == 0) ? 0 : 1;
            f->ref_count = 1;
            f->pipe = NULL;
            f->pipe_end = 0;
            task->fds[i] = f;
        } else {
            task->fds[i] = NULL;
        }
    }
    for (int i = 3; i < MAX_FDS; i++){
        task->fds[i] = NULL;
    }
    task->cwd[0] = '/';
    task->cwd[1] = '\0';
    {
        unsigned int user_heap = (unsigned int)kmalloc(0x100000);
        task->heap_start = user_heap;
        task->heap_brk = user_heap;
        task->heap_end = user_heap + 0x100000;
    }

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
        tss_set_kernel_stack((uint32_t)current_task->stack_base + 4096);
        return current_task->frame;
    }

    current_task->frame = frame;

    task_t *next = current_task->next;
    // skip corpses and napping dads
    // if everyone else is dead/asleep just keep running current lol
    task_t *start = next;
    while ((next->state == TASK_ZOMBIE || next->state == TASK_BLOCKED) && next != current_task) {
        next = next->next;
        if (next == start) break;
    }
    // picked a corpse but current is fine? stay on current then
    if ((next->state == TASK_ZOMBIE || next->state == TASK_BLOCKED) && current_task->state == TASK_RUNNING) {
        next = current_task;
    }
    current_task = next;
    tss_set_kernel_stack((uint32_t)current_task->stack_base + 4096);
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

task_t *scheduler_current_task(void){
    return current_task;
}

int scheduler_other_running_tasks(void){
    if (!ready_queue) return 0;
    int count = 0;
    task_t *t = ready_queue->next;
    do {
        if (t != current_task && t->state == TASK_RUNNING){
            count++;
        }
        t = t->next;
    } while (t != ready_queue->next);
    return count;
}

int scheduler_user_range_valid(const void *ptr, uint32_t length){
    if (!current_task || !current_task->user || ptr == NULL) return 0;
    uint32_t start = (uint32_t)ptr;
    uint32_t end = start + length;
    if (end < start) return 0;

    // userspace code, data, bss (0x07000000 .. 0x09000000)
    if (start >= 0x07000000 && end <= 0x09000000) return 1;

    // user heap
    if (current_task->heap_start && start >= current_task->heap_start && end <= current_task->heap_end) return 1;

    // user stack
    if (current_task->user_stack_base) {
        uint32_t stack_start = (uint32_t)current_task->user_stack_base;
        uint32_t stack_end = stack_start + 65536;
        if (start >= stack_start && end <= stack_end) return 1;
    }

    // kernel/boot rodata
    if (start < 0x1000000) return 1;

    return 0;
}

task_t *find_task(uint32_t pid){
    if (!ready_queue) return NULL;
    task_t *t = ready_queue->next;
    do {
        if (t->pid == pid) return t;
        t = t->next;
    } while (t != ready_queue->next);
    return NULL;
}

int alloc_fd(task_t *task, struct file *f){
    for (int i = 3; i < MAX_FDS; i++){
        if (!task->fds[i]){
            task->fds[i] = f;
            return i;
        }
    }
    return -1;
}

void release_fd(task_t *task, int fd){
    if (fd < 0 || fd >= MAX_FDS) return;
    struct file *f = task->fds[fd];
    if (!f) return;
    f->ref_count--;
    if (f->ref_count <= 0){
        if (f->pipe){
            if (f->pipe_end == 0) f->pipe->read_ref--;
            else f->pipe->write_ref--;
            if (f->pipe->read_ref <= 0 && f->pipe->write_ref <= 0){
                kfree(f->pipe->buffer);
                kfree(f->pipe);
            }
        } else if (f->node){
            vfs_close(f->node);
        }
        kfree(f);
    }
    task->fds[fd] = NULL;
}

// poke dad awake when the kid is done
static void unblock_parent_of(task_t *child) {
    if (!child || !ready_queue) return;
    uint32_t ppid = child->ppid;
    if (ppid == 0) return;
    task_t *t = ready_queue->next;
    do {
        if (t->pid == ppid && t->state == TASK_BLOCKED) {
            t->state = TASK_RUNNING;
            break;
        }
        t = t->next;
    } while (t != ready_queue->next);
}

void task_unblock(uint32_t pid) {
    if (!ready_queue || pid == 0) return;
    task_t *t = ready_queue->next;
    do {
        if (t->pid == pid && t->state == TASK_BLOCKED) {
            t->state = TASK_RUNNING;
            break;
        }
        t = t->next;
    } while (t != ready_queue->next);
}

// fork not a spoon
// no mmu so everybody shares the same user memory lol
// copying heap just leaves stale pointers everywhere + leaks 1mb a fork
// so kid shares heap, gets its own stack copy, dad naps till kid execs or dies
int fork_task(struct interrupt_frame *frame){
    uint32_t *stack = (uint32_t*)kmalloc(4096);
    if (!stack){
        return -1;
    }

    unsigned int offset = (unsigned int)frame - (unsigned int)current_task->stack_base;
    if (offset >= 4096) {
        kfree(stack);
        return -1;
    }
    for (unsigned int i = 0; i < 4096; i++){
        ((char*)stack)[i] = ((char*)current_task->stack_base)[i];
    }
    struct interrupt_frame *child_frame = (struct interrupt_frame*)((unsigned int)stack + offset);
    child_frame->eax = 0;

    task_t *task = (task_t*)kmalloc(sizeof(task_t));
    if (!task){
        kfree(stack);
        return -1;
    }

    task->pid = next_pid++;
    task->ppid = current_task->pid;
    task->frame = child_frame;
    task->stack_base = stack;
    // kid gets its own stack copy so it cant trash dads frames
    // heap stays shared so heap pointers dont go stale (see below)
    task->user_stack_base = current_task->user_stack_base;
    task->user_stack_top = current_task->user_stack_top;
    task->next = NULL;
    task->user = current_task->user;
    task->state = TASK_RUNNING;
    task->exit_code = 0;

    if (current_task->user_stack_base) {
        uint32_t stack_size = current_task->user_stack_top - (uint32_t)current_task->user_stack_base;
        if (stack_size == 0 || stack_size > 65536) stack_size = 16384;
        void *new_ustack = kmalloc(stack_size);
        if (!new_ustack) {
            kfree(stack);
            kfree(task);
            return -1;
        }
        for (unsigned int i = 0; i < stack_size; i++) {
            ((char*)new_ustack)[i] = ((char*)current_task->user_stack_base)[i];
        }
        uint32_t stack_delta = (uint32_t)new_ustack - (uint32_t)current_task->user_stack_base;
        task->user_stack_base = new_ustack;
        task->user_stack_top = current_task->user_stack_top + stack_delta;
        // only shift pointers that actually live in the old stack
        // shifting everything blindly corrupts shit like a heap ebp
        uint32_t old_base = (uint32_t)current_task->user_stack_base;
        uint32_t old_top = current_task->user_stack_top;
        if (frame->useresp >= old_base && frame->useresp <= old_top) {
            child_frame->useresp = frame->useresp + stack_delta;
        }
        if (frame->ebp >= old_base && frame->ebp <= old_top) {
            child_frame->ebp = frame->ebp + stack_delta;
        }
    }

    for (int i = 0; i < MAX_FDS; i++){
        task->fds[i] = current_task->fds[i];
        if (task->fds[i]) task->fds[i]->ref_count++;
    }

    for (int i = 0; i < 256; i++){
        task->cwd[i] = current_task->cwd[i];
    }

    // share heap, dont copy that shit (stale pointers + 1mb leak per fork)
    task->heap_start = current_task->heap_start;
    task->heap_brk = current_task->heap_brk;
    task->heap_end = current_task->heap_end;

    add_task(task);
    // dad naps till kid execs or dies
    current_task->state = TASK_BLOCKED;
    return (int)task->pid;
}

int wait4(int pid, int *status, int options){
    while (1){
        task_t *found = NULL;
        int has_child = 0;
        if (ready_queue){
            task_t *t = ready_queue->next;
            do {
                if (t->ppid == current_task->pid && (pid == -1 || (uint32_t)pid == t->pid)){
                    has_child = 1;
                    if (t->state == TASK_ZOMBIE){
                        found = t;
                        break;
                    }
                }
                t = t->next;
            } while (t != ready_queue->next);
        }
        if (!has_child){
            return -1;
        }
        if (found){
            if (status) *status = found->exit_code;
            int child_pid = (int)found->pid;
            remove_task(found);
            kfree(found->stack_base);
            // stack might be shared with dad so only free it if its actually the kids own
            if (found->user_stack_base && found->user_stack_base != current_task->user_stack_base) kfree(found->user_stack_base);
            // heap is shared, hands off (dad still uses it)
            kfree(found);
            return child_pid;
        }
        if (options & 1) { // WNOHANG
            return 0;
        }
        return -2;
    }
}

void task_exit(int status){
    if (!current_task) return;
    current_task->state = TASK_ZOMBIE;
    current_task->exit_code = status;
    unblock_parent_of(current_task);
    if (ready_queue) {
        task_t *t = ready_queue->next;
        do {
            if (t->ppid == current_task->pid) t->ppid = current_task->ppid;
            t = t->next;
        } while (t != ready_queue->next);
    }
    for (int i = 0; i < MAX_FDS; i++){
        if (current_task->fds[i]) release_fd(current_task, i);
    }
}
