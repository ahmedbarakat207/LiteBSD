#include <stdint.h>

struct interrupt_frame;

typedef struct task {
    uint32_t pid;    // process id
    struct interrupt_frame *frame;
    void *stack_base;
    struct task *next;
} task_t;

void sched_init();
int create_task(void (*entry)(void));
struct interrupt_frame *schedule(struct interrupt_frame *frame);