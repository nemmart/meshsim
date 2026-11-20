/* grep.c: Rob Pike's matcher (The Practice of Programming, ch. 9) and grep (SHELL-SPEC sections 9 and 10).
 * c a literal byte, . any byte, ^ at the start anchors, $ at the end anchors, * zero or more of the previous element,
 * \ makes the next byte literal. A pattern is at most 128 bytes. The matcher recurses only at a *, so its depth is
 * bounded by the pattern's stars (at most 64); the line and -i are statics, to keep each frame small. */
#include "shell.h"

#define RE_MAX   128
#define GREP_MAX 1024

static const unsigned char *g_t;
static u32 g_tn;
static int g_ic;

static int fold(int c) { return g_ic && c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* the element at re: 2 bytes for \x (unless the \ is the pattern's last byte), else 1 */
static u32 elen(const unsigned char *re, u32 rn) { return re[0] == '\\' && rn > 1 ? 2 : 1; }

static int ematch(const unsigned char *re, u32 el, int c) {
    if (el == 2) return fold(re[1]) == fold(c);
    if (re[0] == '.') return 1;
    return fold(re[0]) == fold(c);
}

static int here(const unsigned char *re, u32 rn, u32 ti) {
    for (;;) {
        if (rn == 0) return 1;
        if (rn == 1 && re[0] == '$') return ti == g_tn;
        const u32 el = elen(re, rn);
        if (el < rn && re[el] == '*') {
            for (;;) {                                    /* the shortest first, as Pike's matchstar */
                if (here(re + el + 1, rn - el - 1, ti)) return 1;
                if (ti >= g_tn || !ematch(re, el, g_t[ti])) return 0;
                ti++;
            }
        }
        if (ti < g_tn && ematch(re, el, g_t[ti])) { re += el; rn -= el; ti++; continue; }
        return 0;
    }
}

int re_match(const unsigned char *re, u32 rn, const unsigned char *t, u32 tn, int icase) {
    g_t = t; g_tn = tn; g_ic = icase;
    if (rn && re[0] == '^') return here(re + 1, rn - 1, 0);
    for (u32 ti = 0; ti <= tn; ti++)
        if (here(re, rn, ti)) return 1;
    return 0;
}

/* ------------------------------------------------------------------ grep */
static unsigned char g_gline[GREP_MAX];

struct gstate { const char *re; u32 rn; u32 flags; int many; const char *arg; u64 lineno, count; };
enum { G_N = 1, G_I = 2, G_C = 4, G_V = 8 };

static void line(struct gstate *g, u32 n) {
    g->lineno++;
    const int m = re_match((const unsigned char *)g->re, g->rn, g_gline, n, g->flags & G_I) != !!(g->flags & G_V);
    if (!m) return;
    g->count++;
    if (g->flags & G_C) return;
    if (g->many) pf("%s:", g->arg);
    if (g->flags & G_N) pf("%llu:", g->lineno);
    out_n(g_gline, n);
    out_c('\n');
}

void cmd_grep(int argc, char **argv) {
    u32 f;
    char *v[1];
    int i = opts(argc, argv, "nicv", &f, v);
    if (i < 0) return;
    if (argc - i < 2) { usage("grep"); return; }
    struct gstate g;
    g.re = argv[i++];
    g.rn = str_len(g.re);
    g.flags = f;
    g.many = argc - i > 1;
    if (g.rn > RE_MAX) { epf("grep: pattern too long\n"); return; }
    for (; i < argc && !g_stop; i++) {
        g.arg = argv[i];
        g.lineno = g.count = 0;
        if (arg_path("grep", argv[i], g_path1)) continue;
        const int fd = fs_open(g_path1, FS_READ);
        if (fd < 0) { err_path("grep", argv[i], fd); continue; }
        u32 n = 0;
        int chunked = 0;                                  /* the line so far was cut at 1,024 bytes */
        long got;
        while ((got = fs_read(fd, g_page, SH_PAGE)) > 0) {
            for (long k = 0; k < got; k++) {
                const unsigned char c = g_page[k];
                if (c == '\n') {
                    if (n || !chunked) line(&g, n);
                    n = 0; chunked = 0;
                    continue;
                }
                g_gline[n++] = c;
                if (n == GREP_MAX) { line(&g, n); n = 0; chunked = 1; }
            }
            if (interrupted()) { stopped(); break; }
        }
        if (got < 0) err_path("grep", argv[i], (int)got);
        if (!g_stop && n) line(&g, n);
        fs_close(fd);
        if (g_stop) break;
        if (f & G_C) {
            if (g.many) pf("%s:", g.arg);
            pf("%llu\n", g.count);
        }
    }
}
