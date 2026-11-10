# SHELL-SPEC: the console shell (draft, 2026-11-10)

A small shell on C&C 1, over its UART, for looking around the file system: normally, and above all in **recovery mode**
after a shutdown and reset, to read the logs (`/syslog` first). This spec is for a worker session to build the shell
**against two dummy integration points** (console I/O and file I/O), tested on the host; the real UART driver and the real
file-system client come later and replace the dummies without touching the shell.

## 1. Context (not part of this work)

- The file system runs on C&C 2; units use it through the `fs.h` calls below (requests to C&C 2). In recovery mode a
  small, slow file system (software tree walks, direct LPDDR reads, no cache) serves the same calls. The shell can't tell
  them apart.
- **Shutdown:** C&C 1 keeps `/syslog` open from boot. On a hardware error it sends C&C 2 a shutdown: C&C 2 fails every
  pending request and closes every file but syslog; C&C 1 writes the error to syslog and closes it; that close makes C&C 2
  flush syslog's tail and every dirty line of the shared cache to LPDDR, ack, and halt. After the reset, recovery mode and
  this shell.

## 2. Deliverables

A new directory **`fw/shell/`**:

| file | contents |
|---|---|
| `console.h`, `fs.h` | the two integration points (sections 4 and 5), exactly as specified |
| `shell.c` | `shell_main`, the line editor, the parser, redirection, paths |
| `cmds.c` | the commands |
| `fmt.c`, `str.c` | the printf subset, string helpers (no libc) |
| `grep.c` | the matcher |
| `host/console_host.c` | the dummy console: raw terminal, or a script on stdin |
| `host/fs_mem.c`, `host/fs_mem.h` | the dummy file system, in memory, plus seeding helpers for tests |
| `host/main.c` | host entry: seeds the dummy FS (optional seed file), runs `shell_main` |
| `rv32/crt0.S`, `rv32/link.ld`, `rv32/stubs.c` | a link-only RV32 build: stub console and fs, for the size check |
| `Makefile` | `make host`, `make test`, `make rv32`, `make size` |
| `tests/` | scripted sessions (section 12) |
| `NOTES.md` | what was done, sizes per object, anything left open |

Entry point: **`void shell_main(int mode)`**, `mode` 0 normal, 1 recovery (shown in the prompt). On the target it never
returns; on the host it returns when the console reports end of input.

## 3. Build constraints

- C99, **freestanding**: `-ffreestanding -nostdlib`, no libc, no malloc, no floats; all state in static buffers.
- Target **RV32IMC**, ilp32: `-march=rv32imc -mabi=ilp32 -Os -ffunction-sections -fdata-sections -Wl,--gc-sections`.
- **Code budget: `.text` + `.rodata` ≤ 32 KB**, checked by `make size` (fails over budget). Expected well under (~15 KB).
- **No 64-bit division from libgcc**: lengths and offsets are 64-bit, so decimal output uses its own divide-by-10;
  `make size` also fails if `__udivdi3`, `__umoddi3`, `__divdi3` or `__moddi3` appear in the link.
