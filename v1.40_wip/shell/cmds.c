/* cmds.c: the commands (SHELL-SPEC section 9, with NOTES 1.4). Errors go to the console as "<cmd>: <path>: <message>"
 * and a command goes on to its next argument; an interrupted one prints ^C and stops. */
#include "shell.h"

/* ------------------------------------------------------------------ helpers */
int arg_path(const char *cmd, const char *arg, char *out) {
    const int e = path_resolve(arg, out);
    if (e) { err_path(cmd, arg, e); return -1; }
    return 0;
}

/* Options before operands: spec lists the letters, "n:" one that takes a value (-n N or -nN). flags gets bit i for
 * spec's i-th letter, vals[i] its value. Returns the first operand's index, or -1 with the usage shown. */
int opts(int argc, char **argv, const char *spec, u32 *flags, char **vals) {
    int i = 1;
    *flags = 0;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        for (const char *p = argv[i] + 1; *p; p++) {
            u32 k = 0;
            const char *s = spec;
            for (; *s && *s != *p; s++) if (*s != ':') k++;
            if (!*s || *p == ':') { usage(argv[0]); return -1; }
            *flags |= 1u << k;
            if (s[1] == ':') {
                if (p[1]) vals[k] = (char *)p + 1;
                else if (i + 1 < argc) vals[k] = argv[++i];
                else { usage(argv[0]); return -1; }
                break;
            }
        }
    }
    return i;
}

static int num_opt(const char *cmd, const char *s, u64 *v, int hex) {
    if (str_num(s, v, hex)) { usage(cmd); return -1; }
    return 0;
}

/* a file opened to read, by its argument; the error shown */
static int open_read(const char *cmd, const char *arg) {
    if (arg_path(cmd, arg, g_path1)) return -1;
    const int fd = fs_open(g_path1, FS_READ);
    if (fd < 0) { err_path(cmd, arg, fd); return -1; }
    return fd;
}

/* a file's fs_stat by its argument, as a file: a directory is "is a directory" */
static int stat_file(const char *cmd, const char *arg, struct fs_stat *st) {
    if (arg_path(cmd, arg, g_path1)) return -1;
    const int e = fs_stat(g_path1, st);
    if (e < 0) { err_path(cmd, arg, e); return -1; }
    if (st->type == FS_T_DIR) { err_path(cmd, arg, FS_EISDIR); return -1; }
    return 0;
}

/* the file's page pg into g_page, once */
static u64 g_loaded;
static int load(const char *cmd, const char *arg, int fd, u64 pg) {
    if (pg == g_loaded) return 0;
    const int e = fs_read_page(fd, pg, g_page);
    if (e < 0) { err_path(cmd, arg, e); g_loaded = ~0ull; return -1; }
    g_loaded = pg;
    return 0;
}

/* ------------------------------------------------------------------ help, pwd, cd, echo */
static void cmd_help(int argc, char **argv) {
    (void)argv;
    if (argc != 1) { usage("help"); return; }
    for (const struct cmd *c = g_cmds; c->name; c++) pf("%s\n", c->usage);
    pf("grep: a line over 1024 bytes is matched, printed and counted as lines of 1024\n");
}

static void cmd_pwd(int argc, char **argv) {
    (void)argv;
    if (argc != 1) { usage("pwd"); return; }
    pf("%s\n", g_cwd);
}

static void cmd_cd(int argc, char **argv) {
    if (argc > 2) { usage("cd"); return; }
    const char *arg = argc == 2 ? argv[1] : "/";
    if (arg_path("cd", arg, g_path2)) return;
    struct fs_stat st;
    const int e = fs_stat(g_path2, &st);
    if (e < 0) { err_path("cd", arg, e); return; }
    if (st.type != FS_T_DIR) { err_path("cd", arg, FS_ENOTDIR); return; }
    str_cpy(g_cwd, g_path2);
}

static void cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) pf(i > 1 ? " %s" : "%s", argv[i]);
    pf("\n");
}

/* ------------------------------------------------------------------ ls */
/* The directory's entries sorted by name, in byte order (owner, 2026-11-20), within the page buffer: each pass reads
 * the whole directory and keeps the LS_BATCH smallest entries above the last one printed (NOTES 1.4.2). */
