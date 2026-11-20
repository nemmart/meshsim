/* shell.c: shell_main, the line editor, the parser, redirection and paths (SHELL-SPEC sections 6, 7, 8). */
#include "shell.h"

char g_cwd[SH_LINE];
char g_path1[SH_LINE], g_path2[SH_LINE];
unsigned char g_page[SH_PAGE] __attribute__((aligned(8)));
int g_stop;

static char g_line[SH_LINE];
static char *g_argv[SH_ARGS + 1];

/* ------------------------------------------------------------------ errors (section 5 of the spec) */
static const char *const MSG[] = {
    "no such file or directory", "file exists", "not a directory", "is a directory", "directory not empty",
    "read-only file", "busy", "no space", "bad file", "invalid argument", "I/O error", "name too long",
    "file system shut down",
};

const char *fs_msg(int e) {
    static char unknown[16];
    if (e <= -1 && e >= -13) return MSG[-e - 1];
    /* "error N" without the formatter's sink */
    char t[12];
    u32 n = 0, v = e < 0 ? 0u - (u32)e : (u32)e;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    char *p = unknown;
    const char *w = "error ";
    while (*w) *p++ = *w++;
    if (e < 0) *p++ = '-';
    while (n) *p++ = t[--n];
    *p = 0;
    return unknown;
}

void err_path(const char *cmd, const char *path, int e) { epf("%s: %s: %s\n", cmd, path, fs_msg(e)); }
void err_cmd(const char *cmd, int e) { epf("%s: %s\n", cmd, fs_msg(e)); }
void stopped(void) { epf("^C\n"); g_stop = 1; }

/* ------------------------------------------------------------------ paths (section 8) */
/* The components of s, applied to out: empty and . skipped, .. drops the last one (at / it stays at /). What does not
 * fit in 1,023 bytes is counted in *excess, which .. takes back first: only the result's length matters. */
static int apply(char *out, u32 *len, u32 *excess, const char *s) {
    while (*s) {
        while (*s == '/') s++;
        if (!*s) break;
        const char *c = s;
        while (*s && *s != '/') s++;
        const u32 n = (u32)(s - c);
        if (n == 1 && c[0] == '.') continue;
        if (n == 2 && c[0] == '.' && c[1] == '.') {
            if (*excess) { (*excess)--; continue; }
            while (*len > 1 && out[*len - 1] != '/') (*len)--;
            if (*len > 1) (*len)--;                      /* the / before it, unless it is the root's */
            out[*len] = 0;
            continue;
        }
        if (n > 95) return FS_ENAMETOOLONG;
        const u32 need = (*len > 1 ? 1u : 0u) + n;
        if (*excess || *len + need > SH_LINE - 1) { (*excess)++; continue; }
        if (*len > 1) out[(*len)++] = '/';
        memcpy(out + *len, c, n);
        *len += n;
        out[*len] = 0;
    }
    return 0;
}

int path_resolve(const char *arg, char *out) {
    u32 len = 1, excess = 0;
    out[0] = '/'; out[1] = 0;
    int r = 0;
    if (arg[0] != '/') r = apply(out, &len, &excess, g_cwd);
    if (!r) r = apply(out, &len, &excess, arg);
    if (!r && excess) r = FS_ENAMETOOLONG;
    return r;
}

/* ------------------------------------------------------------------ the line editor (section 6) */
static int g_back = -1;            /* a byte read past an escape that was not a sequence */
static int g_last_cr;

static int get(void) {
    if (g_back >= 0) { const int c = g_back; g_back = -1; return c; }
    return con_getc();
}

static void erase(u32 *n) { (*n)--; con_s("\b \b"); }

/* A line into buf (at most cap - 1 bytes), echoed and edited. Returns 1 a line (Enter); 0 Ctrl-C (^C echoed);
 * -1 the end of input; with ctrl_d, 2 for Ctrl-D on an empty line (owner, 2026-11-20: cat's end of input). Ctrl-D
 * elsewhere is ignored, as any other control byte. */
