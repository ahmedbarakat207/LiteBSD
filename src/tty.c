#include "include/tty.h"
#include "include/keyboard.h"
#include "include/heap.h"

// print rows/columns
int char_raw = 0;
int char_column = 0;

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

void print_char(char c, char color) {
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
    vga[offset + 1] = color;
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

void new_line(){
    char_column = 0;
    char_raw++;
    if (char_raw >= screen_height) {
        scroll_screen();
    }
}

void clear(){
    char* vga = (char*)video_mem;
    for(int i = 0; i < screen_width * screen_height * 2; i += 2){
        vga[i] = ' ';
        vga[i + 1] = VGA_COLOR_BLACK;
    }
    char_raw = 0;
    char_column = 0;
}

static int strings_equal(const char *left, const char *right){
    while(*left && *left == *right){
        left++;
        right++;
    }
    return *left == *right;
}

void shell(){
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
            } else {
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
            break;
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