#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define BAR_WIDTH 30

// slurp a file into buf
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

static void bar(unsigned long used, unsigned long total, int width){
    if (total == 0) { printf("[%-*s]", width, ""); return; }
    int filled = (int)(used * (unsigned long)width / total);
    if (filled > width) filled = width;
    int pct = (int)(used * 100 / total);
    putchar('[');
    for (int i = 0; i < filled; i++)  putchar('|');
    for (int i = filled; i < width; i++) putchar('.');
    printf("] %3d%%", pct);
}

int main(void){
    char buf[4096];

    if (slurp("/proc/meminfo", buf, sizeof(buf)) <= 0) {
        fprintf(stderr, "memstat: can't read /proc/meminfo\n");
        return 1;
    }
    unsigned long total = meminfo_kv(buf, "MemTotal");
    unsigned long free  = meminfo_kv(buf, "MemFree");
    unsigned long avail = meminfo_kv(buf, "MemAvailable");
    unsigned long used  = total > free ? total - free : 0;

    printf("=== Memory ===\n");
    printf("Total:     %8lu kB\n", total);
    printf("Used:      %8lu kB  ", used);
    bar(used, total, BAR_WIDTH);
    printf("\n");
    printf("Free:      %8lu kB\n", free);
    printf("Available: %8lu kB\n\n", avail);

    printf("--- /proc/meminfo ---\n%s", buf);
    return 0;
}
