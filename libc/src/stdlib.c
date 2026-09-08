#include "../include/stdlib.h"
#include "../include/unistd.h"
#include "../include/string.h"
#include <stddef.h>
#include <stdint.h>

#define BLOCK_MAGIC 0x4C425344

typedef struct block_header {
    size_t size;         
    struct block_header *next; 
    int free;                   
    uint32_t magic; // magic wow             
} block_header_t;

static block_header_t *free_list = NULL;

void *sbrk(intptr_t increment) {
    long cur = syscall(9, 0, 0, 0);
    if (cur <= 0) return (void*)-1;
    if (increment == 0) return (void*)cur;
    long target = cur + increment;
    long res = syscall(9, target, 0, 0);
    if (res != target) {
        return (void*)-1;
    }
    return (void*)cur;
}

void *malloc(size_t size) {
    if (size == 0) return NULL;

    size = (size + 7) & ~7;

    block_header_t *curr = free_list;
    block_header_t *prev = NULL;

    while (curr) {
        if (curr->magic == BLOCK_MAGIC && curr->free && curr->size >= size) {
            if (curr->size >= size + sizeof(block_header_t) + 16) {
                block_header_t *next_block = (block_header_t*)((char*)(curr + 1) + size);
                next_block->size = curr->size - size - sizeof(block_header_t);
                next_block->next = curr->next;
                next_block->free = 1;
                next_block->magic = BLOCK_MAGIC;

                curr->size = size;
                curr->next = next_block;
            }
            curr->free = 0;
            return (void*)(curr + 1);
        }
        prev = curr;
        curr = curr->next;
    }
    
    size_t total_size = sizeof(block_header_t) + size;
    void *mem = sbrk(total_size);
    if (mem == (void*)-1) {
        return NULL;
    }

    block_header_t *new_block = (block_header_t*)mem;
    new_block->size = size;
    new_block->next = NULL;
    new_block->free = 0;
    new_block->magic = BLOCK_MAGIC;

    if (prev) {
        prev->next = new_block;
    } else {
        free_list = new_block;
    }

    return (void*)(new_block + 1);
}

void free(void *ptr) {
    if (!ptr) return;

    block_header_t *block = ((block_header_t*)ptr) - 1;
    if (block->magic != BLOCK_MAGIC) {
        return; 
    }

    block->free = 1;
    block_header_t *curr = free_list;
    while (curr && curr->next) {
        if (curr->free && curr->next->free) {
            if ((char*)(curr + 1) + curr->size == (char*)curr->next) {
                curr->size += sizeof(block_header_t) + curr->next->size;
                curr->next = curr->next->next;
                continue;
            }
        }
        curr = curr->next;
    }
}

void *calloc(size_t nmemb, size_t size) {
    if (nmemb != 0 && size > (size_t)-1 / nmemb) {
        return NULL;
    }
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    block_header_t *block = ((block_header_t*)ptr) - 1;
    if (block->magic != BLOCK_MAGIC) return NULL;

    if (block->size >= size) {
        return ptr;
    }

    void *new_ptr = malloc(size);
    if (!new_ptr) return NULL;

    memcpy(new_ptr, ptr, block->size);
    free(ptr);
    return new_ptr;
}

int atoi(const char *nptr) {
    return (int)strtol(nptr, NULL, 10);
}

long atol(const char *nptr) {
    return strtol(nptr, NULL, 10);
}

long strtol(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f' || *s == '\v') {
        s++;
    }

    int negative = 0;
    if (*s == '-') {
        negative = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        base = 16;
    }
    if (base == 0) {
        if (s[0] == '0') {
            base = 8;
        } else {
            base = 10;
        }
    }

    long val = 0;
    const char *start = s;
    while (*s) {
        int digit;
        if (*s >= '0' && *s <= '9') {
            digit = *s - '0';
        } else if (*s >= 'a' && *s <= 'z') {
            digit = *s - 'a' + 10;
        } else if (*s >= 'A' && *s <= 'Z') {
            digit = *s - 'A' + 10;
        } else {
            break;
        }

        if (digit >= base) break;
        val = val * base + digit;
        s++;
    }

    if (endptr) {
        *endptr = (char*)(s == start ? nptr : s);
    }

    return negative ? -val : val;
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    return (unsigned long)strtol(nptr, endptr, base);
}

