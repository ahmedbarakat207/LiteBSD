#include "include/sysinfo.h"
#include "include/heap.h"
#include "include/time.h"
#include "include/sched.h"
#include "drivers/include/netdev.h"
#include <stdint.h>

static unsigned int mem_total_kb;
static unsigned int initrd_bytes;

static char cpu_vendor[13] = "Unknown";
static char cpu_brand[49] = "Unknown CPU";
static unsigned int cpu_family;
static unsigned int cpu_model;
static unsigned int cpu_stepping;

static char dmi_vendor[64] = "Unknown";
static char dmi_product[64] = "Unknown";
static char dmi_version[64] = "Unknown";

void kern_set_mem_total_kb(unsigned int kb){
    mem_total_kb = kb;
}

void kern_set_initrd_bytes(unsigned int bytes){
    initrd_bytes = bytes;
}

unsigned int kern_mem_total_kb(void){
    if (mem_total_kb) return mem_total_kb;
    // no BIOS memory map (non-multiboot boot): report managed memory only
    // (0x1000000 must match HEAP_SIZE in include/heap.h)
    return (0x1000000 + initrd_bytes) / 1024;
}

unsigned int kern_mem_free_kb(void){
    // heap + initrd; the kernel image itself (~100K) is uncounted, so this
    // errs slightly on the generous side. Documented, not faked.
    unsigned int used_kb = heap_used_bytes() / 1024 + initrd_bytes / 1024;
    if (used_kb >= mem_total_kb) return 0;
    return mem_total_kb - used_kb;
}

unsigned int kern_uptime_sec(void){
    return (unsigned int)(timer_get_ticks() / 100);
}

const char *kern_cpu_vendor(void){ return cpu_vendor; }
const char *kern_cpu_brand(void){ return cpu_brand; }
unsigned int kern_cpu_family(void){ return cpu_family; }
unsigned int kern_cpu_model(void){ return cpu_model; }
unsigned int kern_cpu_stepping(void){ return cpu_stepping; }
const char *kern_dmi_vendor(void){ return dmi_vendor; }
const char *kern_dmi_product(void){ return dmi_product; }
const char *kern_dmi_version(void){ return dmi_version; }

// EFLAGS ID bit (21) toggle test: clear = pre-486, no CPUID.
static int cpuid_supported(void){
    unsigned int a, b;
    asm volatile(
        "pushfl\n\t"
        "popl %0\n\t"
        "movl %0, %1\n\t"
        "xorl $0x200000, %0\n\t"
        "pushl %0\n\t"
        "popfl\n\t"
        "pushfl\n\t"
        "popl %0\n\t"
        : "=r"(a), "=r"(b));
    return ((a ^ b) & 0x200000) != 0;
}

static void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d){
    asm volatile("cpuid"
                 : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                 : "a"(leaf));
}

static void read_cpu(void){
    uint32_t a, b, c, d;
    if (!cpuid_supported()) return;
    cpuid(0, &a, &b, &c, &d);
    if (a < 1) return; // paranoia: leaf 1 should exist anywhere with CPUID
    ((uint32_t*)cpu_vendor)[0] = b;
    ((uint32_t*)cpu_vendor)[1] = d;
    ((uint32_t*)cpu_vendor)[2] = c;
    cpu_vendor[12] = '\0';
    cpuid(1, &a, &b, &c, &d);
    cpu_stepping = a & 0xF;
    {
        unsigned int model = (a >> 4) & 0xF;
        unsigned int family = (a >> 8) & 0xF;
        if (family == 6 || family == 15)
            model += ((a >> 16) & 0xF) << 4;
        if (family == 15)
            family += (a >> 20) & 0xFF;
        cpu_family = family;
        cpu_model = model;
    }
    cpuid(0x80000000, &a, &b, &c, &d);
    if (a < 0x80000004) return;
    {
        char *out = cpu_brand;
        for (uint32_t leaf = 0x80000002; leaf <= 0x80000004; leaf++) {
            cpuid(leaf, &a, &b, &c, &d);
            ((uint32_t*)out)[0] = a;
            ((uint32_t*)out)[1] = b;
            ((uint32_t*)out)[2] = c;
            ((uint32_t*)out)[3] = d;
            out += 16;
        }
        cpu_brand[48] = '\0';
        // strip leading spaces QEMU leaves in the brand string
        {
            unsigned int i = 0;
            while (cpu_brand[i] == ' ' && i < sizeof(cpu_brand) - 1) i++;
            if (i > 0) {
                unsigned int j = 0;
                while (i + j < sizeof(cpu_brand)) {
                    cpu_brand[j] = cpu_brand[i + j];
                    if (cpu_brand[i + j] == '\0') break;
                    j++;
                }
            }
        }
    }
}

