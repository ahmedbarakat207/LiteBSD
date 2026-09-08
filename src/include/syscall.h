#include <stdint.h>

#define NULL       0
#define WRITE  1
#define READ   2
#define EXIT   3
#define GETPID 4
#define FORK    5
#define EXECVE  6
#define WAIT4   7
#define GETPPID 8
#define BRK     9
#define MMAP    10
#define MUNMAP  11
#define PIPE    12
#define DUP     13
#define DUP2    14
#define KILL    15
#define IOCTL   16
#define OPEN    17
#define CLOSE   18
#define LSEEK   19
#define STAT    20
#define FSTAT   21
#define UNLINK  22
#define MKDIR   23
#define CHDIR   24
#define GETCWD  25

#define USER_LOAD_ADDR 0x8000000   

struct interrupt_frame;
struct interrupt_frame *syscall_handler(struct interrupt_frame *frame);

static inline int syscall0(uint32_t number){
	uint32_t result;
	asm volatile("int $0x80"
				 : "=a"(result)
				 : "a"(number)
				 : "memory");
	return (int)result;
}

static inline int syscall1(uint32_t number, uint32_t arg1){
	uint32_t result;
	asm volatile("int $0x80"
				 : "=a"(result)
				 : "a"(number), "b"(arg1)
				 : "memory");
	return (int)result;
}

static inline int syscall2(uint32_t number, uint32_t arg1, uint32_t arg2){
	uint32_t result;
	asm volatile("int $0x80"
				 : "=a"(result)
				 : "a"(number), "b"(arg1), "c"(arg2)
				 : "memory");
	return (int)result;
}

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

static inline int syscall_fork(void){
	return syscall0(FORK);
}

static inline int syscall_execve(const char *path, char *const argv[], char *const envp[]){
	return syscall3(EXECVE, (uint32_t)path, (uint32_t)argv, (uint32_t)envp);
}

static inline int syscall_wait4(int pid, int *status){
	return syscall2(WAIT4, (uint32_t)pid, (uint32_t)status);
}

static inline int syscall_getppid(void){
	return syscall0(GETPPID);
}

static inline void *syscall_brk(void *addr){
	return (void*)syscall1(BRK, (uint32_t)addr);
}

static inline void *syscall_mmap(uint32_t length){
	return (void*)syscall1(MMAP, length);
}

static inline int syscall_munmap(void *addr){
	return syscall1(MUNMAP, (uint32_t)addr);
}

static inline int syscall_pipe(int fds[2]){
	return syscall1(PIPE, (uint32_t)fds);
}

static inline int syscall_dup(int fd){
	return syscall1(DUP, (uint32_t)fd);
}

static inline int syscall_dup2(int oldfd, int newfd){
	return syscall2(DUP2, (uint32_t)oldfd, (uint32_t)newfd);
}

static inline int syscall_kill(int pid, int sig){
	return syscall2(KILL, (uint32_t)pid, (uint32_t)sig);
}

static inline int syscall_ioctl(int fd, uint32_t request, void *arg){
	return syscall3(IOCTL, (uint32_t)fd, request, (uint32_t)arg);
}

static inline int syscall_open(const char *path, int flags, int mode){
	return syscall3(OPEN, (uint32_t)path, (uint32_t)flags, (uint32_t)mode);
}

static inline int syscall_close(int fd){
	return syscall1(CLOSE, (uint32_t)fd);
}

static inline int syscall_lseek(int fd, int offset, int whence){
	return syscall3(LSEEK, (uint32_t)fd, (uint32_t)offset, (uint32_t)whence);
}

static inline int syscall_stat(const char *path, void *st){
	return syscall2(STAT, (uint32_t)path, (uint32_t)st);
}

static inline int syscall_fstat(int fd, void *st){
	return syscall2(FSTAT, (uint32_t)fd, (uint32_t)st);
}

static inline int syscall_unlink(const char *path){
	return syscall1(UNLINK, (uint32_t)path);
}

static inline int syscall_mkdir(const char *path){
	return syscall1(MKDIR, (uint32_t)path);
}

static inline int syscall_chdir(const char *path){
	return syscall1(CHDIR, (uint32_t)path);
}

static inline int syscall_getcwd(char *buffer, uint32_t size){
	return syscall2(GETCWD, (uint32_t)buffer, size);
}
