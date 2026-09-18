#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    int use_heap = argc > 1 && strcmp(argv[1], "malloc") == 0;
    pid_t p = fork();
    if (p < 0) {
        printf("fork failed\n");
        return 2;
    }
    if (p == 0) {
        /* child: optionally touch the shared heap, then exit */
        if (use_heap) {
            char *m = (char*)malloc(65536);
            if (m) {
                memset(m, 0xAB, 65536);
                printf("child malloc ok\n");
                fflush(stdout);
            } else {
                printf("child malloc FAILED\n");
                fflush(stdout);
            }
        }
        _exit(7);
    }
    int st = 0;
    pid_t w = waitpid(p, &st, 0);
    printf("child=%d waited=%d alive=%d code=%d\n",
           (int)p, (int)w,
           WIFEXITED(st) ? 1 : 0, WEXITSTATUS(st));
    fflush(stdout);
    return 0;
}
