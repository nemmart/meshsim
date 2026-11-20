/* retest.c: grep.c's matcher (iterative, with its own backtracking stack) against Pike's recursive matcher, as in The
 * Practice of Programming with \ added, on random patterns and lines over a small alphabet with every metacharacter.
 * A hosted program; it includes grep.c and stubs what grep's command needs. */
#include <stdio.h>
#include <string.h>

#include "../grep.c"

/* the stubs: cmd_grep is not run here */
void pf(const char *f, ...) { (void)f; }
void epf(const char *f, ...) { (void)f; }
void out_c(int c) { (void)c; }
void out_n(const void *b, u32 n) { (void)b; (void)n; }
int opts(int argc, char **argv, const char *spec, u32 *flags, char **vals) { (void)argc; (void)argv; (void)spec; (void)flags; (void)vals; return -1; }
void usage(const char *n) { (void)n; }
int arg_path(const char *c, const char *a, char *o) { (void)c; (void)a; (void)o; return -1; }
void err_path(const char *c, const char *p, int e) { (void)c; (void)p; (void)e; }
void stopped(void) {}
int interrupted(void) { return 0; }
u32 str_len(const char *s) { return (u32)strlen(s); }
int g_stop;
char g_path1[SH_LINE];
unsigned char g_page[SH_PAGE];
int fs_open(const char *p, u32 f) { (void)p; (void)f; return -1; }
long fs_read(int fd, void *b, long n) { (void)fd; (void)b; (void)n; return 0; }
int fs_close(int fd) { (void)fd; return 0; }

/* Pike's, recursive, with \ and lengths */
static const unsigned char *T; static u32 TN; static int IC;
static int f2(int c) { return IC && c >= 'A' && c <= 'Z' ? c + 32 : c; }
static u32 el2(const unsigned char *re, u32 rn) { return re[0] == '\\' && rn > 1 ? 2 : 1; }
static int em2(const unsigned char *re, u32 el, int c) { return el == 2 ? f2(re[1]) == f2(c) : re[0] == '.' || f2(re[0]) == f2(c); }
static int here2(const unsigned char *re, u32 rn, u32 ti) {
    if (rn == 0) return 1;
    if (rn == 1 && re[0] == '$') return ti == TN;
    const u32 el = el2(re, rn);
    if (el < rn && re[el] == '*') {
        for (;;) {
            if (here2(re + el + 1, rn - el - 1, ti)) return 1;
            if (ti >= TN || !em2(re, el, T[ti])) return 0;
            ti++;
        }
    }
    return ti < TN && em2(re, el, T[ti]) && here2(re + el, rn - el, ti + 1);
}
static int match2(const unsigned char *re, u32 rn, const unsigned char *t, u32 tn, int ic) {
    T = t; TN = tn; IC = ic;
    if (rn && re[0] == '^') return here2(re + 1, rn - 1, 0);
    for (u32 ti = 0; ti <= tn; ti++) if (here2(re, rn, ti)) return 1;
    return 0;
}

static unsigned long long rng = 88172645463325252ull;
static unsigned rnd(unsigned n) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (unsigned)(rng % n); }

int main(void) {
    static const char PAT[] = "abAB.*^$\\", TXT[] = "abAB.*^$\\";
    unsigned char re[16], t[24];
    long n = 0, bad = 0, hits = 0;
    for (long i = 0; i < 1000000; i++) {
        const u32 rn = rnd(12), tn = rnd(20);
        for (u32 k = 0; k < rn; k++) re[k] = (unsigned char)PAT[rnd(sizeof PAT - 1)];
        for (u32 k = 0; k < tn; k++) t[k] = (unsigned char)TXT[rnd(sizeof TXT - 1)];
        const int ic = (int)rnd(2);
        const int a = re_match(re, rn, t, tn, ic), b = match2(re, rn, t, tn, ic);
        n++; hits += b;
        if (a != b && bad++ < 10) printf("differ: pattern %.*s line %.*s icase %d: %d %d\n", (int)rn, re, (int)tn, t, ic, a, b);
    }
    printf("retest: %ld patterns and lines, %ld matched, %ld differ\n", n, hits, bad);
    return bad != 0;
}
