/* console.h: the shell's console (SHELL-SPEC section 4). The UART on the chip; host/console_host.c on the host. */
#ifndef CONSOLE_H
#define CONSOLE_H

int  con_getc(void);    /* blocks for the next input byte (0..255); -1 at end of input (host scripts only) */
int  con_poll(void);    /* the next input byte if one is waiting, else -1; never blocks */
void con_putc(int c);   /* one output byte */

#endif /* CONSOLE_H */
