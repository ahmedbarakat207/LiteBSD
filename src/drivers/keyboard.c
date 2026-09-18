#include "drivers/include/keyboard.h"
#include "include/tty.h"

char key_buffer[256];
volatile int buffer_head = 0;
volatile int buffer_tail = 0;

static int shift_down = 0;
static int ctrl_down = 0;
static int alt_down = 0;
static int caps_lock = 0;
static int e0_prefix = 0;

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

static void queue_char(char c){
    int next_head = (buffer_head + 1) % 256;
    if (next_head != buffer_tail) {
        key_buffer[buffer_head] = c;
        buffer_head = next_head;
    }
}

static void queue_str(const char *s){
    while (*s) {
        queue_char(*s++);
    }
}

static int handle_nav_key(unsigned char scancode){
    switch (scancode) {
        case 0x48: // Up arrow
            if (ctrl_down) queue_str("\033[1;5A");
            else if (alt_down) queue_str("\033[1;3A");
            else queue_str("\033[A");
            return 1;
        case 0x50: // Down arrow
            if (ctrl_down) queue_str("\033[1;5B");
            else if (alt_down) queue_str("\033[1;3B");
            else queue_str("\033[B");
            return 1;
        case 0x4D: // Right arrow
            if (ctrl_down) queue_str("\033[1;5C");
            else if (alt_down) queue_str("\033[1;3C");
            else queue_str("\033[C");
            return 1;
        case 0x4B: // Left arrow
            if (ctrl_down) queue_str("\033[1;5D");
            else if (alt_down) queue_str("\033[1;3D");
            else queue_str("\033[D");
            return 1;
        case 0x47: // Home
            if (ctrl_down) queue_str("\033[1;5H");
            else queue_str("\033[H");
            return 1;
        case 0x4F: // End
            if (ctrl_down) queue_str("\033[1;5F");
            else queue_str("\033[F");
            return 1;
        case 0x49: // Page Up
            if (ctrl_down) queue_str("\033[5;5~");
            else queue_str("\033[5~");
            return 1;
        case 0x51: // Page Down
            if (ctrl_down) queue_str("\033[6;5~");
            else queue_str("\033[6~");
            return 1;
        case 0x52: // Insert
            queue_str("\033[2~");
            return 1;
        case 0x53: // Delete
            if (ctrl_down) queue_str("\033[3;5~");
            else queue_str("\033[3~");
            return 1;
        default:
            return 0;
    }
}

void keyboard_init(){
    println("[KEYBOARD] Initializing keyboard...", VGA_COLOR_WHITE);
    shift_down = 0;
    ctrl_down = 0;
    alt_down = 0;
    caps_lock = 0;
    e0_prefix = 0;
    buffer_head = 0;
    buffer_tail = 0;
    println("[KEYBOARD] Keyboard initialized.", VGA_COLOR_GREEN);
}

void keyboard_handler(){
    unsigned char scancode = inb(KEYBOARD_DATA_PORT);

    // Extended prefix byte
    if (scancode == 0xE0) {
        e0_prefix = 1;
        return;
    }

    // Extended key sequence
    if (e0_prefix) {
        e0_prefix = 0;
        // Key release
        if (scancode & 0x80) {
            unsigned char code = scancode & 0x7F;
            if (code == 0x1D) ctrl_down = 0; // Right Ctrl release
            else if (code == 0x38) alt_down = 0; // Right Alt release
            return;
        }

        // Key press
        if (scancode == 0x1D) { // Right Ctrl press
            ctrl_down = 1;
            return;
        }
        if (scancode == 0x38) { // Right Alt press
            alt_down = 1;
            return;
        }
        if (scancode == 0x1C) { // Keypad Enter
            queue_char('\n');
            return;
        }
        if (scancode == 0x35) { // Keypad /
            queue_char('/');
            return;
        }

        // Extended navigation / arrow keys
        if (handle_nav_key(scancode)) {
            return;
        }
        return;
    }

    // Normal (non-extended) scancodes
    // Check release
    if (scancode & 0x80) {
        unsigned char code = scancode & 0x7F;
        if (code == 0x2A || code == 0x36) {
            shift_down = 0;
        } else if (code == 0x1D) {
            ctrl_down = 0;
        } else if (code == 0x38) {
            alt_down = 0;
        }
        return;
    }

    // Key presses
    if (scancode == 0x2A || scancode == 0x36) {
        shift_down = 1;
        return;
    }
    if (scancode == 0x1D) {
        ctrl_down = 1;
        return;
    }
    if (scancode == 0x38) {
        alt_down = 1;
        return;
    }
    if (scancode == 0x3A) { // Caps Lock
        caps_lock = !caps_lock;
        return;
    }

    // Keypad navigation keys (without 0xE0)
    if (handle_nav_key(scancode)) {
        return;
    }

    // Keypad plus / minus
    if (scancode == 0x4A) { queue_char('-'); return; }
    if (scancode == 0x4E) { queue_char('+'); return; }

    // Tab key (scancode 0x0F)
    if (scancode == 0x0F) {
        if (shift_down) {
            queue_str("\033[Z"); // Shift-Tab (backtab)
            return;
        }
        queue_char('\t');
        return;
    }

    char c = 0;
    int is_letter = 0;
    if (scancode < 128) {
        c = scancode_to_ascii[scancode];
    }
    if (c >= 'a' && c <= 'z') {
        is_letter = 1;
    }

    if (is_letter) {
        int eff_shift = shift_down ^ caps_lock;
        if (eff_shift) {
            c = c - 'a' + 'A';
        }
    } else if (shift_down) {
        if (scancode < 128 && scancode_shifted[scancode]) {
            c = scancode_shifted[scancode];
        }
    }

    if (c == 0) return;

    // Ctrl macros
    if (ctrl_down) {
        if (c >= 'a' && c <= 'z') {
            queue_char(c - 'a' + 1);
            return;
        }
        if (c >= 'A' && c <= 'Z') {
            queue_char(c - 'A' + 1);
            return;
        }
        if (c == '@' || c == ' ') {
            queue_char(0);
            return;
        }
        if (c == '[') {
            queue_char(27);
            return;
        }
        if (c == '\\') {
            queue_char(28);
            return;
        }
        if (c == ']') {
            queue_char(29);
            return;
        }
        if (c == '^' || scancode == 7) {
            queue_char(30);
            return;
        }
        if (c == '_' || c == '-' || scancode == 12) {
            queue_char(31);
            return;
        }
        if (c == '\b') {
            queue_char(127);
            return;
        }
    }

    // Alt macros (send ESC prefix)
    if (alt_down) {
        queue_char('\033');
        queue_char(c);
        return;
    }

    queue_char(c);
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