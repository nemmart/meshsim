/* console_host.h: the host console's own setup, for host/main.cpp. */
#ifndef CONSOLE_HOST_H
#define CONSOLE_HOST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void con_host_script(const unsigned char *b, size_t n);   /* the keystrokes, given whole: a script */
void con_host_init(void);                                 /* a terminal (raw mode), or stdin read whole as a script */
void shell_main(void);

#ifdef __cplusplus
}
#endif

#endif
