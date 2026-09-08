#define PIT_COMMAND  0x43
#define PIT_DATA0    0x40
#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1
#define ICW1_ICW4    0x01
#define ICW1_INIT    0x10
#define ICW4_8086    0x01

void idt_init();
void pic_remap();
void outb(unsigned short port, unsigned char data);