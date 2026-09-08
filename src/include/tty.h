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