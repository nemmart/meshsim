/* glob.c: * in file names (owner, 2026-11-20). An argument with an unquoted * in its last name is a pattern: * matches
 * any run of bytes, every other byte itself. A command takes its file arguments one at a time from ops_next; a
 * pattern gives its matches there, sorted in byte order, as ls sorts, each as the argument's directory part as typed
 * and the matched name. Nothing is expanded at the parse, so a pattern has no limit on its matches: each pass over the
 * directory keeps the GL_BATCH smallest names above the last one given (as ls does, NOTES 1.4.2). A * in a directory
 * part, or in a name to be made (mkdir, a redirection), is an error. */
#include "shell.h"

#define GL_BATCH 16

u32 g_globmask;                     /* bit i: argv[i] has an unquoted * (set by the parser) */
static char g_gb[GL_BATCH][96];     /* the batch: names, sorted */
static char g_glast[96];
static char g_arg[SH_LINE];         /* the argument given: the directory part as typed, then the name */

int has_star(const char *s) {
    for (; *s; s++) if (*s == '*') return 1;
    return 0;
}

/* p (with *) against s, whole: iterative, back to the last * on a mismatch */
static int wild(const char *p, const char *s) {
    const char *star = 0, *from = 0;
    while (*s) {
        if (*p == '*') { star = p++; from = s; continue; }
        if (*p == *s) { p++; s++; continue; }
        if (!star) return 0;
        p = star + 1;
        s = ++from;
    }
    while (*p == '*') p++;
    return !*p;
}

void ops_start(struct ops *o, const char *cmd, int argc, char **argv, int first) {
    o->cmd = cmd; o->argc = argc; o->argv = argv; o->i = first; o->pat = 0; o->count = 0;
}

/* the next pass over the pattern's directory: 0, or -1 (the error shown) */
static int refill(struct ops *o) {
    const char *a = o->argv[o->i - 1];
    if (o->dlen) { memcpy(g_arg, a, o->dlen); g_arg[o->dlen] = 0; }
    else { g_arg[0] = '.'; g_arg[1] = 0; }
    int r = path_resolve(g_arg, g_path2);
    if (r) { err_path(o->cmd, a, r); return -1; }
    const char *pat = a + o->dlen;
    struct fs_dirent e;
    u32 n = 0, pos = 0;
    while ((r = fs_list(g_path2, &pos, &e)) == 1) {
        if (!wild(pat, e.name) || (o->have_last && str_cmp(e.name, g_glast) <= 0)) continue;
        if (n == GL_BATCH && str_cmp(e.name, g_gb[n - 1]) >= 0) continue;
        u32 i = n < GL_BATCH ? n++ : n - 1;
        while (i && str_cmp(e.name, g_gb[i - 1]) < 0) { str_cpy(g_gb[i], g_gb[i - 1]); i--; }
        str_cpy(g_gb[i], e.name);
    }
    if (r < 0) { err_path(o->cmd, a, r); return -1; }
    o->n = n; o->k = 0; o->matched += n;
    if (n == GL_BATCH) { str_cpy(g_glast, g_gb[n - 1]); o->have_last = 1; }
    return 0;
}

const char *ops_next(struct ops *o) {
    for (;;) {
        if (g_stop) return 0;
        if (o->pat) {
            if (o->k < o->n) {
                const char *name = g_gb[o->k++];
                if (o->dlen + str_len(name) > SH_LINE - 1) { err_path(o->cmd, name, FS_ENAMETOOLONG); continue; }
                memcpy(g_arg, o->argv[o->i - 1], o->dlen);
                str_cpy(g_arg + o->dlen, name);
                o->count++;
                return g_arg;
            }
            if (o->n == GL_BATCH) {                       /* a full batch: there may be more */
                if (refill(o)) o->pat = 0;
                continue;
            }
            if (!o->matched) epf("%s: %s: no match\n", o->cmd, o->argv[o->i - 1]);
            o->pat = 0;
        }
        if (o->i >= o->argc) return 0;
        const char *a = o->argv[o->i++];
        if (!((g_globmask >> (o->i - 1)) & 1)) { o->count++; return a; }
        u32 d = 0;
        for (u32 k = 0; a[k]; k++) if (a[k] == '/') d = k + 1;
        for (u32 k = 0; k < d; k++)
            if (a[k] == '*') { epf("%s: %s: * in a directory name\n", o->cmd, a); d = ~0u; break; }
        if (d == ~0u) continue;
        o->dlen = d; o->pat = 1; o->have_last = 0; o->matched = 0;
        if (refill(o)) o->pat = 0;
    }
}

/* a command's one file: a pattern must match exactly one */
const char *one_operand(const char *cmd, int argc, char **argv, int i) {
    (void)argc;
    struct ops o;
    ops_start(&o, cmd, i + 1, argv, i);
    const char *a = ops_next(&o);
    if (a && o.pat && o.n > 1) { epf("%s: %s: more than one match\n", cmd, argv[i]); return 0; }
    return a;
}
