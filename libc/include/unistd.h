#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>
#include <stdint.h>

#ifndef _PID_T_DECLARED
typedef int pid_t;
#define _PID_T_DECLARED
#endif

#ifndef _OFF_T_DECLARED
typedef long off_t;
#define _OFF_T_DECLARED
#endif

#ifndef _SSIZE_T_DECLARED
typedef long ssize_t;
#define _SSIZE_T_DECLARED
#endif

#ifndef _INTPTR_T_DECLARED
typedef long intptr_t;
#define _INTPTR_T_DECLARED
#endif

long syscall(long number, ...);

int write(int fd, const void *buf, size_t count);
int read(int fd, void *buf, size_t count);
void _exit(int status);
void exit(int status);
pid_t getpid(void);
pid_t getppid(void);
pid_t fork(void);
int execve(const char *path, char *const argv[], char *const envp[]);
int close(int fd);
int pipe(int pipefd[2]);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
off_t lseek(int fd, off_t offset, int whence);
int unlink(const char *path);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);
int brk(void *addr);
void *sbrk(intptr_t increment);

#endif
