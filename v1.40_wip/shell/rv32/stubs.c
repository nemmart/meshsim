/* stubs.c: the console and the fs.h calls the shell makes, stubbed, for the link-only RV32 build. Only section 5.2's
 * calls are here (FS-API 6): a call the shell may not make would be an undefined symbol, and the link would fail. */
#include "../shell.h"

int  con_getc(void) { return -1; }
int  con_poll(void) { return -1; }
void con_putc(int c) { (void)c; }

int  fs_open(const char *path, u32 flags) { (void)path; (void)flags; return FS_EIO; }
long fs_read(int fd, void *buf, long n) { (void)fd; (void)buf; (void)n; return FS_EIO; }
long fs_write(int fd, const void *buf, long n) { (void)fd; (void)buf; (void)n; return FS_EIO; }
int  fs_close(int fd) { (void)fd; return FS_EIO; }
int  fs_read_page(int fd, u64 page, void *buf) { (void)fd; (void)page; (void)buf; return FS_EIO; }
int  fs_mkdir(const char *path) { (void)path; return FS_EIO; }
int  fs_remove(const char *path) { (void)path; return FS_EIO; }
int  fs_list(const char *path, u32 *pos, struct fs_dirent *e) { (void)path; (void)pos; (void)e; return FS_EIO; }
int  fs_stat(const char *path, struct fs_stat *st) { (void)path; (void)st; return FS_EIO; }
int  fs_df(u64 *total, u64 *free) { (void)total; (void)free; return FS_EIO; }
