#include "include/keyboard.h"
#include "include/tty.h"

char key_buffer[256];
volatile int buffer_head = 0;
volatile int buffer_tail = 0;

static inline unsigned char inb(unsigned short port){
    unsigned char value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void keyboard_init(){
    println("[KEYBOARD] Initializing keyboard...", VGA_COLOR_WHITE);
    println("[KEYBOARD] Keyboard initialized.", VGA_COLOR_GREEN);
}

void keyboard_handler(){
    unsigned char scancode = inb(KEYBOARD_DATA_PORT);
    if(scancode < 128){
        char c = scancode_to_ascii[scancode];
        if(c != 0){
            int next_head = (buffer_head + 1) % 256;
            if(next_head != buffer_tail){
                key_buffer[buffer_head] = c;
                buffer_head = next_head;
            }
        }
    }
}

char getchar(){
    while(buffer_head == buffer_tail); // wait for key press
    char c = key_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % 256;
    return c;
}