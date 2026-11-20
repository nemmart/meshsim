/* shell.h: what the shell's files share. Freestanding C99: no libc, no malloc, no floats (SHELL-SPEC section 3). */
#ifndef SHELL_H
#define SHELL_H

#include <stdarg.h>
#include <stddef.h>

#include "../fs/fs.h"
#include "console.h"

#define SH_LINE   1024          /* the line, the cwd, a path scratch: 1,023 bytes and a NUL */
#define SH_PAGE   4096          /* the page buffer and the redirect buffer */
#define SH_ARGS   16            /* tokens on a line */

/* ---- str.c: the string helpers, and the four functions the compilers call */
void  *memcpy(void *d, const void *s, size_t n);
void  *memset(void *d, int c, size_t n);
void  *memmove(void *d, const void *s, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
u32    str_len(const char *s);
int    str_cmp(const char *a, const char *b);          /* byte order, bytes unsigned */
int    str_eq(const char *a, const char *b);
void   str_cpy(char *d, const char *s);
int    str_num(const char *s, u64 *v, int hex);        /* decimal, or 0x hex if hex; 0, or -1 if not a number */

/* ---- div.c: our own division (section 3) */
u32 udiv32(u32 n, u32 d, u32 *rem);
u64 udiv64(u64 n, u64 d, u64 *rem);

/* ---- fmt.c: output (sections 7 and 11) */
void out_c(int c);                                     /* a byte to the current sink */
void out_n(const void *b, u32 n);
void pf(const char *fmt, ...);                         /* formatted, to the current sink */
void epf(const char *fmt, ...);                        /* formatted, to the console always */
void con_s(const char *s);                             /* a string to the console, as it is (no \n mapping) */
int  sink_open(const char *abs, const char *shown, u32 flags);   /* a redirection: 0, or the error (reported) */
void sink_end(void);                                   /* the command's end: flush and close a redirection */
int  sink_redirected(void);
int  interrupted(void);                                /* Ctrl-C waiting; other bytes waiting are dropped */

/* ---- shell.c: paths, errors */
extern char g_cwd[SH_LINE];
extern char g_path1[SH_LINE], g_path2[SH_LINE];
extern unsigned char g_page[SH_PAGE];
extern int g_stop;                                     /* set by an interrupted command: skip its other arguments */
int  path_resolve(const char *arg, char *out);         /* 0, or FS_ENAMETOOLONG */
const char *fs_msg(int e);
void err_path(const char *cmd, const char *path, int e);
void err_cmd(const char *cmd, int e);
void stopped(void);                                    /* prints ^C and sets g_stop */

/* ---- cmds.c */
struct cmd { const char *name; const char *usage; void (*fn)(int argc, char **argv); };
extern const struct cmd g_cmds[];
const struct cmd *cmd_find(const char *name);
void usage(const char *name);
int  opts(int argc, char **argv, const char *spec, u32 *flags, char **vals);   /* first operand, or -1 (usage shown) */
int  arg_path(const char *cmd, const char *arg, char *out);                   /* path_resolve, error shown: 0 or -1 */

/* ---- grep.c */
int  re_match(const unsigned char *re, u32 rn, const unsigned char *t, u32 tn, int icase);
void cmd_grep(int argc, char **argv);

/* ---- the entry */
void shell_main(void);

#endif /* SHELL_H */
