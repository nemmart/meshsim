/* rv_entry.c -- the RV32 build's only additions: the tile state the harness
 * copies in and out, and freestanding memset/memcpy in case the compiler
 * emits them (the build checks that it doesn't). */
#include "../planner/skew.h"
Tile g_tile;
void _start(void) { }
