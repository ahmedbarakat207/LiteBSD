#include <stdint.h>

#define NULL       0
#define WRITE  1
#define READ   2
#define EXIT   3
#define GETPID 4

struct interrupt_frame;
void syscall_handler(struct interrupt_frame *frame);

static inline int syscall3(uint32_t number, uint32_t arg1,
						   uint32_t arg2, uint32_t arg3){
	uint32_t result;
	asm volatile("int $0x80"
				 : "=a"(result)
				 : "a"(number), "b"(arg1), "c"(arg2), "d"(arg3)
				 : "memory");
	return (int)result;
}

static inline int syscall_write(const char *buffer, uint32_t count){
	return syscall3(WRITE, 1, (uint32_t)buffer, count);
}

static inline int syscall_read(char *buffer, uint32_t count){
	return syscall3(READ, 0, (uint32_t)buffer, count);
}

static inline int syscall_exit(int status){
	return syscall3(EXIT, (uint32_t)status, 0, 0);
}

static inline int syscall_getpid(void){
	return syscall3(GETPID, 0, 0, 0);
}