#define LS_BATCH (SH_PAGE / sizeof(struct fs_dirent))
static char g_last[96];

static void ls_line(const char *name, u32 type, u32 flags, u64 length, int lng) {
    const char *slash = type == FS_T_DIR ? "/" : "";
    if (lng)
        pf("%c%c%c%c %12llu %s%s\n", type == FS_T_DIR ? 'd' : type == FS_T_FILE ? '-' : '?',
           (flags & FS_F_FROZEN) ? 'f' : '-', (flags & FS_F_SPARSE) ? 's' : '-', (flags & FS_F_COW) ? 'c' : '-',
           length, name, slash);
    else
        pf("%s%s\n", name, slash);
}

static void ls_dir(const char *arg, const char *path, int lng) {
    struct fs_dirent *b = (struct fs_dirent *)(void *)g_page;
    struct fs_dirent e;
    int have_last = 0;
    for (;;) {
        u32 n = 0, pos = 0;
        int r;
        while ((r = fs_list(path, &pos, &e)) == 1) {
            if (have_last && str_cmp(e.name, g_last) <= 0) continue;
            if (n == LS_BATCH && str_cmp(e.name, b[n - 1].name) >= 0) continue;
            u32 i = n < LS_BATCH ? n++ : n - 1;          /* insertion, the largest dropped when full */
            while (i && str_cmp(e.name, b[i - 1].name) < 0) { b[i] = b[i - 1]; i--; }
            b[i] = e;
        }
        if (r < 0) { err_path("ls", arg, r); return; }
        for (u32 i = 0; i < n; i++) ls_line(b[i].name, b[i].type, b[i].flags, b[i].length, lng);
        if (interrupted()) { stopped(); return; }
        if (n < LS_BATCH) return;
        str_cpy(g_last, b[n - 1].name);
        have_last = 1;
    }
}

static void cmd_ls(int argc, char **argv) {
    u32 f;
    char *v[1];
    int i = opts(argc, argv, "l", &f, v);
    if (i < 0) return;
    const int lng = f & 1, many = argc - i > 1;
    static char *dot[] = {"."};
    char **list = argv + i;
    int n = argc - i;
    if (!n) { list = dot; n = 1; }
    int shown = 0;
    for (int k = 0; k < n && !g_stop; k++) {
        if (arg_path("ls", list[k], g_path2)) continue;
        struct fs_stat st;
        const int e = fs_stat(g_path2, &st);
        if (e < 0) { err_path("ls", list[k], e); continue; }
        if (st.type != FS_T_DIR) { ls_line(list[k], st.type, st.flags, st.length, lng); shown = 1; continue; }
        if (many) { pf(shown ? "\n%s:\n" : "%s:\n", list[k]); shown = 1; }
        ls_dir(list[k], g_path2, lng);
        shown = 1;
    }
}

/* ------------------------------------------------------------------ cat, more */
/* No file: lines typed at the console, each written out at its Enter, until Ctrl-D on an empty line (owner,
 * 2026-11-20). Ctrl-C drops the line being typed and stops; the lines before it stay written. The line is edited as
 * the prompt's is; g_path2 holds it (cat resolves no path). */
static void cat_console(void) {
    while (edit_line(g_path2, SH_LINE, 1) == 1) pf("%s\n", g_path2);
}

static void cmd_cat(int argc, char **argv) {
    if (argc < 2) { cat_console(); return; }
    for (int i = 1; i < argc && !g_stop; i++) {
        const int fd = open_read("cat", argv[i]);
        if (fd < 0) continue;
        long n;
        while ((n = fs_read(fd, g_page, SH_PAGE)) > 0) {
            out_n(g_page, (u32)n);
            if (interrupted()) { stopped(); break; }
        }
        if (n < 0) err_path("cat", argv[i], (int)n);
        fs_close(fd);
    }
}