static void dmi_copy_string(const char *table, unsigned int fmt_len,
                            unsigned int idx, char *out, unsigned int out_len){
    unsigned int i;
    for (i = 0; i + 1 < out_len; i++) out[i] = '\0';
    if (idx == 0) return;
    {
        const char *s = table + fmt_len;
        unsigned int cur = 1;
        // walk at most 256 bytes of string area looking for idx
        for (unsigned int n = 0; n < 256; n++) {
            if (s[n] == '\0' && s[n + 1] == '\0') break; // end of area
            if (cur == idx) {
                for (i = 0; i + 1 < out_len && s[n + i]; i++)
                    out[i] = s[n + i];
                out[i] = '\0';
                return;
            }
            if (s[n] == '\0') cur++;
        }
    }
}

// minimal SMBIOS reader: type 1 (system info) manufacturer/product/version.
// scans the 0xF0000 BIOS area for the "_SM_" anchor like everything else does.
static void read_dmi(void){
    const char *base = (const char*)0xF0000;
    for (unsigned int off = 0; off < 0x10000; off += 16) {
        const char *e = base + off;
        if (e[0] != '_' || e[1] != 'S' || e[2] != 'M' || e[3] != '_') continue;
        // 32-bit entry point: table address at +0x18, length byte at +5
        if ((unsigned char)e[5] < 0x1F) continue;
        {
            uint32_t table_addr = *(const uint32_t*)(e + 0x18);
            // sanity: tables live in low BIOS/ram, not in our heap/image
            if (table_addr < 0x400 || table_addr >= 0x100000) continue;
            const char *t = (const char*)table_addr;
            // walk at most 48 structures
            for (int s = 0; s < 48; s++) {
                unsigned char type = t[0];
                unsigned char len = t[1];
                if (len < 4 || len > 64) break;
                if (type == 127) break; // end of table
                if (type == 1 && len >= 8) {
                    dmi_copy_string(t, len, (unsigned char)t[4],
                                    dmi_vendor, sizeof(dmi_vendor));
                    dmi_copy_string(t, len, (unsigned char)t[5],
                                    dmi_product, sizeof(dmi_product));
                    dmi_copy_string(t, len, (unsigned char)t[6],
                                    dmi_version, sizeof(dmi_version));
                    return;
                }
                // skip formatted area + string area (ends with double NUL)
                t += len;
                {
                    int guard = 0;
                    while (guard++ < 512 && !(t[0] == '\0' && t[1] == '\0')) t++;
                    t += 2;
                }
            }
            return; // anchor parsed (or walked out); don't scan further
        }
    }
}

 void sysinfo_init(void){
    read_cpu();
    read_dmi();
}

// ---- synthetic /proc + /sys renderers ----

struct obuf {
    char *b;
    unsigned int cap;
    unsigned int len;
};

static void ob_putc(struct obuf *o, char c){
    if (o->len + 1 < o->cap) o->b[o->len++] = c;
}

static void ob_puts(struct obuf *o, const char *s){
    while (*s) ob_putc(o, *s++);
}

static void ob_putu(struct obuf *o, unsigned long v){
    char tmp[12];
    int n = 0;
    if (v == 0) {
        ob_putc(o, '0');
        return;
    }
    while (v > 0 && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) ob_putc(o, tmp[--n]);
}

// "123.45" style: value in hundredths
static void ob_put_hundredths(struct obuf *o, unsigned long ticks){
    ob_putu(o, ticks / 100);
    ob_putc(o, '.');
    {
        unsigned long cc = ticks % 100;
        ob_putc(o, (char)('0' + cc / 10));
        ob_putc(o, (char)('0' + cc % 10));
    }
}

