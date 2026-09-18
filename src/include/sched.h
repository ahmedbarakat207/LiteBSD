#include <stdint.h>

struct interrupt_frame;
struct vfs_node;
struct pipe;

#define MAX_FDS 16
#define TASK_RUNNING 0
#define TASK_ZOMBIE 1
#define TASK_BLOCKED 2 // dad naps till kid execs or dies

struct socket;

struct file {
    struct vfs_node *node;
    unsigned int offset;
    int flags;
    int ref_count;
    struct pipe *pipe;
    int pipe_end;
    struct socket *sock;
};

struct pipe {
    char *buffer;
    unsigned int size;
    unsigned int read_pos;
    unsigned int write_pos;
    unsigned int count;
    int read_ref;
    int write_ref;
};

typedef struct task {
    uint32_t pid;    // process id
    uint32_t ppid;
    struct interrupt_frame *frame;
    void *stack_base;
    void *user_stack_base;
    uint32_t user_stack_top;
    struct task *next;
    uint8_t user; // user mode
    uint8_t state;
    int exit_code;
    struct file *fds[MAX_FDS];
    char cwd[256];
    uint32_t heap_start;
    uint32_t heap_brk;
    uint32_t heap_end;
    unsigned long cpu_ticks;  // timer ticks charged while RUNNING
    unsigned long start_tick; // creation tick (USER_HZ=100, matches /proc)
    char comm[16];            // short name for /proc (from exec basename)
    char cmdline[256];        // NUL-joined argv snapshot for /proc
    uint32_t img_size;        // bytes of user image in use (from last exec)
    void    *img_snapshot;    // saved image copy made at fork; restored when child exits sans exec
} task_t;

void sched_init();
int create_task(void (*entry)(void));
int create_user_task(void (*entry)(void));
struct interrupt_frame *schedule(struct interrupt_frame *frame);
uint32_t scheduler_current_pid(void);
task_t *scheduler_current_task(void);
int scheduler_user_range_valid(const void *ptr, uint32_t length);
task_t *find_task(uint32_t pid);
int fork_task(struct interrupt_frame *frame);
int wait4(int pid, int *status, int options);
void task_exit(int status);
void task_unblock(uint32_t pid);
int alloc_fd(task_t *task, struct file *f);
void release_fd(task_t *task, int fd);
int scheduler_other_running_tasks(void);
// process accounting for /proc
task_t *sched_task_head(void);
uint32_t sched_next_pid(void);
unsigned long sched_idle_ticks(void);
int sched_task_count(void);
void sched_account_tick(void);
