#include "include/idt.h"
#include "include/time.h"

static volatile unsigned long timer_ticks;
static unsigned int timer_hz = 100;

void set_timer_frequency(int hz) {
    if (hz == 0) {
        return;
    }
    timer_hz = (unsigned int)hz;
    int divisor = 1193180 / hz; // 1.19318 mhz
    outb(PIT_COMMAND, 0x36);
    outb(PIT_DATA0, divisor & 0xFF);
    outb(PIT_DATA0, (divisor >> 8) & 0xFF);
}

void timer_tick(void) {
    timer_ticks++;
}

unsigned long timer_get_ticks(void) {
    return timer_ticks;
}

void sleep_ticks(unsigned long ticks) {
    unsigned long deadline = timer_ticks + ticks;

    while ((long)(timer_ticks - deadline) < 0) {
        asm volatile("hlt");
    }
}

void sleep(unsigned int ms) {
    unsigned long ticks = ((unsigned long)ms * timer_hz + 999) / 1000;
    sleep_ticks(ticks == 0 ? 1 : ticks);
}