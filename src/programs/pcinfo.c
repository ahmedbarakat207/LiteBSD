#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

// slurp a file into buf; returns bytes read or -1
static int slurp(const char *path, char *buf, int cap){
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    int n = read(fd, buf, cap - 1);
    close(fd);
    if (n < 0) n = 0;
    buf[n] = '\0';
    // strip trailing newline
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
    return n;
}

// grab value of "key\t: value" from cpuinfo blob
static void cpuinfo_field(const char *blob, const char *key, char *out, int cap){
    const char *p = blob;
    int klen = strlen(key);
    out[0] = '\0';
    while (*p) {
        if (strncmp(p, key, klen) == 0) {
            const char *v = p + klen;
            while (*v == '\t' || *v == ' ' || *v == ':') v++;
            int i = 0;
            while (*v && *v != '\n' && i < cap - 1) out[i++] = *v++;
            out[i] = '\0';
            return;
        }
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
}

int main(void){
    char buf[4096], tmp[128];

    printf("=== LiteBSD System Information ===\n\n");

    // kernel version
    if (slurp("/proc/version", buf, sizeof(buf)) > 0)
        printf("Kernel:      %s\n", buf);

    // uptime
    if (slurp("/proc/uptime", buf, sizeof(buf)) > 0) {
        unsigned long s = 0;
        const char *p = buf;
        while (*p >= '0' && *p <= '9') s = s * 10 + (*p++ - '0');
        unsigned long h = s / 3600; s %= 3600;
        unsigned long m = s / 60;   s %= 60;
        printf("Uptime:      %02luh %02lum %02lus\n", h, m, s);
    }

    // load
    if (slurp("/proc/loadavg", buf, sizeof(buf)) > 0)
        printf("Load avg:    %s\n", buf);

    printf("\n--- CPU ---\n");
    if (slurp("/proc/cpuinfo", buf, sizeof(buf)) > 0) {
        cpuinfo_field(buf, "vendor_id",  tmp, sizeof(tmp)); printf("Vendor:      %s\n", tmp);
        cpuinfo_field(buf, "model name", tmp, sizeof(tmp)); printf("Model:       %s\n", tmp);
        cpuinfo_field(buf, "cpu family", tmp, sizeof(tmp)); printf("Family:      %s\n", tmp);
        cpuinfo_field(buf, "model",      tmp, sizeof(tmp)); printf("Model#:      %s\n", tmp);
        cpuinfo_field(buf, "stepping",   tmp, sizeof(tmp)); printf("Stepping:    %s\n", tmp);
    }

    printf("\n--- Hardware (SMBIOS type 1) ---\n");
    if (slurp("/sys/devices/virtual/dmi/id/sys_vendor", buf, sizeof(buf)) > 0)
        printf("Vendor:      %s\n", buf);
    if (slurp("/sys/devices/virtual/dmi/id/product_name", buf, sizeof(buf)) > 0)
        printf("Product:     %s\n", buf);
    if (slurp("/sys/devices/virtual/dmi/id/product_version", buf, sizeof(buf)) > 0)
        printf("Version:     %s\n", buf);

    printf("\n--- Memory ---\n");
    if (slurp("/proc/meminfo", buf, sizeof(buf)) > 0) {
        // just print the first three lines (MemTotal, MemFree, MemAvailable)
        const char *p = buf;
        for (int line = 0; line < 3 && *p; line++) {
            const char *start = p;
            while (*p && *p != '\n') p++;
            printf("  %.*s\n", (int)(p - start), start);
            if (*p) p++;
        }
    }

    return 0;
}
