#include <stdint.h>

void sysinfo_init(void);
void kern_set_mem_total_kb(unsigned int kb);
void kern_set_initrd_bytes(unsigned int bytes);
unsigned int kern_mem_total_kb(void);
unsigned int kern_mem_free_kb(void);
unsigned int kern_uptime_sec(void);
const char *kern_cpu_vendor(void);   // e.g. "GenuineIntel"
const char *kern_cpu_brand(void);    // e.g. "QEMU Virtual CPU version 2.5+"
unsigned int kern_cpu_family(void);
unsigned int kern_cpu_model(void);
unsigned int kern_cpu_stepping(void);
const char *kern_dmi_vendor(void);   // system manufacturer (SMBIOS type 1)
const char *kern_dmi_product(void);  // product name
const char *kern_dmi_version(void);  // product version

struct task; // task_t, full definition in include/sched.h

// /proc top-level file slots
#define PROC_MEMINFO 0
#define PROC_CPUINFO 1
#define PROC_UPTIME 2
#define PROC_VERSION 3
#define PROC_LOADAVG 4
#define PROC_STAT 5
#define NPROC_FILES 6
// /proc/<pid> file slots
#define PID_STAT 0
#define PID_CMDLINE 1
#define NPID_FILES 2
// /sys/devices/virtual/dmi/id slots
#define SYS_VENDOR 0
#define SYS_PRODUCT 1
#define SYS_VERSION 2
#define NSYS_FILES 3

// render a synthetic file into buf (cap bytes); returns length or -1.
int proc_gen_file(int slot, char *buf, unsigned int cap);
int proc_gen_pidfile(struct task *t, int slot, char *buf, unsigned int cap);
int sys_gen_file(int slot, char *buf, unsigned int cap);
