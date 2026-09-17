#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <stdint.h>

#define FBIOGET_VSCREENINFO 0x4600

struct fb_bitfield {
    uint32_t offset;
    uint32_t length;
    uint32_t msb_right;
};

struct fb_var_screeninfo {
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t bits_per_pixel;
    uint32_t grayscale;
    struct fb_bitfield red;
    struct fb_bitfield green;
    struct fb_bitfield blue;
    struct fb_bitfield transp;
    uint32_t nonstd;
    uint32_t activate;
    uint32_t height;
    uint32_t width;
    uint32_t accel_flags;
    uint32_t pixclock;
    uint32_t left_margin;
    uint32_t right_margin;
    uint32_t upper_margin;
    uint32_t lower_margin;
    uint32_t hsync_len;
    uint32_t vsync_len;
    uint32_t sync;
    uint32_t vmode;
    uint32_t reserved[6];
};

// slurp a file into buf; returns bytes read or -1
static int slurp(const char *path, char *buf, int cap) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    int n = read(fd, buf, cap - 1);
    close(fd);
    if (n < 0) n = 0;
    buf[n] = '\0';
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
    return n;
}

// find value after "key: " or "key=..."
static void extract_field(const char *blob, const char *key, char *out, int cap) {
    const char *p = blob;
    int klen = strlen(key);
    out[0] = '\0';
    while (*p) {
        if (strncmp(p, key, klen) == 0) {
            const char *v = p + klen;
            while (*v == '\t' || *v == ' ' || *v == ':' || *v == '=') v++;
            if (*v == '"') v++;
            int i = 0;
            while (*v && *v != '\n' && *v != '\r' && *v != '"' && i < cap - 1) {
                out[i++] = *v++;
            }
            out[i] = '\0';
            return;
        }
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
}

static int count_packages(void) {
    DIR *d = opendir("/bin");
    if (!d) return 0;
    int count = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] != '.') count++;
    }
    closedir(d);
    return count;
}