/* Pages of 23 rows: a line over 80 columns is several. The prompt shows the share of the bytes shown so far. */
static void cmd_more(int argc, char **argv) {
    if (argc != 2) { usage("more"); return; }
    if (sink_redirected()) { cmd_cat(argc, argv); return; }
    struct fs_stat st;
    if (stat_file("more", argv[1], &st)) return;
    const int fd = open_read("more", argv[1]);
    if (fd < 0) return;
    u32 rows = 0, col = 0, limit = 23;
    u64 shown = 0;
    long n;
    while ((n = fs_read(fd, g_page, SH_PAGE)) > 0) {
        for (long i = 0; i < n; i++) {
            const int c = g_page[i];
            if (c != '\n' && col == 80) { rows++; col = 0; }
            if (rows >= limit) {
                const u64 pct = st.length ? shown * 100 / st.length : 100;
                epf("--More-- (%llu%%)", pct);
                int k;
                do k = con_getc(); while (k >= 0 && k != ' ' && k != '\r' && k != '\n' && k != 'q' && k != 0x03);
                con_putc('\r');                          /* the prompt erased: "--More-- (" N "%)" */
                for (u32 w = pct < 10 ? 13 : pct < 100 ? 14 : 15; w; w--) con_putc(' ');
                con_putc('\r');
                if (k < 0 || k == 'q' || k == 0x03) { fs_close(fd); return; }
                limit = k == ' ' ? 23 : 1;
                rows = 0;
            }
            out_c(c);
            shown++;
            if (c == '\n') { rows++; col = 0; } else col++;
        }
    }
    if (n < 0) err_path("more", argv[1], (int)n);
    fs_close(fd);
}

/* ------------------------------------------------------------------ head, tail */
static int lines_opt(const char *cmd, int argc, char **argv, u64 *lines) {
    u32 f;
    char *v[1];
    const int i = opts(argc, argv, "n:", &f, v);
    if (i < 0) return -1;
    *lines = 10;
    if ((f & 1) && num_opt(cmd, v[0], lines, 0)) return -1;
    if (i == argc) { usage(cmd); return -1; }
    return i;
}

static void header(int many, int *shown, const char *arg) {
    if (many) pf(*shown ? "\n==> %s <==\n" : "==> %s <==\n", arg);
    *shown = 1;
}

static void cmd_head(int argc, char **argv) {
    u64 lines;
    const int first = lines_opt("head", argc, argv, &lines);
    if (first < 0) return;
    int shown = 0;
    for (int i = first; i < argc && !g_stop; i++) {
        const int fd = open_read("head", argv[i]);
        if (fd < 0) continue;
        header(argc - first > 1, &shown, argv[i]);
        u64 seen = 0;
        long n = 0;
        while (seen < lines && (n = fs_read(fd, g_page, SH_PAGE)) > 0) {
            long k = 0;
            while (k < n && seen < lines) if (g_page[k++] == '\n') seen++;
            out_n(g_page, (u32)k);
            if (interrupted()) { stopped(); break; }
        }
        if (n < 0) err_path("head", argv[i], (int)n);
        fs_close(fd);
    }
}

/* The last N lines, found by reading backward from the last page with fs_read_page, never from the start; a last line
 * without a newline counts as a line. */
static void cmd_tail(int argc, char **argv) {
    u64 lines;
    const int first = lines_opt("tail", argc, argv, &lines);
    if (first < 0) return;
    int shown = 0;
    for (int i = first; i < argc && !g_stop; i++) {
        struct fs_stat st;
        if (stat_file("tail", argv[i], &st)) continue;
        const int fd = fs_open(g_path1, FS_READ);
        if (fd < 0) { err_path("tail", argv[i], fd); continue; }
        header(argc - first > 1, &shown, argv[i]);
        const u64 len = st.length;
        u64 start = len, seen = 0;
        g_loaded = ~0ull;
        int bad = 0;
        if (lines && len) {
            start = 0;
            for (u64 at = len; at-- > 0;) {
                if ((at + 1) % SH_PAGE == 0 && interrupted()) { stopped(); bad = 1; break; }   /* a page done */
                if (load("tail", argv[i], fd, at / SH_PAGE)) { bad = 1; break; }
                if (g_page[at % SH_PAGE] == '\n' && at != len - 1 && ++seen == lines) { start = at + 1; break; }
            }
        }
        for (u64 at = start; !bad && at < len;) {
            if (load("tail", argv[i], fd, at / SH_PAGE)) break;
            const u32 off = (u32)(at % SH_PAGE);
            u32 n = SH_PAGE - off;
            if (n > len - at) n = (u32)(len - at);
            out_n(g_page + off, n);
            at += n;
            if (interrupted()) { stopped(); break; }
        }
        fs_close(fd);
    }
}

