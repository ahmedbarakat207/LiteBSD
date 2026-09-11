#include "include/tty.h"
#include "include/keyboard.h"
#include "include/heap.h"
#include "include/sched.h"
#include "include/idt.h"

// print rows/columns
int char_raw = 0;
int char_column = 0;

// console termios: single global line discipline, the keyboard is the only
// input and VGA the only output. Default is canonical with echo (what hush
// expects); fullscreen programs like vi switch to raw via TCSETS.
static unsigned int con_iflag = 0;
static unsigned int con_oflag = 0;
static unsigned int con_cflag = 0;
static unsigned int con_lflag = CON_ICANON | CON_ECHO | CON_ISIG | CON_IEXTEN;
static unsigned char con_line = 0;
static unsigned char con_cc[CON_NCCS] = { [CON_VMIN] = 1 };
static unsigned int con_ispeed = 13; // B9600, matches libc default
static unsigned int con_ospeed = 13;
static unsigned short con_ws_row = screen_height;
static unsigned short con_ws_col = screen_width;

int cons_is_canonical(){
    return (con_lflag & CON_ICANON) != 0;
}

int cons_echo_on(){
    return (con_lflag & CON_ECHO) != 0;
}

unsigned int cons_cc(int idx){
    if (idx < 0 || idx >= CON_NCCS) return 0;
    return con_cc[idx];
}

void cons_tcget(struct con_termios *out){
    out->c_iflag = con_iflag;
    out->c_oflag = con_oflag;
    out->c_cflag = con_cflag;
    out->c_lflag = con_lflag;
    out->c_line = con_line;
    for (int i = 0; i < CON_NCCS; i++) out->c_cc[i] = con_cc[i];
    out->c_ispeed = con_ispeed;
    out->c_ospeed = con_ospeed;
}

void cons_tcset(const struct con_termios *in){
    con_iflag = in->c_iflag;
    con_oflag = in->c_oflag;
    con_cflag = in->c_cflag;
    con_lflag = in->c_lflag;
    con_line = in->c_line;
    for (int i = 0; i < CON_NCCS; i++) con_cc[i] = in->c_cc[i];
    con_ispeed = in->c_ispeed;
    con_ospeed = in->c_ospeed;
}

void cons_ws_get(struct con_winsize *out){
    out->ws_row = con_ws_row;
    out->ws_col = con_ws_col;
    out->ws_xpixel = 0;
    out->ws_ypixel = 0;
}

void cons_ws_set(const struct con_winsize *in){
    if (in->ws_row >= 1 && in->ws_row <= 128) con_ws_row = in->ws_row;
    if (in->ws_col >= 1 && in->ws_col <= 256) con_ws_col = in->ws_col;
}

// sliding window scrolling
void scroll_screen() {
    char* vga = (char*)video_mem;
    for (int row = 1; row < screen_height; row++) {
        for (int col = 0; col < screen_width; col++) {
            int src_offset = (row * screen_width + col) * 2;
            int dst_offset = ((row - 1) * screen_width + col) * 2;
            vga[dst_offset] = vga[src_offset];
            vga[dst_offset + 1] = vga[src_offset + 1];
        }
    }
    int bottom_row = screen_height - 1;
    for (int col = 0; col < screen_width; col++) {
        int offset = (bottom_row * screen_width + col) * 2;
        vga[offset] = ' ';
        vga[offset + 1] = VGA_COLOR_BLACK;
    }
    char_raw = screen_height - 1;
    char_column = 0;
}

static int ansi_state = 0; // 0 normal, 1 got ESC, 2 in CSI
static int ansi_params[4];
static int ansi_nparams = 0;
static int ansi_private = 0; // ? prefix (DEC private modes)
static int ansi_standout = 0;
static int ansi_saved_row = 0;
static int ansi_saved_col = 0;

// alternate screen for fullscreen programs (vi uses ?1049h/?1049l)
static char alt_screen[80 * 25 * 2];
static int alt_active = 0;
static int alt_saved_row = 0;
static int alt_saved_col = 0;

static void alt_enter(){
    if (alt_active) return;
    char* vga = (char*)video_mem;
    for (int i = 0; i < 80 * 25 * 2; i++) alt_screen[i] = vga[i];
    alt_saved_row = char_raw;
    alt_saved_col = char_column;
    alt_active = 1;
    for (int i = 0; i < 80 * 25 * 2; i += 2) {
        vga[i] = ' ';
        vga[i + 1] = VGA_COLOR_BLACK;
    }
    char_raw = 0;
    char_column = 0;
}

