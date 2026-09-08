#include "../include/errno.h"
#include "../include/unistd.h"
#include "../include/string.h"

int errno = 0;

char *strerror(int errnum) {
    switch (errnum) {
        case 0: return "Success";
        case EPERM: return "Operation not permitted";
        case ENOENT: return "No such file or directory";
        case ESRCH: return "No such process";
        case EINTR: return "Interrupted system call";
        case EIO: return "Input/output error";
        case ENXIO: return "Device not configured";
        case E2BIG: return "Argument list too long";
        case ENOEXEC: return "Exec format error";
        case EBADF: return "Bad file descriptor";
        case ECHILD: return "No child processes";
        case EDEADLK: return "Resource deadlock avoided";
        case ENOMEM: return "Cannot allocate memory";
        case EACCES: return "Permission denied";
        case EFAULT: return "Bad address";
        case EBUSY: return "Device busy";
        case EEXIST: return "File exists";
        case EXDEV: return "Cross-device link";
        case ENODEV: return "Operation not supported by device";
        case ENOTDIR: return "Not a directory";
        case EISDIR: return "Is a directory";
        case EINVAL: return "Invalid argument";
        case ENFILE: return "Too many open files in system";
        case EMFILE: return "Too many open files";
        case ENOTTY: return "Inappropriate ioctl for device";
        case EFBIG: return "File too large";
        case ENOSPC: return "No space left on device";
        case ESPIPE: return "Illegal seek";
        case EROFS: return "Read-only file system";
        case EMLINK: return "Too many links";
        case EPIPE: return "Broken pipe";
        case EDOM: return "Numerical argument out of domain";
        case ERANGE: return "Result too large";
        case ENOSYS: return "Function not implemented";
        default: return "Unknown error";
    }
}

void perror(const char *s) {
    if (s && *s) {
        write(2, s, strlen(s));
        write(2, ": ", 2);
    }
    char *err_str = strerror(errno);
    write(2, err_str, strlen(err_str));
    write(2, "\n", 1);
}
