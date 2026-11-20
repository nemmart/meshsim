/* grep.c: Rob Pike's matcher (The Practice of Programming, ch. 9) and grep (SHELL-SPEC sections 9 and 10).
 * c a literal byte, . any byte, ^ at the start anchors, $ at the end anchors, * zero or more of the previous element,
 * \ makes the next byte literal. A pattern is at most 128 bytes.
 *
 * Pike's matchstar recurses once per *, up to 64 deep for a pattern of 128 bytes: over the 2 KB stack. So the same
 * search, in the same order (each * the shortest first), keeps its backtracking points in a static stack instead: for
 * each * reached, where the pattern goes on after it and the text position the rest was last tried at. */
#include "shell.h"

#define RE_MAX   128
#define GREP_MAX 1024

static const unsigned char *g_re, *g_t;
static u32 g_rn, g_tn;
static int g_ic;
static struct { unsigned short r, ti; } g_bt[RE_MAX / 2];    /* a * per two bytes of pattern at most */

static int fold(int c) { return g_ic && c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* the element at r: 2 bytes for \x (unless the \ is the pattern's last byte), else 1 */
static u32 elen(u32 r) { return g_re[r] == '\\' && r + 1 < g_rn ? 2 : 1; }

static int ematch(u32 r, int c) {
    if (elen(r) == 2) return fold(g_re[r + 1]) == fold(c);
    if (g_re[r] == '.') return 1;
    return fold(g_re[r]) == fold(c);
}

/* the pattern from r matched at text position ti, through to its end */
static int here(u32 r, u32 ti) {
    u32 sp = 0;
    for (;;) {
        if (r == g_rn) return 1;
        if (r + 1 == g_rn && g_re[r] == '$') {
            if (ti == g_tn) return 1;
        } else {
            const u32 el = elen(r);
            if (r + el < g_rn && g_re[r + el] == '*') {           /* zero of it first; come back for more */
                g_bt[sp].r = (unsigned short)r; g_bt[sp].ti = (unsigned short)ti; sp++;
                r += el + 1;
                continue;
            }
            if (ti < g_tn && ematch(r, g_t[ti])) { r += el; ti++; continue; }
        }
        /* failed: the latest * takes one more byte, if it can; else the one before it */
        for (;;) {
            if (!sp) return 0;
            const u32 sr = g_bt[sp - 1].r, sti = g_bt[sp - 1].ti;
            if (sti < g_tn && ematch(sr, g_t[sti])) {
                g_bt[sp - 1].ti = (unsigned short)(sti + 1);
                r = sr + elen(sr) + 1; ti = sti + 1;
                break;
            }
            sp--;
        }
    }
}

int re_match(const unsigned char *re, u32 rn, const unsigned char *t, u32 tn, int icase) {
    g_re = re; g_rn = rn; g_t = t; g_tn = tn; g_ic = icase;
    if (rn && re[0] == '^') { g_re++; g_rn--; return here(0, 0); }
    for (u32 ti = 0; ti <= tn; ti++)
        if (here(0, ti)) return 1;
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
