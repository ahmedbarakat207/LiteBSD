#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

#define MAX_PROCS 64
#define BAR_WIDTH  20

struct proc_entry {
    int   pid;
    int   ppid;
    char  state;
    char  comm[17];
    unsigned long utime;
    unsigned long vsize;
    unsigned long rss;
};

// slurp a small file into buf; returns bytes read or -1
static int slurp(const char *path, char *buf, int cap){
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    int n = read(fd, buf, cap - 1);
    close(fd);
    if (n < 0) n = 0;
    buf[n] = '\0';
    return n;
}

static unsigned long parse_ul(const char *s){
    unsigned long v = 0;
    while (*s == ' ') s++;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return v;
}

// pull "key: value kB" lines from /proc/meminfo
static unsigned long meminfo_kv(const char *buf, const char *key){
    const char *p = buf;
    int klen = strlen(key);
    while (*p) {
        if (strncmp(p, key, klen) == 0) {
            p += klen;
            while (*p == ' ' || *p == ':') p++;
            return parse_ul(p);
        }
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    return 0;
}

// fill entry from /proc/<pid>/stat
static int parse_pidstat(int pid, struct proc_entry *e){
    char path[64], buf[512];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    if (slurp(path, buf, sizeof(buf)) <= 0) return -1;
    e->pid = pid;

    // format: pid (comm) state ppid pgrp sess ... utime ... vsize rss ...
    const char *p = buf;
    while (*p && *p != '(') p++;
    if (!*p) return -1;
    p++; // skip '('
    int i = 0;
    while (*p && *p != ')' && i < 16) e->comm[i++] = *p++;
    e->comm[i] = '\0';
    if (*p) p++; // skip ')'
    while (*p == ' ') p++;
    e->state = *p++;
    while (*p == ' ') p++;
    e->ppid = (int)parse_ul(p);
    // skip pgrp sess tty tpgid flags minflt cminflt majflt cmajflt = 9 fields
    for (int f = 0; f < 9; f++) {
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
    }
    e->utime = parse_ul(p);
    // skip stime cutime cstime prio nice threads itreal starttime = 8 fields then vsize rss
    for (int f = 0; f < 8; f++) {
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
    }
    e->vsize = parse_ul(p);
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    e->rss = parse_ul(p);
    return 0;
}

static void bar(unsigned long used, unsigned long total, int width){
    if (total == 0) { printf("[%-*s]", width, ""); return; }
    int filled = (int)(used * (unsigned long)width / total);
    if (filled > width) filled = width;
    putchar('[');
    for (int i = 0; i < filled; i++)  putchar('|');
    for (int i = filled; i < width; i++) putchar(' ');
    putchar(']');
}

// "1d 02:03:04" style from seconds
static void fmt_uptime(unsigned long s){
    unsigned long d = s / 86400; s %= 86400;
    unsigned long h = s / 3600;  s %= 3600;
    unsigned long m = s / 60;    s %= 60;
    if (d) printf("%lud %02lu:%02lu:%02lu", d, h, m, s);
    else   printf("%02lu:%02lu:%02lu", h, m, s);
}

int main(void){
    char buf[2048];

    // uptime
    unsigned long uptime_sec = 0;
    if (slurp("/proc/uptime", buf, sizeof(buf)) > 0)
        uptime_sec = parse_ul(buf);

    // loadavg
    char loadavg[32] = "0.00 0.00 0.00";
    if (slurp("/proc/loadavg", buf, sizeof(buf)) > 0) {
        int i = 0;
        while (buf[i] && buf[i] != '\n' && i < 31) { loadavg[i] = buf[i]; i++; }
        loadavg[i] = '\0';
    }

    // mem
    unsigned long mem_total = 0, mem_free = 0;
    if (slurp("/proc/meminfo", buf, sizeof(buf)) > 0) {
        mem_total = meminfo_kv(buf, "MemTotal");
        mem_free  = meminfo_kv(buf, "MemFree");
    }
    unsigned long mem_used = mem_total > mem_free ? mem_total - mem_free : 0;

    // proc table
    struct proc_entry procs[MAX_PROCS];
    int nprocs = 0;
    DIR *d = opendir("/proc");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) && nprocs < MAX_PROCS) {
            const char *n = de->d_name;
            if (n[0] < '1' || n[0] > '9') continue;
            int pid = atoi(n);
            if (pid <= 0) continue;
            if (parse_pidstat(pid, &procs[nprocs]) == 0)
                nprocs++;
        }
        closedir(d);
    }

    // header
    printf("top - up ");
    fmt_uptime(uptime_sec);
    printf(", load: %s\n", loadavg);
    printf("Tasks: %d total\n", nprocs);
    printf("Mem: ");
    bar(mem_used, mem_total, BAR_WIDTH);
    printf(" %lu/%lu kB\n\n", mem_used, mem_total);

    // column header
    printf("%-6s %-6s %-4s %8s %8s  %s\n",
           "PID", "PPID", "S", "VSIZE", "RSS", "COMMAND");
    printf("%-6s %-6s %-4s %8s %8s  %s\n",
           "------", "------", "----", "--------", "--------", "-------");

    for (int i = 0; i < nprocs; i++) {
        struct proc_entry *e = &procs[i];
        printf("%-6d %-6d %-4c %8lu %8lu  %s\n",
               e->pid, e->ppid, e->state,
               e->vsize / 1024, e->rss * 4,
               e->comm);
    }
    return 0;
}
