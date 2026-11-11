package gold;

import static gold.Op.*;

/** Opcode classes and the memory-address rule, as jolt-cpp's isa/riscv.hpp at 821e1bd. */
public final class Isa {
  private Isa() {}

  /** Registers: 0-31 RISC-V; 32-39 reserved state; 40-47 allocatable temporaries; 48-127 inline scratch. */
  public static final int NUM_RISC_REGS = 32;
  public static final int NUM_REGS = 128;
  public static final int RESERVATION_W = 32, RESERVATION_D = 33;            // isa/virtual.hpp:34-35
  public static final int MTVEC = 34, MSCRATCH = 35, MEPC = 36, MCAUSE = 37,  // isa/virtual.hpp:36-41
      MTVAL = 38, MSTATUS = 39;
  public static final int FIRST_TEMP = 40;            // allocator start (virtual.cpp:142)
  public static final int NUM_STATEFUL_REGS = 48;     // 0-47 (virtual.hpp:43-53)

  /** CSR address to its register, 0 if unsupported (isa/virtual.hpp:56-66). */
  public static int csrToReg(int csr) {
    switch (csr) {
      case 0x300: return MSTATUS;
      case 0x305: return MTVEC;
      case 0x340: return MSCRATCH;
      case 0x341: return MEPC;
      case 0x342: return MCAUSE;
      case 0x343: return MTVAL;
      default: return 0;
    }
  }

  public static boolean isStore(int op) {        // riscv.hpp:172-186
    return op == SB || op == SH || op == SW || op == SD
        || op == VirtualStoreB || op == VirtualStoreH || op == VirtualStoreW;
  }

  public static boolean isLoad(int op) {         // riscv.hpp:188-205
    return op == LB || op == LH || op == LW || op == LD || op == LBU || op == LHU || op == LWU
        || op == VirtualLoadB || op == VirtualLoadH || op == VirtualLoadW;
  }

  /** The division family's advice opcodes and VirtualAdvice (riscv.hpp:284-291). */
  public static boolean isVirtualAdvice(int op) {
    return op == VirtualAdvice || (op >= VirtualAdviceDivU && op <= VirtualAdviceRemW);
  }

  /**
   * The low address bits a narrow access drops: the word address is the effective address minus them
   * (riscv.hpp:215-255). Subtracting keeps a misaligned access misaligned, so it is caught.
   */
  static long floorMask(int op) {
    switch (op) {
      case VirtualLoadB: case VirtualStoreB: return 7;
      case VirtualLoadH: case VirtualStoreH: return 6;
      case VirtualLoadW: case VirtualStoreW: return 4;
      default: return 0;
    }
  }

  /** The 8-byte word a memory row accesses; a misaligned word address aborts (riscv.hpp:257-270). */
  public static long wordAddress(int op, long base, long offset) {
    long ea = base + offset;
    long addr = ea - (ea & floorMask(op));
    if ((addr & 7) != 0) {
      throw new EmulationError(String.format("misaligned memory access: ea=%#x word_address=%#x op=%s", ea, addr,
          Op.name(op)));
    }
    return addr;
  }
}
