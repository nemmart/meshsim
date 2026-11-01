/* Test-side support for the v1.2 recipes (meshsim, WORKER4): what a recipe leaves to its program.
 * template_lookup (rcp_replay.c, recipe 26): the program's own template index (4.19.9). Here a small table the test
 * fills in data memory: tpl_n entries of {instruction word, first chunk in big SRAM, length in 128-bit words}; the
 * expansion from `offset` is at chunk + 4 x offset, length - offset words. An unknown word gives 0 entries. */
#include <stdint.h>
volatile uint32_t tpl_n;
volatile uint32_t tpl_tab[3 * 16];
void template_lookup(uint32_t instr, uint32_t offset, uint32_t *chunk, uint32_t *entries) {
    for (uint32_t i = 0; i < tpl_n && i < 16; i++)
        if (tpl_tab[3 * i] == instr) {
            uint32_t len = tpl_tab[3 * i + 2];
            *chunk = tpl_tab[3 * i + 1] + 4 * offset;
            *entries = offset < len ? len - offset : 0;
            return;
        }
    *chunk = 0; *entries = 0;
}
