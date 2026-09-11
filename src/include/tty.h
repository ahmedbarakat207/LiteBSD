// vga configs
#define video_mem 0xB8000
#define screen_width 80
#define screen_height 25

// print rows/columns
extern int char_raw;
extern int char_column;

// vga colors
#define VGA_COLOR_BLACK 0
#define VGA_COLOR_BLUE 1
#define VGA_COLOR_GREEN 2
#define VGA_COLOR_CYAN 3
#define VGA_COLOR_RED 4
#define VGA_COLOR_MAGENTA 5
#define VGA_COLOR_BROWN 6
#define VGA_COLOR_LIGHT_GREY 7
#define VGA_COLOR_DARK_GREY 8
#define VGA_COLOR_LIGHT_BLUE 9
#define VGA_COLOR_LIGHT_GREEN 10
#define VGA_COLOR_LIGHT_CYAN 11
#define VGA_COLOR_LIGHT_RED 12
#define VGA_COLOR_LIGHT_MAGENTA 13
#define VGA_COLOR_LIGHT_BROWN 14
#define VGA_COLOR_YELLOW 14
#define VGA_COLOR_WHITE 15

void print_char(char c, char color);

void print(const char* str, char color);

void println(const char* str, char color);
void print_hex(unsigned int val, char color);

void print_dec(unsigned int val, char color);

void new_line();

void clear();

void disable_cursor(void);

void shell();

void err(const char* msg);

// console termios: layout mirrors libc struct termios (same order/sizes,
// Linux-compatible flag values) so field copies are ABI-safe.
#define CON_NCCS 32
#define CON_VEOF 4
#define CON_VTIME 5
#define CON_VMIN 6

#define CON_ISIG 0000001
#define CON_ICANON 0000002
#define CON_ECHO 0000010
#define CON_IEXTEN 0100000

#define CON_TCGETS 0x5401
#define CON_TCSETS 0x5402
#define CON_TCSETSW 0x5403
#define CON_TCSETSF 0x5404
#define CON_TCFLSH 0x540B
#define CON_TIOCGWINSZ 0x5413
#define CON_TIOCSWINSZ 0x5414
#define CON_FIONREAD 0x541B

struct con_termios {
    unsigned int c_iflag;
    unsigned int c_oflag;
    unsigned int c_cflag;
    unsigned int c_lflag;
    unsigned char c_line;
    unsigned char c_cc[CON_NCCS];
    unsigned int c_ispeed;
    unsigned int c_ospeed;
};

struct con_winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

int cons_is_canonical();
int cons_echo_on();
unsigned int cons_cc(int idx);
void cons_tcget(struct con_termios *out);
void cons_tcset(const struct con_termios *in);
void cons_ws_get(struct con_winsize *out);
void cons_ws_set(const struct con_winsize *in);