static void alt_exit(){
    if (!alt_active) return;
    char* vga = (char*)video_mem;
    for (int i = 0; i < 80 * 25 * 2; i++) vga[i] = alt_screen[i];
    char_raw = alt_saved_row;
    char_column = alt_saved_col;
    alt_active = 0;
}

// fill cells with spaces; rows/cols are 0-based and clamped, end exclusive
static void vga_fill(int row0, int col0, int row1, int col1, char attr){
    if (row0 < 0) row0 = 0;
    if (col0 < 0) col0 = 0;
    if (row1 > screen_height) row1 = screen_height;
    if (col1 > screen_width) col1 = screen_width;
    char* vga = (char*)video_mem;
    for (int row = row0; row < row1; row++) {
        for (int col = col0; col < col1; col++) {
            int offset = (row * screen_width + col) * 2;
            vga[offset] = ' ';
            vga[offset + 1] = attr;
        }
    }
}

static int ansi_param(int idx, int def){
    if (idx < 0 || idx > ansi_nparams) return def;
    int v = ansi_params[idx];
    return v <= 0 ? def : v;
}

static void ansi_csi(char final){
    int p0 = ansi_param(0, 0);
    switch (final) {
        case 'H':
        case 'f': {
            int row = ansi_param(0, 1);
            int col = ansi_param(1, 1);
            if (row < 1) row = 1;
            if (row > screen_height) row = screen_height;
            if (col < 1) col = 1;
            if (col > screen_width) col = screen_width;
            char_raw = row - 1;
            char_column = col - 1;
            break;
        }
        case 'A': {
            int n = p0 ? p0 : 1;
            char_raw -= n;
            if (char_raw < 0) char_raw = 0;
            break;
        }
        case 'B': {
            int n = p0 ? p0 : 1;
            char_raw += n;
            if (char_raw >= screen_height) char_raw = screen_height - 1;
            break;
        }
        case 'C': {
            int n = p0 ? p0 : 1;
            char_column += n;
            if (char_column >= screen_width) char_column = screen_width - 1;
            break;
        }
        case 'D': {
            int n = p0 ? p0 : 1;
            char_column -= n;
            if (char_column < 0) char_column = 0;
            break;
        }
        case 'J':
            if (p0 == 2) vga_fill(0, 0, screen_height, screen_width, VGA_COLOR_LIGHT_GREY);
            else vga_fill(char_raw, char_column, screen_height, screen_width, VGA_COLOR_LIGHT_GREY);
            break;
        case 'K':
            if (p0 == 2) vga_fill(char_raw, 0, char_raw + 1, screen_width, VGA_COLOR_LIGHT_GREY);
            else vga_fill(char_raw, char_column, char_raw + 1, screen_width, VGA_COLOR_LIGHT_GREY);
            break;
        case 'm':
            if (ansi_private) break;
            if (ansi_nparams < 0) {
                ansi_standout = 0;
                break;
            }
            for (int i = 0; i <= ansi_nparams; i++) {
                if (ansi_params[i] == 7) ansi_standout = 1;
                else if (ansi_params[i] == 0) ansi_standout = 0;
            }
            break;
        case 's':
            if (ansi_private) break;
            ansi_saved_row = char_raw;
            ansi_saved_col = char_column;
            break;
        case 'u':
            if (ansi_private) break;
            char_raw = ansi_saved_row;
            char_column = ansi_saved_col;
            break;
        case 'h':
            // DEC private mode on: only alternate screen is supported
            if (ansi_private && p0 == 1049) alt_enter();
            break;
        case 'l':
            if (ansi_private && p0 == 1049) alt_exit();
            break;
        default:
            break;
    }
}