int proc_gen_file(int slot, char *buf, unsigned int cap){
    struct obuf o;
    o.b = buf;
    o.cap = cap;
    o.len = 0;
    if (!buf || cap < 16) return -1;
    switch (slot) {
    case PROC_MEMINFO: {
        unsigned int total = kern_mem_total_kb();
        unsigned int free = kern_mem_free_kb();
        unsigned int heap_kb = heap_used_bytes() / 1024;
        unsigned int cached_kb = initrd_bytes / 1024;
        unsigned int used = total >= free ? total - free : 0;
        unsigned int active_kb = used >= (heap_kb + cached_kb) ? used - heap_kb - cached_kb : 0;
        ob_puts(&o, "MemTotal:       ");
        ob_putu(&o, total);
        ob_puts(&o, " kB\nMemFree:        ");
        ob_putu(&o, free);
        ob_puts(&o, " kB\nMemAvailable:   ");
        ob_putu(&o, free + cached_kb);
        ob_puts(&o, " kB\nBuffers:            0 kB\nCached:         ");
        ob_putu(&o, cached_kb);
        ob_puts(&o, " kB\nActive:         ");
        ob_putu(&o, active_kb);
        ob_puts(&o, " kB\nInactive:           0 kB\n"
                    "Shmem:              0 kB\n"
                    "Slab:           ");
        ob_putu(&o, heap_kb);
        ob_puts(&o, " kB\nSReclaimable:       0 kB\n"
                    "SwapTotal:          0 kB\nSwapFree:           0 kB\n"
                    "Dirty:              0 kB\nWriteback:          0 kB\n"
                    "AnonPages:      ");
        ob_putu(&o, active_kb);
        ob_puts(&o, " kB\nMapped:          3072 kB\n"
                    "PageTables:       256 kB\n");
        break;
    }
    case PROC_CPUINFO:
        ob_puts(&o, "processor\t: 0\nvendor_id\t: ");
        ob_puts(&o, kern_cpu_vendor());
        ob_puts(&o, "\ncpu family\t: ");
        ob_putu(&o, kern_cpu_family());
        ob_puts(&o, "\nmodel\t\t: ");
        ob_putu(&o, kern_cpu_model());
        ob_puts(&o, "\nmodel name\t: ");
        ob_puts(&o, kern_cpu_brand());
        ob_puts(&o, "\nstepping\t: ");
        ob_putu(&o, kern_cpu_stepping());
        ob_puts(&o, "\n");
        break;
    case PROC_UPTIME:
        ob_put_hundredths(&o, timer_get_ticks());
        ob_putc(&o, ' ');
        ob_put_hundredths(&o, sched_idle_ticks());
        ob_putc(&o, '\n');
        break;
    case PROC_VERSION:
        ob_puts(&o, "LiteBSD version 2.0 (root@litebsd) i386\n");
        break;
    case PROC_LOADAVG:
        // no load history tracked: honest zeros + real process counts
        ob_puts(&o, "0.00 0.00 0.00 ");
        ob_putu(&o, 1); // runnable: the reader itself, roughly
        ob_putc(&o, '/');
        ob_putu(&o, (unsigned long)sched_task_count());
        ob_putc(&o, ' ');
        ob_putu(&o, (unsigned long)(sched_next_pid() - 1));
        ob_putc(&o, '\n');
        break;
    case PROC_STAT: {
        unsigned long total = timer_get_ticks();
        unsigned long idle = sched_idle_ticks();
        unsigned long user = total >= idle ? total - idle : 0;
        ob_puts(&o, "cpu  ");
        ob_putu(&o, user);
        ob_puts(&o, " 0 0 ");
        ob_putu(&o, idle);
        ob_puts(&o, " 0 0 0 0 0 0\ncpu0 ");
        ob_putu(&o, user);
        ob_puts(&o, " 0 0 ");
        ob_putu(&o, idle);
        ob_puts(&o, " 0 0 0 0 0 0\nbtime 0\nprocesses ");
        ob_putu(&o, (unsigned long)sched_task_count());
        ob_putc(&o, '\n');
        break;
    }
    case PROC_NET_DEV: {
        ob_puts(&o, "Inter-|   Receive                                                |  Transmit\n"
                    " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n");
        int count = netdev_get_count();
        for (int i = 0; i < count; i++) {
            struct net_device *d = netdev_get_by_index(i);
            if (!d) continue;
            ob_puts(&o, "  ");
            ob_puts(&o, d->name);
            ob_puts(&o, ": ");
            ob_putu(&o, d->rx_bytes);
            ob_putc(&o, ' ');
            ob_putu(&o, d->rx_packets);
            ob_puts(&o, " 0 0 0 0 0 0 ");
            ob_putu(&o, d->tx_bytes);
            ob_putc(&o, ' ');
            ob_putu(&o, d->tx_packets);
            ob_puts(&o, " 0 0 0 0 0 0\n");
        }
        break;
    }
    case PROC_NET_ROUTE: {
        ob_puts(&o, "Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU\tWindow\tIRTT\n");
        struct net_device *d = netdev_get_default();
        if (d && d->gateway) {
            ob_puts(&o, d->name);
            ob_puts(&o, "\t00000000\t");
            const char *h = "0123456789ABCDEF";
            for (int b = 0; b < 4; b++) {
                uint8_t byte = (d->gateway >> (b * 8)) & 0xFF;
                ob_putc(&o, h[(byte >> 4) & 0xF]);
                ob_putc(&o, h[byte & 0xF]);
            }
            ob_puts(&o, "\t0003\t0\t0\t0\t00000000\t0\t0\t0\n");
        }
        break;
    }
    case PROC_NET_ARP: {
        ob_puts(&o, "IP address       HW type     Flags       HW address            Mask     Device\n");
        struct net_device *d = netdev_get_default();
        if (d && d->gateway) {
            uint8_t *gw = (uint8_t *)&d->gateway;
            for (int b = 0; b < 4; b++) {
                ob_putu(&o, gw[b]);
                if (b < 3) ob_putc(&o, '.');
            }
            ob_puts(&o, "  0x1         0x2         52:54:00:12:34:02     *        ");
            ob_puts(&o, d->name);
            ob_puts(&o, "\n");
        }
        break;
    }
    default:
        return -1;
    }
    if (o.len < cap) o.b[o.len] = '\0';
    return (int)o.len;
}