int main(void) {
    char buf[4096];
    char user[64], host[64], os[128], machine[128], kernel[128], uptime_str[128];
    char pkgs[64], shell_str[64], res[64], term[64], cpu[128], mem[128];

    // User & Host
    const char *u = getenv("USER");
    snprintf(user, sizeof(user), "%s", u ? u : "root");
    if (slurp("/etc/hostname", host, sizeof(host)) <= 0) {
        snprintf(host, sizeof(host), "litebsd");
    }

    // OS
    if (slurp("/etc/os-release", buf, sizeof(buf)) > 0) {
        extract_field(buf, "PRETTY_NAME", os, sizeof(os));
        if (os[0] == '\0') extract_field(buf, "NAME", os, sizeof(os));
    } else {
        os[0] = '\0';
    }
    if (os[0] == '\0') snprintf(os, sizeof(os), "LiteBSD 2.0 (i386)");

    // Machine / Host
    if (slurp("/sys/devices/virtual/dmi/id/product_name", machine, sizeof(machine)) <= 0) {
        snprintf(machine, sizeof(machine), "PC (i386)");
    }

    // Kernel
    if (slurp("/proc/version", kernel, sizeof(kernel)) <= 0) {
        snprintf(kernel, sizeof(kernel), "LiteBSD 2.0");
    } else {
        // if starts with "LiteBSD ", strip it to keep it neat
        if (strncmp(kernel, "LiteBSD ", 8) == 0) {
            memmove(kernel, kernel + 8, strlen(kernel + 8) + 1);
        }
    }

    // Uptime
    if (slurp("/proc/uptime", buf, sizeof(buf)) > 0) {
        unsigned long s = 0;
        const char *p = buf;
        while (*p >= '0' && *p <= '9') s = s * 10 + (*p++ - '0');
        unsigned long h = s / 3600; s %= 3600;
        unsigned long m = s / 60;
        if (h > 0) {
            snprintf(uptime_str, sizeof(uptime_str), "%luh %lum", h, m);
        } else {
            snprintf(uptime_str, sizeof(uptime_str), "%lum", m);
        }
    } else {
        snprintf(uptime_str, sizeof(uptime_str), "unknown");
    }

    // Packages
    int npkgs = count_packages();
    snprintf(pkgs, sizeof(pkgs), "%d (bin)", npkgs);

    // Shell
    const char *sh = getenv("SHELL");
    snprintf(shell_str, sizeof(shell_str), "%s", sh ? sh : "hush 1.36.1");

    // Resolution
    res[0] = '\0';
    int fb_fd = open("/dev/fb0", O_RDONLY);
    if (fb_fd >= 0) {
        struct fb_var_screeninfo var;
        if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &var) == 0 && var.xres > 0) {
            snprintf(res, sizeof(res), "%ux%u @ 60Hz", var.xres, var.yres);
        }
        close(fb_fd);
    }
    if (res[0] == '\0') {
        struct winsize ws;
        if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
            snprintf(res, sizeof(res), "%ux%u (text)", ws.ws_col, ws.ws_row);
        } else {
            snprintf(res, sizeof(res), "1024x768");
        }
    }

    // Terminal
    const char *t = getenv("TERM");
    snprintf(term, sizeof(term), "%s", t ? t : "/dev/tty0");

    // CPU
    if (slurp("/proc/cpuinfo", buf, sizeof(buf)) > 0) {
        extract_field(buf, "model name", cpu, sizeof(cpu));
        if (cpu[0] == '\0') extract_field(buf, "vendor_id", cpu, sizeof(cpu));
    } else {
        cpu[0] = '\0';
    }
    if (cpu[0] == '\0') snprintf(cpu, sizeof(cpu), "x86 Processor");

    // Memory
    if (slurp("/proc/meminfo", buf, sizeof(buf)) > 0) {
        char val[64];
        unsigned long total_kb = 0, free_kb = 0;
        extract_field(buf, "MemTotal", val, sizeof(val));
        total_kb = strtoul(val, NULL, 10);
        extract_field(buf, "MemAvailable", val, sizeof(val));
        if (val[0] == '\0') extract_field(buf, "MemFree", val, sizeof(val));
        free_kb = strtoul(val, NULL, 10);
        unsigned long used_kb = (total_kb > free_kb) ? (total_kb - free_kb) : 0;
        unsigned long used_mb = used_kb / 1024;
        unsigned long total_mb = total_kb / 1024;
        unsigned long pct = total_kb ? ((used_kb * 100) / total_kb) : 0;
        snprintf(mem, sizeof(mem), "%luMiB / %luMiB (%lu%%)", used_mb, total_mb, pct);
    } else {
        snprintf(mem, sizeof(mem), "unknown");
    }

    // Header line and underline
    char header[256];
    snprintf(header, sizeof(header), "\033[1;31m%s\033[0m\033[1m@\033[0m\033[1;31m%s\033[0m", user, host);
    int ulen = strlen(user) + 1 + strlen(host);
    char underline[128];
    if (ulen > 120) ulen = 120;
    for (int i = 0; i < ulen; i++) underline[i] = '-';
    underline[ulen] = '\0';

    // Beastie BSD Daemon ASCII Art (15 lines)
    static const char *logo[15] = {
        "              ,        ,  ",
        "             /(        )` ",
        "             \\ \\___   / | ",
        "             /- _  `-/  ' ",
        "            (/\\/ \\ \\   /\\ ",
        "            / /   | `    \\",
        "            O O   ) /    |",
        "            `-^--'`<     '",
        "           (_.)  _  )   / ",
        "            `.___/`    /  ",
        "              `-----' /   ",
        "  <----.     _/'---.      ",
        "   <----/____\\-------'    ",
        "                          ",
        "                          "
    };

    // Right-side info lines (15 lines)
    char info[15][256];
    snprintf(info[0],  sizeof(info[0]),  "%s", header);
    snprintf(info[1],  sizeof(info[1]),  "\033[0;37m%s\033[0m", underline);
    snprintf(info[2],  sizeof(info[2]),  "\033[1;31mOS\033[0m: %s", os);
    snprintf(info[3],  sizeof(info[3]),  "\033[1;31mHost\033[0m: %s", machine);
    snprintf(info[4],  sizeof(info[4]),  "\033[1;31mKernel\033[0m: %s", kernel);
    snprintf(info[5],  sizeof(info[5]),  "\033[1;31mUptime\033[0m: %s", uptime_str);
    snprintf(info[6],  sizeof(info[6]),  "\033[1;31mPackages\033[0m: %s", pkgs);
    snprintf(info[7],  sizeof(info[7]),  "\033[1;31mShell\033[0m: %s", shell_str);
    snprintf(info[8],  sizeof(info[8]),  "\033[1;31mResolution\033[0m: %s", res);
    snprintf(info[9],  sizeof(info[9]),  "\033[1;31mTerminal\033[0m: %s", term);
    snprintf(info[10], sizeof(info[10]), "\033[1;31mCPU\033[0m: %s", cpu);
    snprintf(info[11], sizeof(info[11]), "\033[1;31mMemory\033[0m: %s", mem);
    info[12][0] = '\0';
    snprintf(info[13], sizeof(info[13]),
             "\033[40m   \033[41m   \033[42m   \033[43m   \033[44m   \033[45m   \033[46m   \033[47m   \033[0m");
    snprintf(info[14], sizeof(info[14]),
             "\033[100m   \033[101m   \033[102m   \033[103m   \033[104m   \033[105m   \033[106m   \033[107m   \033[0m");

    printf("\n");
    for (int i = 0; i < 15; i++) {
        // Print logo in bold red
        printf("\033[1;31m%s\033[0m  %s\n", logo[i], info[i]);
    }
    printf("\n");

    return 0;
}