int edit_line(char *buf, u32 cap, int ctrl_d) {
    u32 n = 0;
    for (;;) {
        int c = get();
        if (c < 0) return -1;
        if (c == '\n' && g_last_cr) { g_last_cr = 0; continue; }
        g_last_cr = c == '\r';
        if (c == '\r' || c == '\n') { con_s("\r\n"); buf[n] = 0; return 1; }
        if (c == 0x1B) {                                  /* ESC [ ... final 0x40-0x7E, or ESC O x: ignored */
            c = get();
            if (c == '[') {
                do c = get(); while (c >= 0 && !(c >= 0x40 && c <= 0x7E));
            } else if (c == 'O') {
                get();
            } else {
                g_back = c;
            }
            continue;
        }
        if (c == 0x08 || c == 0x7F) { if (n) erase(&n); else con_putc(0x07); continue; }
        if (c == 0x15) { while (n) erase(&n); continue; }
        if (c == 0x17) {
            while (n && buf[n - 1] == ' ') erase(&n);
            while (n && buf[n - 1] != ' ') erase(&n);
            continue;
        }
        if (c == 0x03) { con_s("^C\r\n"); return 0; }
        if (c == 0x04 && ctrl_d && n == 0) return 2;
        if (c >= 0x20 && c <= 0x7E) {
            if (n >= cap - 1) { con_putc(0x07); continue; }
            buf[n++] = (char)c;
            con_putc(c);
        }
    }
}

/* ------------------------------------------------------------------ parsing (section 7) */
/* Tokens in place in g_line: the result is never longer than what it comes from. Returns argc, or -1 (reported). */
static int parse(char **rfile, int *append) {
    char *r = g_line, *w = g_line, *tok = 0;
    int argc = 0, in_tok = 0, redir = 0, want_file = 0;
    *rfile = 0;
    for (;;) {
        char c = *r;
        if (c == 0 || c == ' ' || c == '\t' || c == '>') {
            if (in_tok) {
                *w++ = 0;
                in_tok = 0;
                if (want_file) { *rfile = tok; want_file = 0; }
                else if (argc == SH_ARGS) { epf("syntax error: too many arguments\n"); return -1; }
                else g_argv[argc++] = tok;
            }
            if (c == 0) break;
            r++;
            if (c == '>') {
                if (redir) { epf("syntax error: more than one redirection\n"); return -1; }
                redir = 1;
                if (*r == '>') { r++; *append = 1; } else *append = 0;
                want_file = 1;
            }
            continue;
        }
        if (!in_tok) { in_tok = 1; tok = w; }
        if (c == '"') {
            r++;
            for (;;) {
                c = *r;
                if (!c) { epf("syntax error: unterminated quote\n"); return -1; }
                r++;
                if (c == '"') break;
                if (c == '\\' && (*r == '"' || *r == '\\')) c = *r++;
                *w++ = c;
            }
            continue;
        }
        *w++ = c;
        r++;
    }
    if (want_file) { epf("syntax error: no file after >\n"); return -1; }
    g_argv[argc] = 0;
    return argc;
}

static void run_line(void) {
    char *rfile;
    int append = 0;
    const int argc = parse(&rfile, &append);
    if (argc < 0) return;
    const struct cmd *c = 0;
    if (argc) {
        c = cmd_find(g_argv[0]);
        if (!c) { epf("%s: command not found\n", g_argv[0]); return; }
    }
    if (rfile) {
        const int e = path_resolve(rfile, g_path1);
        if (e) { epf("sh: %s: %s\n", rfile, fs_msg(e)); return; }
        if (sink_open(g_path1, rfile, append ? FS_APPEND : FS_APPEND | FS_TRUNC)) return;
    }
    g_stop = 0;
    if (c) c->fn(argc, g_argv);
    sink_end();
}

void shell_main(void) {
    g_cwd[0] = '/'; g_cwd[1] = 0;
    for (;;) {
        con_s(g_cwd);
        con_s("> ");
        const int r = edit_line(g_line, SH_LINE, 0);
        if (r < 0) return;
        if (r > 0) run_line();
    }
}
