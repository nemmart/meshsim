package gold;

/**
 * A trace row as WORKER18's fixed record: ten little-endian u64 words holding jolt-cpp's RiscCycle
 * (trace_owner.hpp:19-68). Agreed with the owner 2026-11-02; jolt-asic-planning branch worker18-rows, row_dump.cpp,
 * writes the same record from jolt-cpp.
 *
 * <pre>
 *   w0 pc
 *   w1 opcode | rd_reg << 8 | rs1_reg << 16 | rs2_reg << 24 | (u16) inline_sequence_remaining << 32
 *      | is_compressed << 48 | virt_flags << 56          (virt_flags: bit 0 is_virtual, bit 1 first in sequence)
 *   w2 imm   w3 rd   w4 rs1   w5 rs2   w6 rd_prev   w7 mem_access_addr   w8 mem_pre_value   w9 mem_post_value
 * </pre>
 *
 * Hash: WORKER16's mix (state_probe.cpp:42) folded over the ten words from 0x243f6a8885a308d3; a window's hash folds
 * the rows' hashes from 0; a run's hash folds the windows' hashes from 0.
 */
public final class Row {
  private Row() {}

  public static final int WORDS = 10;
  public static final int BYTES = 8 * WORDS;
  public static final String[] NAMES = {"pc", "op/regs/flags", "imm", "rd", "rs1", "rs2", "rd_prev", "mem_addr",
      "mem_pre", "mem_post"};

  public static long mix(long h, long w) {
    h ^= w + 0x9e3779b97f4a7c15L + (h << 6) + (h >>> 2);
    h *= 0xff51afd7ed558ccdL;
    return h ^ (h >>> 33);
  }

  public static long hash(long[] w) {
    long h = 0x243f6a8885a308d3L;
    for (int i = 0; i < WORDS; i++) h = mix(h, w[i]);
    return h;
  }

  public static String describe(long[] w) {
    long f = w[1];
    return String.format("pc=%#x %s rd=x%d rs1=x%d rs2=x%d rem=%d%s%s%s imm=%#x rd=%#x rs1=%#x rs2=%#x rd_prev=%#x"
            + " mem=%#x pre=%#x post=%#x",
        w[0], Op.name((int) (f & 0xff)), (f >>> 8) & 0xff, (f >>> 16) & 0xff, (f >>> 24) & 0xff, (f >>> 32) & 0xffff,
        ((f >>> 48) & 1) != 0 ? " C" : "", ((f >>> 56) & 1) != 0 ? " virt" : "", ((f >>> 57) & 1) != 0 ? " first" : "",
        w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9]);
  }
}