/* ------------------------------------------------------------------ wc */
static void cmd_wc(int argc, char **argv) {
    if (argc < 2) { usage("wc"); return; }
    u64 tl = 0, tw = 0, tb = 0;
    for (int i = 1; i < argc && !g_stop; i++) {
        const int fd = open_read("wc", argv[i]);
        if (fd < 0) continue;
        u64 l = 0, w = 0, b = 0;
        int in_word = 0;
        long n;
        while ((n = fs_read(fd, g_page, SH_PAGE)) > 0) {
            for (long k = 0; k < n; k++) {
                const int c = g_page[k];
                const int space = c == ' ' || (c >= '\t' && c <= '\r');
                if (c == '\n') l++;
                if (!space && !in_word) w++;
                in_word = !space;
            }
            b += (u64)n;
            if (interrupted()) { stopped(); break; }
        }
        if (n < 0) err_path("wc", argv[i], (int)n);
        fs_close(fd);
        if (g_stop) break;
        pf("%8llu %8llu %8llu %s\n", l, w, b, argv[i]);
        tl += l; tw += w; tb += b;
    }
    if (argc > 2 && !g_stop) pf("%8llu %8llu %8llu total\n", tl, tw, tb);
}

/* ------------------------------------------------------------------ hexdump */
static void cmd_hexdump(int argc, char **argv) {
    u32 f;
    char *v[2];
    const int i = opts(argc, argv, "s:n:", &f, v);
    if (i < 0) return;
    if (argc - i != 1) { usage("hexdump"); return; }
    u64 off = 0, count = ~0ull;
    if ((f & 1) && num_opt("hexdump", v[0], &off, 1)) return;
    if ((f & 2) && num_opt("hexdump", v[1], &count, 1)) return;
    const char *arg = argv[i];
    struct fs_stat st;
    if (stat_file("hexdump", arg, &st)) return;
    const int fd = fs_open(g_path1, FS_READ);
    if (fd < 0) { err_path("hexdump", arg, fd); return; }
    u64 end = st.length;
    if (off > end) off = end;
    if (count < end - off) end = off + count;
    g_loaded = ~0ull;
    u64 at = off;
    while (at < end) {
        unsigned char b[16];
        u32 k = 0;
        for (; k < 16 && at + k < end; k++) {
            if (load("hexdump", arg, fd, (at + k) / SH_PAGE)) { fs_close(fd); return; }
            b[k] = g_page[(at + k) % SH_PAGE];
        }
        pf("%08llx ", at);
        for (u32 j = 0; j < 16; j++) {
            if (j == 8) out_c(' ');
            if (j < k) pf(" %02x", b[j]); else pf("   ");
        }
        pf("  |");
        for (u32 j = 0; j < k; j++) out_c(b[j] >= 0x20 && b[j] <= 0x7E ? b[j] : '.');
        pf("|\n");
        at += k;
        if (interrupted()) { stopped(); fs_close(fd); return; }
    }
    pf("%08llx\n", end);
    fs_close(fd);
}

/* ------------------------------------------------------------------ stat, df */
static void cmd_stat(int argc, char **argv) {
    if (argc < 2) { usage("stat"); return; }
    int shown = 0;
    for (int i = 1; i < argc; i++) {
        if (arg_path("stat", argv[i], g_path1)) continue;
        struct fs_stat st;
        const int e = fs_stat(g_path1, &st);
        if (e < 0) { err_path("stat", argv[i], e); continue; }
        if (shown) pf("\n");
        shown = 1;
        pf("name: %s\n", argv[i]);
        if (st.type == FS_T_DIR) pf("type: dir\n");
        else if (st.type == FS_T_FILE) pf("type: file\n");
        else pf("type: %u\n", st.type);
        pf("length: %llu\npages: %llu\ndepth: %u\nflags:", st.length, st.pages, st.depth);
        if (st.flags & FS_F_FROZEN) pf(" frozen");
        if (st.flags & FS_F_SPARSE) pf(" sparse");
        if (st.flags & FS_F_COW) pf(" cow");
        if (!(st.flags & (FS_F_FROZEN | FS_F_SPARSE | FS_F_COW))) pf(" none");
        pf("\ncopies: %u\n", st.copies);
    }
}

