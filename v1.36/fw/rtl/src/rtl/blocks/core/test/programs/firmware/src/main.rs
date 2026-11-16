//! Bare-metal Rust test program for the RV32 core.
//!
//! Built for `riscv32i-unknown-none-elf`, a legal subset of the core's ISA, and
//! started by `start.S`. The boot load has written `.rodata` and `.data` into
//! DMEM and zeroed `.bss` (`link.ld`).
//! `main` returns 0 when every check passes, or the number of the first failed
//! check; `start.S` reports that to the test memory's result device.
//!
//! Inputs pass through `black_box` so the compiler cannot fold the checks away and
//! the core must really run the arithmetic, including the software multiply and
//! divide helpers (the target has no M extension).

#![no_std]
#![no_main]

use core::arch::global_asm;
use core::hint::black_box;
use core::panic::PanicInfo;
use core::ptr::{addr_of, addr_of_mut, read_volatile, write_volatile};

global_asm!(include_str!("../start.S"));

/// Result device failure address (see test/src/rv32_core_test_memory.rs).
const FAIL_ADDRESS: *mut u32 = 0x2000_0004 as *mut u32;

/// Initialized data: must be written into DMEM by the boot load (.data).
static mut INITIALIZED: [u32; 4] = [0x1234_5678, 0x9abc_def0, 7, 0xffff_ffff];

/// Zero-initialized data: must be zeroed by the boot load (.bss).
static mut ZEROED: [u32; 16] = [0; 16];

/// Read-only data, written into DMEM by the boot load (.rodata).
static SQUARES: [u16; 8] = [0, 1, 4, 9, 16, 25, 36, 49];

#[panic_handler]
fn panic(_: &PanicInfo) -> ! {
    // SAFETY: the failure address is a device register in the test memory map.
    unsafe { write_volatile(FAIL_ADDRESS, 0xdead) };
    loop {}
}

/// Recursive, so each level needs its own stack frame.
fn fibonacci(n: u32) -> u32 {
    if n < 2 { n } else { fibonacci(n - 1) + fibonacci(n - 2) }
}

#[unsafe(no_mangle)]
pub extern "C" fn main() -> u32 {
    // ---- 1-2: the boot load's initialized .data and zeroed .bss ----
    // SAFETY: single-threaded bare-metal program; nothing else touches these.
    let initialized = unsafe { read_volatile(addr_of!(INITIALIZED)) };
    if initialized != [0x1234_5678, 0x9abc_def0, 7, 0xffff_ffff] {
        return 1;
    }
    let zeroed = unsafe { read_volatile(addr_of!(ZEROED)) };
    if zeroed.iter().any(|&word| word != 0) {
        return 2;
    }

    // ---- 3: globals can be written and read back ----
    unsafe {
        write_volatile(addr_of_mut!(ZEROED[5]), 0xa5a5_5a5a);
        write_volatile(addr_of_mut!(INITIALIZED[2]), 8);
        if read_volatile(addr_of!(ZEROED[5])) != 0xa5a5_5a5a || read_volatile(addr_of!(INITIALIZED[2])) != 8 {
            return 3;
        }
    }

    // ---- 4: calls and the stack ----
    if fibonacci(black_box(15)) != 610 {
        return 4;
    }

    // ---- 5-9: software divide and remainder (no divide instruction) ----
    let (a, b) = (black_box(-100i32), black_box(7i32));
    if a / b != -14 {
        return 5;
    }
    if a % b != -2 {
        return 6;
    }
    let (c, d) = (black_box(0xffff_fff0u32), black_box(0x10u32));
    if c / d != 0x0fff_ffff || c % d != 0 {
        return 7;
    }
    if black_box(i32::MIN).wrapping_div(black_box(-1)) != i32::MIN {
        return 8;
    }
    if black_box(1_000_000_007u32) % black_box(97u32) != 1_000_000_007 % 97 {
        return 9;
    }

    // ---- 10-11: software multiply, 32 and 64 bits ----
    if black_box(12_345u32).wrapping_mul(black_box(6_789u32)) != 83_810_205 {
        return 10;
    }
    let product = (black_box(0xdead_beefu64)) * (black_box(0x1234_5678u64));
    if product != 0x0fd5_bdee_5621_ca08 {
        return 11;
    }

    // ---- 12: read-only data from DMEM ----
    let sum: u32 = SQUARES.iter().map(|&square| u32::from(black_box(square))).sum();
    if sum != 140 {
        return 12;
    }

    // ---- 13: 64-bit shifts and carries ----
    let wide = black_box(0x0000_0001_ffff_ffffu64);
    if wide + 1 != 0x0000_0002_0000_0000 || (wide << 31) >> 62 != 3 {
        return 13;
    }
    0
}
