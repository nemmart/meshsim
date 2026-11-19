/*
 * CoreMark port for the RV32 core running on its Verilator model (program_test).
 *
 * Adapted from CoreMark's barebones/core_portme.c, Copyright 2018 Embedded
 * Microprocessor Benchmark Consortium (EEMBC), licensed under the Apache License,
 * Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0).
 *
 * Time is the mcycle counter; the retired-instruction count (minstret) over the
 * same interval is reported after CoreMark's own output. Text goes to the test
 * memory's console device, one byte per store, through a small ee_printf that
 * handles the conversions CoreMark's report uses (%d %u %lu %x %04x %s %c).
 *
 * Pass or fail: CoreMark checks its own results and prints any problem as a line
 * containing "ERROR!", but main always returns 0. So the port watches the output
 * and, when CoreMark finishes (portable_fini), reports failure itself unless
 * CoreMark recognised the standard performance-run parameters and printed no
 * error other than the one for runs shorter than 10 seconds. A simulated run is
 * far shorter than 10 seconds at the 1 GHz target, so every run here is
 * unofficial under CoreMark's rules; correctness is still checked in full.
 */
#include "coremark.h"
#include <stdarg.h>

/* Test memory devices (test/src/rv32_core_test_memory.rs). */
#define RESULT_FAIL_ADDRESS ((volatile ee_u32 *)0x20000004)
#define CONSOLE_ADDRESS ((volatile ee_u8 *)0x20000018)

/* Clock of the 1 GHz design target: mcycle ticks per second. */
#define EE_TICKS_PER_SEC 1000000000u

/* The standard performance-run seeds (0, 0, 0x66). */
volatile ee_s32 seed1_volatile = 0x0;
volatile ee_s32 seed2_volatile = 0x0;
volatile ee_s32 seed3_volatile = 0x66;
volatile ee_s32 seed4_volatile = ITERATIONS;
volatile ee_s32 seed5_volatile = 0;

ee_u32 default_num_contexts = 1;

/* ---------------------------------------------------------------------------
 * Timing
 * ------------------------------------------------------------------------- */

static ee_u32 start_cycles, stop_cycles, start_instructions, stop_instructions;

static ee_u32 read_mcycle(void)
{
    ee_u32 value;
    __asm__ volatile("csrr %0, mcycle" : "=r"(value));
    return value;
}

static ee_u32 read_minstret(void)
{
    ee_u32 value;
    __asm__ volatile("csrr %0, minstret" : "=r"(value));
    return value;
}

void start_time(void)
{
    start_instructions = read_minstret();
    start_cycles = read_mcycle();
}

void stop_time(void)
{
    stop_cycles = read_mcycle();
    stop_instructions = read_minstret();
}

CORE_TICKS get_time(void)
{
    return stop_cycles - start_cycles;
}

secs_ret time_in_secs(CORE_TICKS ticks)
{
    return ticks / EE_TICKS_PER_SEC;
}

/* ---------------------------------------------------------------------------
 * Output, and the check on it
 * ------------------------------------------------------------------------- */

static const char PERFORMANCE_RUN_LINE[] = "2K performance run parameters for coremark.";
static const char SHORT_RUN_ERROR[] = "ERROR! Must execute for at least 10 secs for a valid result!";

static char line[128];
static ee_u32 line_length;
static int saw_performance_run;
static int saw_other_error;

static int starts_with(const char *text, const char *prefix)
{
    while (*prefix != '\0') {
        if (*text++ != *prefix++) {
            return 0;
        }
    }
    return 1;
}

static int contains(const char *text, const char *part)
{
    for (; *text != '\0'; text++) {
        if (starts_with(text, part)) {
            return 1;
        }
    }
    return 0;
}

static void check_line(void)
{
    line[line_length] = '\0';
    if (starts_with(line, PERFORMANCE_RUN_LINE)) {
        saw_performance_run = 1;
    }
    if (contains(line, "ERROR!") && !starts_with(line, SHORT_RUN_ERROR)) {
        saw_other_error = 1;
    }
    line_length = 0;
}

static void put_char(char c)
{
    *CONSOLE_ADDRESS = (ee_u8)c;
    if (c == '\n') {
        check_line();
    } else if (line_length < sizeof(line) - 1) {
        line[line_length++] = c;
    }
}

static void put_number(ee_u32 value, ee_u32 base, int negative, int width, char pad)
{
    char digits[12];
    int count = 0;
    do {
        ee_u32 digit = value % base;
        digits[count++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
        value /= base;
    } while (value != 0);
    if (negative) {
        digits[count++] = '-';
    }
    for (; width > count; width--) {
        put_char(pad);
    }
    while (count > 0) {
        put_char(digits[--count]);
    }
}

int ee_printf(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    for (const char *p = format; *p != '\0'; p++) {
        if (*p != '%') {
            put_char(*p);
            continue;
        }
        p++;
        char pad = ' ';
        int width = 0;
        if (*p == '0') {
            pad = '0';
            p++;
        }
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p++ - '0');
        }
        if (*p == 'l') {
            p++; /* long is 32 bits here */
        }
        switch (*p) {
            case 'd': {
                ee_s32 value = va_arg(arguments, ee_s32);
                put_number(value < 0 ? -(ee_u32)value : (ee_u32)value, 10, value < 0, width, pad);
                break;
            }
            case 'u':
                put_number(va_arg(arguments, ee_u32), 10, 0, width, pad);
                break;
            case 'x':
                put_number(va_arg(arguments, ee_u32), 16, 0, width, pad);
                break;
            case 's':
                for (const char *text = va_arg(arguments, const char *); *text != '\0'; text++) {
                    put_char(*text);
                }
                break;
            case 'c':
                put_char((char)va_arg(arguments, int));
                break;
            default:
                put_char(*p);
                break;
        }
    }
    va_end(arguments);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Freestanding support: GCC may call memset to clear arrays. The attribute stops
 * GCC from turning this loop back into a call to memset itself.
 * ------------------------------------------------------------------------- */

__attribute__((optimize("no-tree-loop-distribute-patterns"))) void *memset(void *destination, int value,
                                                                          ee_size_t count)
{
    ee_u8 *bytes = destination;
    while (count-- > 0) {
        *bytes++ = (ee_u8)value;
    }
    return destination;
}

/* ---------------------------------------------------------------------------
 * Start and finish
 * ------------------------------------------------------------------------- */

void portable_init(core_portable *p, int *argc, char *argv[])
{
    (void)argc;
    (void)argv;
    p->portable_id = 1;
}

void portable_fini(core_portable *p)
{
    ee_printf("Instructions     : %lu\n", (long unsigned)(stop_instructions - start_instructions));
    p->portable_id = 0;
    if (!saw_performance_run || saw_other_error) {
        *RESULT_FAIL_ADDRESS = 1;
        for (;;) {
        }
    }
}