/* pages x 4096 in MB, one decimal, rounded: pages / 256 */
static void mb(u64 pages, const char *what) {
    const u64 tenths = (pages * 10 + 128) / 256;
    pf("%llu.%llu MB %s", tenths / 10, tenths % 10, what);
}

static void cmd_df(int argc, char **argv) {
    (void)argv;
    if (argc != 1) { usage("df"); return; }
    u64 total, free;
    const int e = fs_df(&total, &free);
    if (e < 0) { err_cmd("df", e); return; }
    pf("pages: %llu total, %llu used, %llu free\n", total, total - free, free);
    pf("bytes: "); mb(total, "total, "); mb(total - free, "used, "); mb(free, "free\n");
}

/* ------------------------------------------------------------------ mkdir, rmdir, rm */
static void cmd_mkdir(int argc, char **argv) {
    if (argc < 2) { usage("mkdir"); return; }
    for (int i = 1; i < argc; i++) {
        if (arg_path("mkdir", argv[i], g_path1)) continue;
        const int e = fs_mkdir(g_path1);
        if (e < 0) err_path("mkdir", argv[i], e);
    }
}

static void remove_as(const char *cmd, int dir, int argc, char **argv) {
    if (argc < 2) { usage(cmd); return; }
    for (int i = 1; i < argc; i++) {
        if (arg_path(cmd, argv[i], g_path1)) continue;
        struct fs_stat st;
        int e = fs_stat(g_path1, &st);
        if (e < 0) { err_path(cmd, argv[i], e); continue; }
        if (dir && st.type != FS_T_DIR) { err_path(cmd, argv[i], FS_ENOTDIR); continue; }
        if (!dir && st.type == FS_T_DIR) { err_path(cmd, argv[i], FS_EISDIR); continue; }
        e = fs_remove(g_path1);
        if (e < 0) err_path(cmd, argv[i], e);
    }
}
static void cmd_rmdir(int argc, char **argv) { remove_as("rmdir", 1, argc, argv); }
static void cmd_rm(int argc, char **argv) { remove_as("rm", 0, argc, argv); }

/* ------------------------------------------------------------------ the table, in section 9's order */
const struct cmd g_cmds[] = {
    {"help", "help", cmd_help},
    {"ls", "ls [-l] [path...]", cmd_ls},
    {"cd", "cd [dir]", cmd_cd},
    {"pwd", "pwd", cmd_pwd},
    {"cat", "cat [file...]", cmd_cat},
    {"more", "more file", cmd_more},
    {"head", "head [-n N] file...", cmd_head},
    {"tail", "tail [-n N] file...", cmd_tail},
    {"grep", "grep [-n] [-i] [-c] [-v] pattern file...", cmd_grep},
    {"wc", "wc file...", cmd_wc},
    {"hexdump", "hexdump [-s off] [-n len] file", cmd_hexdump},
    {"stat", "stat path...", cmd_stat},
    {"df", "df", cmd_df},
    {"echo", "echo [arg...]", cmd_echo},
    {"mkdir", "mkdir dir...", cmd_mkdir},
    {"rmdir", "rmdir dir...", cmd_rmdir},
    {"rm", "rm file...", cmd_rm},
    {0, 0, 0},
};

const struct cmd *cmd_find(const char *name) {
    for (const struct cmd *c = g_cmds; c->name; c++)
        if (str_eq(c->name, name)) return c;
    return 0;
}

void usage(const char *name) {
    const struct cmd *c = cmd_find(name);
    epf("usage: %s\n", c ? c->usage : name);
}
