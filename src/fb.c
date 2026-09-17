#include "include/fb.h"
#include "include/font.h"
#include "include/tty.h"
#include <stdint.h>

static uint32_t fb_addr = 0;
static uint32_t fb_width = FB_DEFAULT_WIDTH;
static uint32_t fb_height = FB_DEFAULT_HEIGHT;
static uint32_t fb_pitch = FB_DEFAULT_WIDTH * 4;
static uint8_t  fb_bpp = FB_DEFAULT_BPP;
static int      fb_active = 0;

static inline void outl(uint16_t port, uint32_t val) {
    asm volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t val;
    asm volatile ("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outw(uint16_t port, uint16_t val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    asm volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = 0x80000000 | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xFC);
    outl(0xCF8, address);
    return inl(0xCFC);
}

static uint32_t find_pci_framebuffer(void) {
    for (uint8_t slot = 0; slot < 32; slot++) {
        uint32_t dev_vendor = pci_read_config(0, slot, 0, 0x00);
        uint16_t vendor = dev_vendor & 0xFFFF;
        uint16_t device = (dev_vendor >> 16) & 0xFFFF;
        if (vendor == 0xFFFF) continue;

        uint32_t class_reg = pci_read_config(0, slot, 0, 0x08);
        uint8_t class_code = (class_reg >> 24) & 0xFF;

        // Match Bochs/QEMU VGA (1234:1111) or Display Controller class (0x03)
        if ((vendor == 0x1234 && device == 0x1111) || class_code == 0x03) {
            uint32_t bar0 = pci_read_config(0, slot, 0, 0x10);
            uint32_t addr = bar0 & 0xFFFFFFF0;
            if (addr != 0) return addr;
        }
    }
    return 0;
}

static const uint32_t ansi_palette[16] = {
    0x00000000, // 0: Black
    0x001E66F5, // 1: Blue
    0x0020A020, // 2: Green
    0x00179299, // 3: Cyan
    0x00C02020, // 4: Red
    0x008839EF, // 5: Magenta
    0x00B07020, // 6: Brown / Orange
    0x00C0C0C0, // 7: Light Grey
    0x00555555, // 8: Dark Grey
    0x004080FF, // 9: Light Blue
    0x0040D040, // 10: Light Green
    0x0040D0E0, // 11: Light Cyan
    0x00FF4040, // 12: Light Red
    0x00E040FF, // 13: Light Magenta
    0x00FFE040, // 14: Yellow
    0x00FFFFFF  // 15: White
};

void fb_init(struct mb_info *info) {
    // 1. Check if Multiboot 1 bootloader already set up linear framebuffer
    if (info && (info->flags & MB_INFO_FRAMEBUFFER) && info->framebuffer_addr != 0) {
        fb_addr = (uint32_t)info->framebuffer_addr;
        fb_pitch = info->framebuffer_pitch;
        fb_width = info->framebuffer_width;
        fb_height = info->framebuffer_height;
        fb_bpp = info->framebuffer_bpp;
        fb_active = 1;
    }

    // 2. Check and configure Bochs Graphics Adapter (BGA) if available
    outw(0x01CE, 0); // VBE_DISPI_INDEX_ID
    uint16_t bga_id = inw(0x01CF);
    if (bga_id >= 0xB0C0 && bga_id <= 0xB0C6) {
        outw(0x01CE, 4); outw(0x01CF, 0x00);        // VBE_DISPI_DISABLED
        outw(0x01CE, 1); outw(0x01CF, FB_DEFAULT_WIDTH); // 1024
        outw(0x01CE, 2); outw(0x01CF, FB_DEFAULT_HEIGHT); // 768
        outw(0x01CE, 3); outw(0x01CF, FB_DEFAULT_BPP); // 32
        outw(0x01CE, 4); outw(0x01CF, 0x01 | 0x40); // VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED

        fb_width = FB_DEFAULT_WIDTH;
        fb_height = FB_DEFAULT_HEIGHT;
        fb_pitch = FB_DEFAULT_WIDTH * 4;
        fb_bpp = FB_DEFAULT_BPP;

        if (!fb_active) {
            uint32_t pci_bar = find_pci_framebuffer();
            fb_addr = pci_bar ? pci_bar : 0xFD000000;
            fb_active = 1;
        }
    }

    // 3. Fallback to standard QEMU PCI framebuffer address if still inactive
    if (!fb_active) {
        uint32_t pci_bar = find_pci_framebuffer();
        fb_addr = pci_bar ? pci_bar : 0xFD000000;
        fb_width = FB_DEFAULT_WIDTH;
        fb_height = FB_DEFAULT_HEIGHT;
        fb_pitch = FB_DEFAULT_WIDTH * 4;
        fb_bpp = FB_DEFAULT_BPP;
        fb_active = 1;
    }
}

int fb_is_active(void) {
    return fb_active;
}

uint32_t fb_get_addr(void) {
    return fb_addr;
}

uint32_t fb_get_width(void) {
    return fb_width;
}

uint32_t fb_get_height(void) {
    return fb_height;
}

uint32_t fb_get_pitch(void) {
    return fb_pitch;
}

/* Linux Framebuffer ioctl structures */
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

int fb_ioctl(uint32_t request, uint32_t arg) {
    if (!fb_active) return -1;
    switch (request) {
        case FBIOGET_VSCREENINFO: {
            if (!arg) return -1;
            struct fb_var_screeninfo *var = (struct fb_var_screeninfo *)arg;
            for (unsigned int i = 0; i < sizeof(*var); i++) ((char*)var)[i] = 0;
            var->xres = fb_width;
            var->yres = fb_height;
            var->xres_virtual = fb_width;
            var->yres_virtual = fb_height;
            var->xoffset = 0;
            var->yoffset = 0;
            var->bits_per_pixel = fb_bpp;
            var->grayscale = 0;

            var->red.offset = 16;
            var->red.length = 8;
            var->red.msb_right = 0;

            var->green.offset = 8;
            var->green.length = 8;
            var->green.msb_right = 0;

            var->blue.offset = 0;
            var->blue.length = 8;
            var->blue.msb_right = 0;

            var->transp.offset = 24;
            var->transp.length = 8;
            var->transp.msb_right = 0;

            var->height = 0;
            var->width = 0;
            var->pixclock = 15385; // ~65MHz for 1024x768 @ 60Hz
            var->left_margin = 160;
            var->right_margin = 24;
            var->upper_margin = 29;
            var->lower_margin = 3;
            var->hsync_len = 136;
            var->vsync_len = 6;
            var->sync = 0;
            var->vmode = 0;
            return 0;
        }
        case FBIOPUT_VSCREENINFO: {
            return 0;
        }
        case FBIOGET_FSCREENINFO: {
            if (!arg) return -1;
            struct fb_fix_screeninfo *fix = (struct fb_fix_screeninfo *)arg;
            for (unsigned int i = 0; i < sizeof(*fix); i++) ((char*)fix)[i] = 0;
            const char *id_str = "LiteBSD FB";
            for (int i = 0; id_str[i] && i < 15; i++) fix->id[i] = id_str[i];
            fix->smem_start = fb_addr;
            fix->smem_len = fb_height * fb_pitch;
            fix->type = 0; // FB_TYPE_PACKED_PIXELS
            fix->visual = 2; // FB_VISUAL_TRUECOLOR
            fix->line_length = fb_pitch;
            fix->accel = 0;
            return 0;
        }
        case FBIOBLANK: {
            if (arg != 0) {
                fb_clear(0x00000000);
            }
            return 0;
        }
        default:
            return -1;
    }
}

void fb_clear(uint32_t color) {

    if (!fb_addr) return;
    uint32_t *dst = (uint32_t *)fb_addr;
    uint32_t count = (fb_height * fb_pitch) / 4;
    asm volatile (
        "cld\n\t"
        "rep stosl"
        : "+D"(dst), "+c"(count)
        : "a"(color)
        : "memory"
    );
}

void fb_scroll_up(int char_rows) {
    if (!fb_addr) return;
    uint32_t pixel_rows = (uint32_t)(char_rows * CHAR_HEIGHT);
    if (pixel_rows >= fb_height) {
        fb_clear(0x00000000);
        return;
    }

    uint32_t *dst = (uint32_t *)fb_addr;
    const uint32_t *src = (const uint32_t *)(fb_addr + pixel_rows * fb_pitch);
    uint32_t copy_dwords = (fb_height - pixel_rows) * (fb_pitch / 4);

    asm volatile (
        "cld\n\t"
        "rep movsl"
        : "+D"(dst), "+S"(src), "+c"(copy_dwords)
        :
        : "memory"
    );

    uint32_t *clear_dst = (uint32_t *)(fb_addr + (fb_height - pixel_rows) * fb_pitch);
    uint32_t clear_dwords = pixel_rows * (fb_pitch / 4);
    uint32_t zero = 0;

    asm volatile (
        "cld\n\t"
        "rep stosl"
        : "+D"(clear_dst), "+c"(clear_dwords)
        : "a"(zero)
        : "memory"
    );
}

void fb_draw_char(unsigned char c, int col, int row, uint8_t color_attr) {
    if (!fb_addr) return;
    if (col < 0 || col >= screen_width || row < 0 || row >= screen_height) return;

    uint32_t fg = ansi_palette[color_attr & 0x0F];
    uint32_t bg = ansi_palette[(color_attr >> 4) & 0x0F];

    const unsigned char *glyph = font_8x16[c];
    uint32_t px = (uint32_t)(col * CHAR_WIDTH);
    uint32_t py = (uint32_t)(row * CHAR_HEIGHT);

    for (int y = 0; y < CHAR_HEIGHT; y++) {
        uint8_t bits = glyph[y];
        uint32_t *line_ptr = (uint32_t *)(fb_addr + (py + (uint32_t)y) * fb_pitch) + px;
        line_ptr[0] = (bits & 0x80) ? fg : bg;
        line_ptr[1] = (bits & 0x40) ? fg : bg;
        line_ptr[2] = (bits & 0x20) ? fg : bg;
        line_ptr[3] = (bits & 0x10) ? fg : bg;
        line_ptr[4] = (bits & 0x08) ? fg : bg;
        line_ptr[5] = (bits & 0x04) ? fg : bg;
        line_ptr[6] = (bits & 0x02) ? fg : bg;
        line_ptr[7] = (bits & 0x01) ? fg : bg;
    }
}

void fb_fill_cells(int col0, int row0, int col1, int row1, uint8_t color_attr) {
    if (col0 < 0) col0 = 0;
    if (row0 < 0) row0 = 0;
    if (col1 > screen_width) col1 = screen_width;
    if (row1 > screen_height) row1 = screen_height;

    for (int r = row0; r < row1; r++) {
        for (int c = col0; c < col1; c++) {
            fb_draw_char(' ', c, r, color_attr);
        }
    }
}
