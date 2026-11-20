/* console_host.c: the shell's console on the host (SHELL-SPEC section 4).
 *
 * A terminal: raw mode (no echo, no line buffering), restored at exit. Anything else (a pipe, a file, or a test's
 * keystrokes given by host/main.cpp) is a script: its bytes are read as they are.
 *
 * In a script, con_poll gives the next byte only when it is Ctrl-C (0x03) †: a script's bytes are typed ahead, and the
 * rest of them belong to the prompts after the command, not to the command's interrupted() checks, which would drop
 * them. So a test interrupts a command by writing \x03 right after the command's Enter. */
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "../console.h"
#include "console_host.h"

static const unsigned char *g_script;
static size_t g_n, g_at;
static int g_tty;
static struct termios g_saved;

static void restore(void) {
    fflush(stdout);
    tcsetattr(0, TCSANOW, &g_saved);
}

void con_host_script(const unsigned char *b, size_t n) {
    g_script = b; g_n = n; g_at = 0;
}

void con_host_init(void) {
    if (g_script) return;
    if (isatty(0)) {
        struct termios t;
        tcgetattr(0, &g_saved);
        t = g_saved;
        t.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ISIG | IEXTEN);
        t.c_iflag &= ~(tcflag_t)(ICRNL | IXON);
        t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &t);
        atexit(restore);
        g_tty = 1;
        return;
    }
    /* a script on stdin: read it whole */
    static unsigned char *b;
    size_t cap = 0, n = 0;
    for (;;) {
        if (n == cap) { cap = cap ? cap * 2 : 65536; b = realloc(b, cap); if (!b) exit(1); }
        const size_t r = fread(b + n, 1, cap - n, stdin);
        if (!r) break;
        n += r;
    }
    con_host_script(b, n);
}

int con_getc(void) {
    fflush(stdout);
    if (!g_tty) return g_at < g_n ? g_script[g_at++] : -1;
    unsigned char c;
    return read(0, &c, 1) == 1 ? c : -1;
}

int con_poll(void) {
    if (!g_tty) return g_at < g_n && g_script[g_at] == 0x03 ? g_script[g_at++] : -1;
    fflush(stdout);
    struct pollfd p = {0, POLLIN, 0};
    unsigned char c;
    if (poll(&p, 1, 0) == 1 && read(0, &c, 1) == 1) return c;
    return -1;
}

void con_putc(int c) { putchar(c); }
