/* fs.h: the file system's API, v1.40 (v1.40_wip/planning/FS-API.md, section 5). Usable from freestanding C on a tile
 * and from C++ on a host. Every call returns a negative error (FS_E*) or a value of 0 or more.
 *
 * Pages are named by their address in units of 128 bytes (23.5), so a page's address is a multiple of 32.
 * A path is absolute from '/', else from the caller's tile's working directory (fs_chdir); "." and ".." may appear
 * anywhere. At most 1,023 bytes and 10 names; a name at most 95 bytes; no empty name, no trailing '/'. */
#ifndef FS_H
#define FS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t u32;
typedef uint64_t u64;

/* ---- the errors (5.5) */
#define FS_ENOENT        (-1)
#define FS_EEXIST        (-2)
#define FS_ENOTDIR       (-3)
#define FS_EISDIR        (-4)
#define FS_ENOTEMPTY     (-5)
#define FS_EROFS         (-6)   /* frozen, or a COW file */
#define FS_EBUSY         (-7)   /* has copies, or is open */
#define FS_ENOSPC        (-8)
#define FS_EBADF         (-9)
#define FS_EINVAL        (-10)
#define FS_EIO           (-11)
#define FS_ENAMETOOLONG  (-12)
#define FS_ESHUTDOWN     (-13)

/* ---- fs_open's flags (5.2) */
#define FS_READ    1u   /* sequential fs_read; fs_read_page */
#define FS_APPEND  2u   /* fs_write appends; creates the file if missing */
#define FS_WRITE   4u   /* fs_write_page, random page writes; creates the file if missing */
#define FS_TRUNC   8u   /* with FS_APPEND: empty the file first */
#define FS_ROOT   16u   /* alone: a COW file held for the Updater or the Tracer; one owner; the master and tracer tiles' */

/* ---- types and flags, as fs_list and fs_stat give them */
#define FS_T_DIR     1u
#define FS_T_FILE    2u
#define FS_F_FROZEN  1u
#define FS_F_SPARSE  2u
#define FS_F_COW     4u

struct fs_dirent { char name[96]; u32 type; u32 flags; u64 length; };
struct fs_stat   { u32 type; u32 flags; u64 length; u64 pages; u32 copies; u32 depth; };

/* ---- pages (5.1): served by the tracer tile's page service, from the calling tile's pool (the master tile's,
 *      the memory tile's); only the acquire says what the pages are for */
int  fs_reserve_pages(u32 n);                         /* held in the caller's pool for one request */
int  fs_acquire_cow_pages(u32 *pages, u32 k);         /* k <= 256, for the Updater or the Tracer: marked "may be cached" */
int  fs_acquire_pages(u32 *pages, u32 k);             /* k <= 256, for a directory or a streamed file: not marked */
int  fs_release_pages(const u32 *pages, u32 k);
int  fs_unreserve(void);

/* ---- streamed files (5.2): every caller */
int  fs_open(const char *path, u32 flags);
long fs_read(int fd, void *buf, long n);
long fs_write(int fd, const void *buf, long n);
int  fs_flush(int fd);
int  fs_close(int fd);
int  fs_read_page(int fd, u64 page, void *buf);
int  fs_write_page(int fd, u64 page, const void *buf);
int  fs_fprintf(int fd, const char *fmt, ...);
int  fs_chdir(const char *path);                      /* the caller's tile's working directory */
int  fs_mkdir(const char *path);
int  fs_remove(const char *path);
int  fs_remove_directory(const char *path);           /* a directory and its files: no subdirectory, nothing open */
int  fs_list(const char *path, u32 *pos, struct fs_dirent *e);
int  fs_stat(const char *path, struct fs_stat *st);
int  fs_df(u64 *total_pages, u64 *free_pages);

/* ---- COW files (5.3): the master tile alone; cow_root also the tracer tile's */
int  cow_create(const char *path, u64 length_pages, u32 *root, u32 *depth);
int  cow_copy(const char *src, const char *dst, u32 *root, u32 *depth);
int  cow_commit(const char *path);                    /* the root, once the run that makes it is done */
int  cow_freeze(const char *path);
int  cow_root(int fd, u32 *root, u32 *depth);          /* fd opened FS_ROOT */

/* ---- the file system as a whole (5.4): the master tile alone */
#define FS_STARTED_FORMAT 1   /* fs_startup's mode: no superblock, formatted */
#define FS_STARTED_MOUNT  2   /* shut down cleanly: mounted */
#define FS_STARTED_FSCK   3   /* not shut down cleanly: checked and repaired */
int  fs_startup(int format);                          /* format: nonzero, a format whatever the superblock says */
int  fs_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* FS_H */
