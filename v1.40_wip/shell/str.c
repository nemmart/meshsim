/* str.c: the string helpers, and memcpy, memset, memmove and memcmp, which the compilers call even when freestanding.
 * With GCC this file is compiled with -fno-tree-loop-distribute-patterns, or the loop in memset could become a call to
 * memset (SHELL-SPEC section 3). */
#include "shell.h"

void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *a = d;
    const unsigned char *b = s;
    while (n--) *a++ = *b++;
    return d;
}

void *memset(void *d, int c, size_t n) {
    unsigned char *a = d;
    while (n--) *a++ = (unsigned char)c;
    return d;
}

void *memmove(void *d, const void *s, size_t n) {
    unsigned char *a = d;
    const unsigned char *b = s;
    if (a < b) {
        while (n--) *a++ = *b++;
    } else {
        while (n) { n--; a[n] = b[n]; }
    }
    return d;
}

int memcmp(const void *x, const void *y, size_t n) {
    const unsigned char *a = x, *b = y;
    for (; n; n--, a++, b++)
        if (*a != *b) return *a < *b ? -1 : 1;
    return 0;
}

u32 str_len(const char *s) {
    u32 n = 0;
    while (s[n]) n++;
    return n;
}

int str_cmp(const char *x, const char *y) {
    const unsigned char *a = (const unsigned char *)x, *b = (const unsigned char *)y;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b ? 0 : (*a < *b ? -1 : 1);
}

int str_eq(const char *a, const char *b) { return str_cmp(a, b) == 0; }

void str_cpy(char *d, const char *s) {
    while ((*d++ = *s++) != 0) {}
}

int str_num(const char *s, u64 *v, int hex) {
    u64 x = 0;
    if (!*s) return -1;
    if (hex && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        if (!*s) return -1;
        for (; *s; s++) {
            u32 d;
            if (*s >= '0' && *s <= '9') d = (u32)(*s - '0');
            else if (*s >= 'a' && *s <= 'f') d = (u32)(*s - 'a' + 10);
            else if (*s >= 'A' && *s <= 'F') d = (u32)(*s - 'A' + 10);
            else return -1;
            if (x >> 60) return -1;
            x = (x << 4) | d;
        }
    } else {
        for (; *s; s++) {
            if (*s < '0' || *s > '9') return -1;
            const u64 d = (u64)(*s - '0');
            if (x > (~0ull - d) / 10) return -1;
            x = x * 10 + d;
        }
    }
    *v = x;
    return 0;
}