char *itoa(int value, char *str, int base) {
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }

    char *rc = str;
    char *ptr = str;
    char *low;
    unsigned int uval = (unsigned int)value;

    if (value < 0 && base == 10) {
        *ptr++ = '-';
        str++;
        uval = (unsigned int)-value;
    }

    low = ptr;
    do {
        int rem = uval % base;
        *ptr++ = (rem < 10) ? ('0' + rem) : ('a' + rem - 10);
        uval /= base;
    } while (uval);

    *ptr-- = '\0';

    // invert digits
    while (low < ptr) {
        char tmp = *low;
        *low++ = *ptr;
        *ptr-- = tmp;
    }

    return rc;
}

int abs(int j) {
    return j < 0 ? -j : j;
}

long labs(long j) {
    return j < 0 ? -j : j;
}

static unsigned long next_rand = 1;

int rand(void) {
    next_rand = next_rand * 1103515245 + 12345;
    return (unsigned int)(next_rand / 65536) % (RAND_MAX + 1);
}

void srand(unsigned int seed) {
    next_rand = seed;
}
#define MAX_ATEXIT 32
static void (*atexit_funcs[MAX_ATEXIT])(void);
static int atexit_count = 0;

int atexit(void (*function)(void)) {
    if (atexit_count >= MAX_ATEXIT || !function) {
        return -1;
    }
    atexit_funcs[atexit_count++] = function;
    return 0;
}

void abort(void) {
    _exit(134); // 128 + SIGABRT(6)
}

char **environ = NULL;

char *getenv(const char *name) {
    if (!environ || !name || !*name) return NULL;

    size_t name_len = strlen(name);
    for (char **ep = environ; *ep; ep++) {
        if (strncmp(*ep, name, name_len) == 0 && (*ep)[name_len] == '=') {
            return *ep + name_len + 1;
        }
    }
    return NULL;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (!name || !*name || strchr(name, '=')) return -1;
    if (getenv(name) && !overwrite) return 0;

    size_t name_len = strlen(name);
    size_t val_len = value ? strlen(value) : 0;
    char *new_entry = malloc(name_len + val_len + 2);
    if (!new_entry) return -1;

    memcpy(new_entry, name, name_len);
    new_entry[name_len] = '=';
    if (value) memcpy(new_entry + name_len + 1, value, val_len);
    new_entry[name_len + 1 + val_len] = '\0';

    if (!environ) {
        environ = malloc(sizeof(char*) * 2);
        if (!environ) { free(new_entry); return -1; }
        environ[0] = new_entry;
        environ[1] = NULL;
        return 0;
    }

    // check for environ
    for (size_t i = 0; environ[i]; i++) {
        if (strncmp(environ[i], name, name_len) == 0 && environ[i][name_len] == '=') {
            environ[i] = new_entry;
            return 0;
        }
    }

    // append to environ
    size_t count = 0;
    while (environ[count]) count++;

    char **new_env = malloc(sizeof(char*) * (count + 2));
    if (!new_env) { free(new_entry); return -1; }
    for (size_t i = 0; i < count; i++) new_env[i] = environ[i];
    new_env[count] = new_entry;
    new_env[count + 1] = NULL;
    environ = new_env;
    return 0;
}

int unsetenv(const char *name) {
    if (!environ || !name || !*name || strchr(name, '=')) return -1;

    size_t name_len = strlen(name);
    for (size_t i = 0; environ[i]; i++) {
        if (strncmp(environ[i], name, name_len) == 0 && environ[i][name_len] == '=') {
            for (size_t j = i; environ[j]; j++) {
                environ[j] = environ[j + 1];
            }
            return 0;
        }
    }
    return 0;
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *)) {
    size_t l = 0;
    size_t r = nmemb;
    while (l < r) {
        size_t m = l + (r - l) / 2;
        const void *elem = (const char*)base + m * size;
        int cmp = compar(key, elem);
        if (cmp == 0) return (void*)elem;
        if (cmp < 0) r = m;
        else l = m + 1;
    }
    return NULL;
}

static void swap(char *a, char *b, size_t size) {
    for (size_t i = 0; i < size; i++) {
        char tmp = a[i];
        a[i] = b[i];
        b[i] = tmp;
    }
}

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    if (nmemb < 2 || size == 0) return;

    char *b = (char*)base;
    char *pivot = b + (nmemb - 1) * size;
    size_t i = 0;

    for (size_t j = 0; j < nmemb - 1; j++) {
        if (compar(b + j * size, pivot) <= 0) {
            swap(b + i * size, b + j * size, size);
            i++;
        }
    }
    swap(b + i * size, pivot, size);

    if (i > 1) {
        qsort(b, i, size, compar);
    }
    if (nmemb - i - 1 > 1) {
        qsort(b + (i + 1) * size, nmemb - i - 1, size, compar);
    }
}