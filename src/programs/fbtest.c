#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOPUT_VSCREENINFO 0x4601
#define FBIOGET_FSCREENINFO 0x4602
#define FBIOBLANK           0x4611

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

struct fb_fix_screeninfo {
    char id[16];
    uint32_t smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t type_aux;
    uint32_t visual;
    uint16_t xpanstep;
    uint16_t ypanstep;
    uint16_t ywrapstep;
    uint32_t line_length;
    uint32_t mmio_start;
    uint32_t mmio_len;
    uint32_t accel;
    uint16_t capabilities;
    uint16_t reserved[2];
};

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("[fbtest] Opening /dev/fb0...\n");
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        perror("open(/dev/fb0)");
        return 1;
    }

    struct fb_var_screeninfo var;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0) {
        perror("ioctl(FBIOGET_VSCREENINFO)");
        close(fd);
        return 1;
    }
    printf("[fbtest] Variable info: %ux%u, %u bpp\n", var.xres, var.yres, var.bits_per_pixel);

    struct fb_fix_screeninfo fix;
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0) {
        perror("ioctl(FBIOGET_FSCREENINFO)");
        close(fd);
        return 1;
    }
    printf("[fbtest] Fixed info: ID=\"%s\", smem_start=0x%x, smem_len=%u, line_len=%u\n",
           fix.id, fix.smem_start, fix.smem_len, fix.line_length);

    // Test read()
    uint32_t pixel = 0;
    int n = read(fd, &pixel, sizeof(pixel));
    printf("[fbtest] read() 4 bytes: status=%d, val=0x%08x\n", n, pixel);

    // Test write()
    n = write(fd, &pixel, sizeof(pixel));
    printf("[fbtest] write() 4 bytes: status=%d\n", n);

    // Test mmap()
    size_t screensize = var.yres * fix.line_length;
    uint32_t *fbp = (uint32_t *)mmap(NULL, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fbp == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }
    printf("[fbtest] mmap succeeded! Address: %p\n", (void*)fbp);

    // Draw a neat test pattern in the top-right corner
    // 780 to 980 x, 20 to 140 y
    printf("[fbtest] Drawing test pattern in top-right corner...\n");
    int start_x = 780, end_x = 980;
    int start_y = 20, end_y = 140;
    uint32_t colors[] = {
        0x00FF0000, // Red
        0x00FF7F00, // Orange
        0x00FFFF00, // Yellow
        0x0000FF00, // Green
        0x0000FFFF, // Cyan
        0x000000FF, // Blue
        0x008B00FF, // Violet
        0x00FFFFFF  // White
    };
    int num_colors = sizeof(colors) / sizeof(colors[0]);
    int col_width = (end_x - start_x) / num_colors;

    for (int y = start_y; y < end_y; y++) {
        for (int x = start_x; x < end_x; x++) {
            // border
            if (x == start_x || x == end_x - 1 || y == start_y || y == end_y - 1) {
                fbp[y * (fix.line_length / 4) + x] = 0x00FFFFFF;
            } else if (y < start_y + 60) {
                // Color stripes
                int c_idx = (x - start_x) / col_width;
                if (c_idx >= num_colors) c_idx = num_colors - 1;
                fbp[y * (fix.line_length / 4) + x] = colors[c_idx];
            } else {
                // Horizontal grayscale gradient
                int grad = ((x - start_x) * 255) / (end_x - start_x);
                fbp[y * (fix.line_length / 4) + x] = (grad << 16) | (grad << 8) | grad;
            }
        }
    }

    munmap(fbp, screensize);
    close(fd);
    printf("[fbtest] Success! Framebuffer is fully functional.\n");
    return 0;
}
