#include "include/keyboard.h"
#include "include/tty.h"

char key_buffer[256];
volatile int buffer_head = 0;
volatile int buffer_tail = 0;

// shift state for capitals and : ! ? etc (vi needs :q! and ZZ)
static int shift_down = 0;

static const char scancode_shifted[128] = {
    [2] = '!', [3] = '@', [4] = '#', [5] = '$', [6] = '%',
    [7] = '^', [8] = '&', [9] = '*', [10] = '(', [11] = ')',
    [12] = '_', [13] = '+',
    [16] = 'Q', [17] = 'W', [18] = 'E', [19] = 'R', [20] = 'T',
    [21] = 'Y', [22] = 'U', [23] = 'I', [24] = 'O', [25] = 'P',
    [26] = '{', [27] = '}',
    [30] = 'A', [31] = 'S', [32] = 'D', [33] = 'F', [34] = 'G',
    [35] = 'H', [36] = 'J', [37] = 'K', [38] = 'L',
    [39] = ':', [40] = '"', [41] = '~', [43] = '|',
    [44] = 'Z', [45] = 'X', [46] = 'C', [47] = 'V', [48] = 'B',
    [49] = 'N', [50] = 'M',
    [51] = '<', [52] = '>', [53] = '?',
};

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
    if (scancode == 0x2A || scancode == 0x36) {
        shift_down = 1;
        return;
    }
    if (scancode == 0xAA || scancode == 0xB6) {
        shift_down = 0;
        return;
    }
    if(scancode < 128){
        char c = scancode_to_ascii[scancode];
        if (shift_down && scancode_shifted[scancode]) c = scancode_shifted[scancode];
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
    while(buffer_head == buffer_tail) {
        asm volatile("sti; hlt"); // wait for key press or timer interrupt
    }
    char c = key_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % 256;
    return c;
}

int keyboard_available(){
    return (buffer_head - buffer_tail + 256) % 256;
}

int keyboard_trygetc(){
    if (buffer_head == buffer_tail) return -1;
    char c = key_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % 256;
    return (unsigned char)c;
}

void keyboard_flush(){
    buffer_tail = buffer_head;
}