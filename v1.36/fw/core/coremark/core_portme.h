/*
 * CoreMark port for the RV32 core running on its Verilator model (program_test).
 *
 * Adapted from CoreMark's barebones/core_portme.h, Copyright 2018 Embedded
 * Microprocessor Benchmark Consortium (EEMBC), licensed under the Apache License,
 * Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0).
 *
 * Built by `cargo xtask`, which passes ITERATIONS and FLAGS_STR. The benchmark
 * code itself comes unmodified from the pinned CoreMark revision
 * (coremark.toml).
 */
#ifndef CORE_PORTME_H
#define CORE_PORTME_H

/* No floating point, no C library: output goes through the test console. */
#define HAS_FLOAT 0
#define HAS_TIME_H 0
#define USE_CLOCK 0
#define HAS_STDIO 0
#define HAS_PRINTF 0

#define COMPILER_VERSION "GCC" __VERSION__
#define COMPILER_FLAGS FLAGS_STR
#define MEM_LOCATION "STATIC (DMEM)"

typedef signed short ee_s16;
typedef unsigned short ee_u16;
typedef signed int ee_s32;
typedef float ee_f32;
typedef unsigned char ee_u8;
typedef unsigned int ee_u32;
typedef ee_u32 ee_ptr_int;
typedef unsigned int ee_size_t;
#define NULL ((void *)0)

#define align_mem(x) (void *)(4 + (((ee_ptr_int)(x) - 1) & ~3))

/* Ticks are core clock cycles (mcycle). */
#define CORETIMETYPE ee_u32
typedef ee_u32 CORE_TICKS;

#define SEED_METHOD SEED_VOLATILE
#define MEM_METHOD MEM_STATIC
#define MULTITHREAD 1
#define MAIN_HAS_NOARGC 1
#define MAIN_HAS_NORETURN 0

extern ee_u32 default_num_contexts;

typedef struct CORE_PORTABLE_S
{
    ee_u8 portable_id;
} core_portable;

void portable_init(core_portable *p, int *argc, char *argv[]);
void portable_fini(core_portable *p);

#define PERFORMANCE_RUN 1

int ee_printf(const char *fmt, ...);

#endif /* CORE_PORTME_H */