void print_char(char c, char color) {
    // ANSI escape sequence state machine (subset vi needs: H A B C D J K m s u)
    if (ansi_state == 0) {
        if (c == 0x1b) {
            ansi_state = 1;
            return;
        }
        if (c == '\a') return; // no bell hardware, ignore
    } else if (ansi_state == 1) {
        if (c == '[') {
            ansi_state = 2;
            ansi_nparams = 0;
            ansi_private = 0;
            ansi_params[0] = 0;
            ansi_params[1] = 0;
            ansi_params[2] = 0;
            ansi_params[3] = 0;
            return;
        }
        ansi_state = 0;
        return;
    } else if (ansi_state == 2) {
        if (c == '?' && ansi_nparams == 0 && ansi_params[0] == 0) {
            ansi_private = 1;
            return;
        }
        if (c >= '0' && c <= '9') {
            ansi_params[ansi_nparams] = ansi_params[ansi_nparams] * 10 + (c - '0');
            if (ansi_params[ansi_nparams] > 999) ansi_params[ansi_nparams] = 999;
            return;
        }
        if (c == ';') {
            if (ansi_nparams < 3) ansi_nparams++;
            return;
        }
        ansi_state = 0;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) ansi_csi(c);
        return;
    }

    // tab
    if (c == '\t') {
        int next_stop = (char_column + 8) & ~7;
        if (next_stop >= screen_width) next_stop = screen_width - 1;
        while (char_column < next_stop) {
            print_char(' ', color);
        }
        return;
    }

    // backspace
    if (c == '\b') {
        if (char_column > 0) {
            char_column--;
        } else if (char_raw > 0) {
            char_raw--;
            char_column = screen_width - 1;
        }
        return;
    }
    // carriage return
    if (c == '\r') {
        char_column = 0;
        return;
    }
    // new line
    if (c == '\n') {
        char_column = 0;
        char_raw++;
        if (char_raw >= screen_height) {
            scroll_screen();
        }
        return;
    }
    // rest
    char* vga = (char*)video_mem;
    int offset = ((char_raw * screen_width) + char_column) * 2;
    vga[offset] = c;
    vga[offset + 1] = ansi_standout ? 0xF0 : color;
    char_column++;

    if (char_column >= screen_width) {
        char_column = 0;
        char_raw++;
        // scroll
        if (char_raw >= screen_height) {
            scroll_screen();
        }
    }
}

void print(const char* str, char color){
    while(*str){
        print_char(*str, color);
        str++;
    }
}
void println(const char* str, char color){
    print(str, color);
    new_line();
}

void print_hex(unsigned int val, char color) {
    print("0x", color);
    char buf[9];
    const char hex_chars[] = "0123456789abcdef";
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex_chars[val & 0xF];
        val >>= 4;
    }
    buf[8] = '\0';
    print(buf, color);
}

void print_dec(unsigned int val, char color) {
    if (val == 0) {
        print_char('0', color);
        return;
    }
    char buf[12];
    int idx = 0;
    while (val > 0) {
        buf[idx++] = '0' + (val % 10);
        val /= 10;
    }
    for (int i = idx - 1; i >= 0; i--) {
        print_char(buf[i], color);
    }
}

void new_line(){
    char_column = 0;
    char_raw++;
    if (char_raw >= screen_height) {
        scroll_screen();
    }
}

void disable_cursor(void){
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}

void clear(){
    char* vga = (char*)video_mem;
    for(int i = 0; i < screen_width * screen_height * 2; i += 2){
        vga[i] = ' ';
        vga[i + 1] = VGA_COLOR_BLACK;
    }
    char_raw = 0;
    char_column = 0;
    disable_cursor();
}

static int strings_equal(const char *left, const char *right){
    while(*left && *left == *right){
        left++;
        right++;
    }
    return *left == *right;
}

void shell(){
    while(scheduler_other_running_tasks() > 0){
        asm volatile("sti; hlt");
    }
    new_line();
    println("LiteBSD Shell", VGA_COLOR_WHITE);
    println("Type 'help' for a list of commands.", VGA_COLOR_WHITE);
    new_line();
    while(1){
        print("> ", VGA_COLOR_WHITE);
        char *input = kmalloc(256);
        int i = 0;
        char c;
        while((c = getchar()) != '\n'){
            if(c == '\b'){
                if(i > 0){
                    i--;
                    print_char('\b', VGA_COLOR_WHITE);
                    print_char(' ', VGA_COLOR_WHITE);
                    print_char('\b', VGA_COLOR_WHITE);
                }
            } else if (i < 255) {
                input[i++] = c;
                print_char(c, VGA_COLOR_WHITE);
            }
        }
        input[i] = '\0';
        new_line();
        if(strings_equal(input, "help")){
            println("Available commands:", VGA_COLOR_WHITE);
            println("help - Show this help message", VGA_COLOR_WHITE);
            println("clear - Clear the screen", VGA_COLOR_WHITE);
            println("exit - Exit the shell", VGA_COLOR_WHITE);
        } else if(strings_equal(input, "clear")){
            clear();
        } else if(strings_equal(input, "exit")){
            println("Exiting shell...", VGA_COLOR_WHITE);
            kfree(input);
            while(1) asm volatile("hlt");
        } else {
            println("Unknown command. Type 'help' for a list of commands.", VGA_COLOR_RED);
        }
        kfree(input);
    }
}

void err(const char* msg){
    print("[ERR] Kernel Panic: ", VGA_COLOR_RED);
    println(msg, VGA_COLOR_RED);
    while(1){
        asm volatile("hlt");
    }
}