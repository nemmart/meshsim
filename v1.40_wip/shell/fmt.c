/* fmt.c: the shell's output (SHELL-SPEC sections 7 and 11). One formatter, writing to the current sink: the console,
 * where every \n goes out as \r\n, or a redirection's 4 KB buffer, plain \n, flushed when full and at the command's
 * end. Error messages always go to the console. */
#include "shell.h"

/* ---- the sinks */
static int g_redir;                 /* a redirection is open */
static int g_rfd;
static int g_rbad;                  /* a write failed: reported once, the rest dropped */
static u32 g_rlen;
static const char *g_rshown;        /* the path as typed, for the messages */
static unsigned char g_rbuf[SH_PAGE];

static void con_out(int c) {
    if (c == '\n') con_putc('\r');
    con_putc(c);
}

void con_s(const char *s) {
    while (*s) con_putc(*s++);
}

static void rflush(void) {
    if (g_rlen && !g_rbad) {
        const long w = fs_write(g_rfd, g_rbuf, (long)g_rlen);
        if (w != (long)g_rlen) {
            g_rbad = 1;
            epf("sh: %s: %s\n", g_rshown, fs_msg(w < 0 ? (int)w : FS_ENOSPC));
        }
    }
    g_rlen = 0;
}

void out_c(int c) {
    if (!g_redir) { con_out(c); return; }
    if (g_rbad) return;
    g_rbuf[g_rlen++] = (unsigned char)c;
    if (g_rlen == SH_PAGE) rflush();
}

void out_n(const void *b, u32 n) {
    const unsigned char *p = b;
    while (n--) out_c(*p++);
}

int sink_open(const char *abs, const char *shown, u32 flags) {
    const int fd = fs_open(abs, flags);
    if (fd < 0) { epf("sh: %s: %s\n", shown, fs_msg(fd)); return fd; }
    g_redir = 1; g_rfd = fd; g_rbad = 0; g_rlen = 0; g_rshown = shown;
    return 0;
}

void sink_end(void) {
    if (!g_redir) return;
    rflush();
    const int r = fs_close(g_rfd);
    if (r < 0 && !g_rbad) epf("sh: %s: %s\n", g_rshown, fs_msg(r));
    g_redir = 0;
}

int sink_redirected(void) { return g_redir; }

int interrupted(void) {
    int c, hit = 0;
    while ((c = con_poll()) >= 0)
        if (c == 0x03) hit = 1;
    return hit;
}

/* ---- the formatter: %s %c %d %u %x %llu %llx %lld %%; a width; - to left-align; 0 to zero-pad */
static u32 utoa(char *b, u64 v, u32 base) {
    char t[24];
    u32 n = 0;
    do { const u32 d = (u32)(v % base); t[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v /= base; } while (v);
    for (u32 i = 0; i < n; i++) b[i] = t[n - 1 - i];
    return n;
}

static void vfmt(void (*put)(int), const char *f, va_list ap) {
    for (; *f; f++) {
        if (*f != '%') { put(*f); continue; }
        f++;
        int left = 0, zero = 0, ll = 0;
        u32 width = 0;
        if (*f == '-') { left = 1; f++; }
        if (*f == '0') { zero = 1; f++; }
        while (*f >= '0' && *f <= '9') width = width * 10 + (u32)(*f++ - '0');
        if (f[0] == 'l' && f[1] == 'l') { ll = 1; f += 2; }
        char b[24];
        const char *s = b;
        u32 n = 0;
        char sign = 0;
        switch (*f) {
        case 's': s = va_arg(ap, const char *); n = str_len(s); break;
        case 'c': b[0] = (char)va_arg(ap, int); n = 1; break;
        case 'd': {
            const long long v = ll ? va_arg(ap, long long) : va_arg(ap, int);
            u64 u = (u64)v;
            if (v < 0) { sign = '-'; u = 0ull - u; }
            n = utoa(b, u, 10);
            break;
        }
        case 'u': n = utoa(b, ll ? va_arg(ap, u64) : va_arg(ap, unsigned), 10); break;
        case 'x': n = utoa(b, ll ? va_arg(ap, u64) : va_arg(ap, unsigned), 16); break;
        case '%': b[0] = '%'; n = 1; break;
        case 0: return;
        default: put('%'); put(*f); continue;
        }
        const u32 total = n + (sign ? 1u : 0u);
        u32 pad = width > total ? width - total : 0;
        if (!left && !zero) while (pad) { put(' '); pad--; }
        if (sign) put(sign);
        if (!left && zero) while (pad) { put('0'); pad--; }
        for (u32 i = 0; i < n; i++) put(s[i]);
        while (pad) { put(' '); pad--; }
    }
}

void pf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfmt(out_c, fmt, ap);
    va_end(ap);
}

void epf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfmt(con_out, fmt, ap);
    va_end(ap);
}