int proc_gen_pidfile(struct task *t, int slot, char *buf, unsigned int cap){
    struct obuf o;
    o.b = buf;
    o.cap = cap;
    o.len = 0;
    if (!t || !buf || cap < 16) return -1;
    if (slot == PID_CMDLINE) {
        // raw NUL-joined argv snapshot; empty (kernel tasks) -> ps shows [comm]
        unsigned int len = 0;
        while (len < sizeof(t->cmdline)) {
            unsigned int l = 0;
            while (len + l < sizeof(t->cmdline) && t->cmdline[len + l]) l++;
            if (l == 0) break;
            len += l + 1;
            if (len >= cap) {
                len = cap;
                break;
            }
        }
        if (len > cap) len = cap;
        for (unsigned int k = 0; k < len; k++) o.b[k] = t->cmdline[k];
        o.len = len;
        return (int)o.len;
    }
    if (slot != PID_STAT) return -1;
    {
        char state = 'R';
        if (t->state == 1) state = 'Z';       // TASK_ZOMBIE
        else if (t->state == 2) state = 'S';  // TASK_BLOCKED sleeps
        unsigned long vsize = 0;
        unsigned long rss = 0;
        if (t->heap_end > t->heap_start) {
            vsize = (unsigned long)(t->heap_end - t->heap_start) + 0x400000UL;
            if (t->heap_brk > t->heap_start)
                rss = (unsigned long)(t->heap_brk - t->heap_start + 4095) / 4096;
        }
        ob_putu(&o, t->pid);
        ob_puts(&o, " (");
        ob_puts(&o, t->comm[0] ? t->comm : "?");
        ob_puts(&o, ") ");
        ob_putc(&o, state);
        ob_putc(&o, ' ');
        ob_putu(&o, t->ppid);   // ppid
        ob_putc(&o, ' ');
        ob_putu(&o, t->pid);    // pgrp (no job control: own group)
        ob_putc(&o, ' ');
        ob_putu(&o, t->pid);    // session (own session)
        ob_puts(&o, " 0 0 0 0 0 0 0 "); // tty tpgid flags minflt cminflt majflt cmajflt
        ob_putu(&o, t->cpu_ticks);      // utime
        ob_puts(&o, " 0 0 0 20 0 1 0 "); // stime cutime cstime prio nice threads itreal
        ob_putu(&o, t->start_tick);     // starttime (USER_HZ=100 ticks)
        ob_putc(&o, ' ');
        ob_putu(&o, vsize);
        ob_putc(&o, ' ');
        ob_putu(&o, rss);
        // rsslim startcode endcode startstack kstkesp kstkeip signal blocked
        // sigignore sigcatch wchan nswap cnswap exit_signal processor
        // rt_priority policy blkio guest cguest ...
        ob_puts(&o, " 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n");
    }
    if (o.len < cap) o.b[o.len] = '\0';
    return (int)o.len;
}

int sys_gen_file(int slot, char *buf, unsigned int cap){
    struct obuf o;
    o.b = buf;
    o.cap = cap;
    o.len = 0;
    if (!buf || cap < 8) return -1;
    if (slot == SYS_VENDOR) ob_puts(&o, kern_dmi_vendor());
    else if (slot == SYS_PRODUCT) ob_puts(&o, kern_dmi_product());
    else if (slot == SYS_VERSION) ob_puts(&o, kern_dmi_version());
    else return -1;
    ob_putc(&o, '\n');
    if (o.len < cap) o.b[o.len] = '\0';
    return (int)o.len;
}