- **Data**: static buffers about 14 KB (line 1 KB, cwd 1 KB, two path scratches 2 KB, one page buffer 4 KB, the
  redirect buffer 4 KB, grep's line 1 KB); stack ≤ 2 KB. No unbounded recursion (the matcher's is bounded by the
  pattern's length, ≤ 128).
- The host build uses the same sources plus `host/`, compiled with the host compiler (warnings on: `-Wall -Wextra`).

## 4. `console.h`

```c
int  con_getc(void);    // blocks for the next input byte (0..255); -1 at end of input (host scripts only)
int  con_poll(void);    // the next input byte if one is waiting, else -1; never blocks
void con_putc(int c);   // one output byte
```

The shell writes `\r\n` for every newline it sends to the console. Host dummy: if stdin is a terminal, raw mode (no echo,
no line buffering) restored at exit; otherwise bytes come from stdin as they are (scripts), and `con_poll` returns the next
byte without waiting. The shell always echoes, so a script's output is deterministic.

## 5. `fs.h`

The API agreed on 2026-11-10, plus four additions the shell needs (marked **+**; to be confirmed by the owner).

```c
typedef unsigned int u32;  typedef unsigned long long u64;

// flags
#define FS_READ    1   // sequential fs_read; fs_read_page
#define FS_APPEND  2   // fs_write appends; creates the file if missing
#define FS_WRITE   4   // fs_write_page (random page writes)
#define FS_TRUNC   8   // + with FS_APPEND: empty the file first (the shell's >)

int  fs_open(const char *path, int flags);          // an fd >= 0, or an error
long fs_read(int fd, void *buf, long n);            // bytes read; 0 at the end
long fs_write(int fd, const void *buf, long n);     // appends; bytes written
int  fs_close(int fd);
int  fs_mkdir(const char *path);
int  fs_remove(const char *path);                   // a file, or an empty directory
int  fs_read_page(int fd, u64 page, void *buf);     // 4 KB at page x 4096; bytes past the length are undefined
int  fs_write_page(int fd, u64 page, const void *buf);
int  fs_fprintf(int fd, const char *fmt, ...);      // the shell doesn't need it; the dummy may leave it unimplemented

#define FS_T_DIR   1
#define FS_T_FILE  2                                 // type values are open (item 119); others print as numbers
#define FS_F_FROZEN 1
#define FS_F_SPARSE 2

struct fs_dirent { char name[96]; u32 type; u32 flags; u64 length; };
struct fs_stat   { u32 type; u32 flags; u64 length; u64 pages; u32 copies; u32 depth; };

int  fs_list(const char *path, u32 *pos, struct fs_dirent *e);  // + (refines fs_list's index): *pos starts at 0 and is
                                                                 // advanced past the entry returned; 1 an entry, 0 the end
int  fs_stat(const char *path, struct fs_stat *st);             // + for stat, tail, cd, rm/rmdir checks
int  fs_df(u64 *total_pages, u64 *free_pages);                  // +
```

**Errors** are negative: `FS_ENOENT -1`, `FS_EEXIST -2`, `FS_ENOTDIR -3`, `FS_EISDIR -4`, `FS_ENOTEMPTY -5`,
`FS_EROFS -6` (frozen), `FS_EBUSY -7` (has copies, or open), `FS_ENOSPC -8`, `FS_EBADF -9`, `FS_EINVAL -10`, `FS_EIO -11`,
`FS_ENAMETOOLONG -12`, `FS_ESHUTDOWN -13`. The shell maps each to a message ("no such file or directory", "file exists",
"not a directory", "is a directory", "directory not empty", "read-only file", "busy", "no space", "bad file",
"invalid argument", "I/O error", "name too long", "file system shut down"); an unknown code prints as `error N`.

**Limits:** a name ≤ 95 bytes (any bytes but `/` and NUL); a path ≤ 1,023 bytes; ≤ 10 levels. Paths given to `fs.h` are
always absolute and normalized (section 8).

**The dummy (`host/fs_mem.c`)** keeps a tree of directories and files in memory, files stored as 4 KB pages so
`fs_read_page` behaves as the real one; it enforces the limits and returns the errors above (including `FS_EROFS` on a
frozen file and `FS_ENOSPC` past a page capacity set by the test). Directory order is creation order. Seeding helpers for
tests: `fsmem_init(u64 capacity_pages)`, `fsmem_put(path, data, len, flags)`, `fsmem_mkdir(path)`; `host/main.c` reads an
optional seed file (section 12).

## 6. The line editor

- Prompt: `<cwd>> ` in normal mode, `[recovery] <cwd>> ` in recovery mode, e.g. `[recovery] /logs> `.
- The line holds up to 1,023 bytes; a byte past that rings the bell (0x07) and is dropped.
- Printable bytes 0x20-0x7E are inserted at the end and echoed. Other bytes are ignored, except:

| key | bytes | action |
|---|---|---|
| Enter | CR or LF (LF right after CR ignored) | echo `\r\n`, run the line |
| Backspace | 0x08 or 0x7F | erase the last byte (`\b \b`); bell on an empty line |
| Ctrl-U | 0x15 | erase the whole line |
| Ctrl-W | 0x17 | erase the last word (trailing spaces, then non-spaces) |
| Ctrl-C | 0x03 | echo `^C\r\n`, drop the line, new prompt |
| escape sequences | `ESC [` ... final byte 0x40-0x7E, or `ESC O x` | consumed and ignored (arrow keys insert nothing) |

- **Stretch** (only once everything else passes, and inside the budget): left/right/Home/End with insertion mid-line and
  forward delete (`ESC [ 3 ~`); up/down history of 8 lines.
- **Interrupts while a command runs:** long loops (`cat`, `more`, `grep`, `hexdump`, `wc`, `ls`) call `interrupted()`,
  which reads `con_poll` and returns true on 0x03 (other bytes waiting are dropped). An interrupted command prints `^C` and
  stops; a `>` file keeps what was written.

## 7. Parsing and redirection

- Tokens are separated by spaces and tabs. Double quotes group (`"my file"`); inside them `\"` and `\\` are the only
  escapes. An unterminated quote is an error: `syntax error: unterminated quote`.
- At most 16 tokens (`syntax error: too many arguments`).
- **Redirection:** one `>` or `>>`, anywhere in the line, followed by a file (`> f`, `>f`, `>> f`, `>>f`). `>` opens
  `FS_APPEND|FS_TRUNC`, `>>` opens `FS_APPEND`. A second redirection, or `>` with no file, is a syntax error. If the open
  fails, the command doesn't run: `sh: <path>: <message>`.
- Redirection takes the command's normal output only; error messages and `more`'s prompt always go to the console.
  Redirected output is plain `\n`, buffered 4 KB, flushed when full and at the command's end, then the file is closed. A
  write error is reported once (`sh: <path>: <message>`) and further output is dropped.
- Nothing else: no pipes, globbing, variables, `;`, or scripts. An empty line does nothing.

## 8. Paths and the current directory

The shell keeps the cwd (initially `/`). A path is resolved by prefixing the cwd if it is relative, then normalizing:
repeated `/` collapsed, `.` removed, `..` removes the previous component (at `/`, stays at `/`), a trailing `/` removed
(except `/` itself). A result over 1,023 bytes, or a component over 95, gives `name too long` without calling the FS.

## 9. Commands

Errors print as `<cmd>: <path>: <message>` (or `<cmd>: <message>`) and the command goes on to its next argument where it
has several. Bad options or missing arguments print `usage: <usage line>`. An unknown command prints
`<name>: command not found`. Options must come before operands; `-n N` also accepts `-nN`.

| command | usage | behavior and output |
|---|---|---|
| `help` | `help` | every command's usage line, in this table's order |
| `ls` | `ls [-l] [path...]` | no path: the cwd. A directory: its entries in directory order, one per line, directories with a trailing `/`. A file: its own name. With several paths, each directory's listing is preceded by `<path>:` and separated by a blank line. **`-l`**: `<t><f><s> <length> <name>`, with `t` `d` (directory), `-` (file) or `?` (other), `f` `f` if frozen else `-`, `s` `s` if sparse else `-`, the length in bytes right-aligned in 12 columns |
| `cd` | `cd [dir]` | no argument: `/`. Checks with `fs_stat` that it is a directory (`not a directory` otherwise) |
| `pwd` | `pwd` | the cwd |
| `cat` | `cat file...` | the files' bytes, concatenated, as they are |
| `more` | `more file` | pages of 23 lines (long lines wrap at 80 columns and count as several), then `--More-- (NN%)` (percent of bytes shown). Space: next page; Enter: next line; `q` or Ctrl-C: stop. The prompt is erased (`\r`, spaces, `\r`) before going on. Redirected: same as `cat` |
| `head` | `head [-n N] file...` | the first N lines (default 10) |
| `tail` | `tail [-n N] file...` | the last N lines (default 10), found by reading **backward from the last page** with `fs_read_page` and the length from `fs_stat`, never from the start. A last line without a newline counts as a line. With several files, each is preceded by `==> <path> <==` and separated by a blank line (also `head`) |
| `grep` | `grep [-n] [-i] [-c] [-v] pattern file...` | lines matching the pattern (section 10). `-n`: prefix `N:` (line number, from 1). `-i`: ignore ASCII case. `-c`: only the count. `-v`: non-matching lines. Several files: each output line (or count) prefixed `<path>:` |
| `wc` | `wc file...` | `<lines> <words> <bytes> <path>`, each number right-aligned in 8 columns; several files add a `total` line |
| `hexdump` | `hexdump [-s off] [-n len] file` | 16 bytes per line: the offset as 8 hex digits (more if needed), two spaces, the bytes as `xx` separated by spaces with an extra space after the eighth, padded on the last line, two spaces, `\|<ascii>\|` (0x20-0x7E as is, others `.`); then a last line with the end offset. `-s` and `-n` take decimal or `0x` hex. Starts with `fs_read_page` at `-s`'s page |
| `stat` | `stat path...` | `name: <path>`, `type: dir\|file\|<n>`, `length: <n>`, `pages: <n>`, `depth: <n>`, `flags: <frozen sparse, or none>`, `copies: <n>`, one per line; a blank line between paths |
| `df` | `df` | `pages: <total> total, <used> used, <free> free` then `bytes: <total> total, <used> used, <free> free` (pages x 4096, in MB with one decimal, e.g. `4096.0 MB`, computed without floats) |
| `echo` | `echo [arg...]` | the arguments joined by single spaces, then a newline (with `>>`, a note into `/syslog`) |
| `mkdir` | `mkdir dir...` | `fs_mkdir` each |
| `rmdir` | `rmdir dir...` | each must be a directory (`not a directory` otherwise); `fs_remove` (so `directory not empty` from the FS) |
| `rm` | `rm file...` | each must not be a directory (`is a directory` otherwise); `fs_remove` |

Files are read with a single fd per file through `fs_read` into the 4 KB page buffer (except `tail` and `hexdump -s`).
Reading or listing a directory where a file is expected gives `is a directory`, and the reverse `not a directory`.

## 10. The matcher

Rob Pike's matcher (*The Practice of Programming*, ch. 9): `c` a literal byte, `.` any byte, `^` at the start anchors,
`$` at the end anchors, `*` zero or more of the previous element; `\` makes the next byte literal. Patterns are at most
128 bytes (`grep: pattern too long`). Lines are matched up to 1,024 bytes; a longer line is matched (and printed, and
counted) as chunks of 1,024 bytes, noted in `help`. `-i` folds ASCII letters in both pattern and line.

## 11. Formatted output

One internal formatter writing to the current sink (the console with `\n` → `\r\n`, or the redirect buffer): `%s`, `%c`,
`%d`, `%u`, `%x`, `%llu`, `%llx`, `%%`; a width; `-` to left-align; `0` to zero-pad. 64-bit decimal by repeated
divide-by-10 written with shifts and subtraction (no libgcc).

## 12. Tests

- Each test is `tests/NN-name.in` (the keystrokes), an optional `tests/NN-name.seed` (the dummy FS's starting contents),
  and `tests/NN-name.out` (the exact console bytes expected, `\r\n` included). `make test` runs every test through the host
  build and diffs; a failure prints the diff.
- `.in` is text with escapes for control bytes: `\r`, `\n`, `\x03`, `\x7f`, `\e` (ESC), `\\`; a literal newline in the file
  is ignored (so tests stay readable), so every Enter is written `\r`.
- `.seed` lines: `cap <pages>`, `dir <path>`, `file <path> [frozen] [sparse]` followed by the contents up to a line `.`,
  `gen <path> <bytes> <pattern>` for big files (numbered lines `line N\n`, or `bytes` for 0-255 repeating), `frozen` on a
  file sets the flag.
- **Required coverage:** every command's normal output and each of its error paths; every editor key, including Backspace
  on an empty line, a line at 1,023 bytes, arrow keys ignored; quotes (names with spaces) and both syntax errors; `>` and
  `>>`, a redirect whose open fails, a redirect to a frozen file, running out of space mid-redirect; path resolution (`.`,
  `..` past `/`, `//`, trailing `/`, a 95- and a 96-byte name, an over-long path); `tail` on files of 0, 1, 4,095, 4,096,
  4,097 and 8,192 bytes, with and without a final newline, and a last line longer than a page; `head`/`tail` with several
  files; `more` paging with Space, Enter and `q`; `grep` with each metacharacter, anchors, `\`, `-i -n -c -v`, an over-long
  line; `hexdump` with `-s`/`-n` across a page boundary; Ctrl-C interrupting `cat` of a 1 MB file; `df`; recovery mode's
  prompt.
- `make rv32` links with the stubs and `make size` prints `.text`, `.rodata`, `.data`, `.bss` and per-object sizes, and
  fails over the code budget or on a libgcc 64-bit division symbol. Record the sizes in `NOTES.md`.

## 13. Out of scope

The real UART driver; the real `fs.h` client (requests to C&C 2) and the recovery file system; the allocator; the shutdown
sequence; pipes, globbing, scripts. History and cursor movement are a stretch (section 6).

## 14. Open (owner)

1. The `fs.h` additions: `FS_TRUNC`, `fs_stat`, `fs_df`, and `fs_list`'s cursor in place of an index.
2. The type values (item 119); the dummy uses `FS_T_DIR 1`, `FS_T_FILE 2`.
3. Recovery mode's free pages for `>` and `rm`: the bitmap saved at shutdown, rebuilt at mount, or a reserved pool.
