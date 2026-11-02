package gold;

import java.io.IOException;
import java.math.BigInteger;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Checks every inline's generated rows against jolt-cpp's emit lists (src/tracer/inlines/*.inc at 821e1bd), read as
 * data: each line is one emit_* call, i.e. one row, in order.
 *
 * <pre>
 *   InlineEmitTest &lt;jolt-cpp src/tracer/inlines&gt; [--write-digests]   row-for-row against the .inc files
 *   InlineEmitTest                                                   against test/inline-emit-digests.txt
 * </pre>
 *
 * The digests file holds, per inline, the row count and a hash of the .inc's rows under two operand bindings; it is
 * written from the .inc files with --write-digests, so the check runs on every build without a jolt-cpp checkout.
 * Inlines not written yet are reported as pending; any written inline that differs fails the test.
 */
public final class InlineEmitTest {
  static final Path DIGESTS = Path.of("test/inline-emit-digests.txt");

  /** .inc file to opcode (virtual.cpp:988-1062). */
  static final Map<String, Integer> FILES = new LinkedHashMap<>();
  static {
    FILES.put("bigint_multiply", Op.BIGINT256_MUL_INLINE);
    FILES.put("blake2b", Op.BLAKE2_INLINE);
    FILES.put("blake3", Op.BLAKE3_INLINE);
    FILES.put("blake3_keyed64", Op.BLAKE3_KEYED64_INLINE);
    FILES.put("keccak256", Op.KECCAK256);
    FILES.put("keccak256_absorb_permute", Op.KECCAK256_ABSORB_PERMUTE);
    FILES.put("sha256", Op.SHA256_INLINE);
    FILES.put("sha256_init", Op.SHA256_INIT_INLINE);
    FILES.put("secp256k1_mulq", Op.SECP256K1_MULQ_INLINE);
    FILES.put("secp256k1_squareq", Op.SECP256K1_SQUAREQ_INLINE);
    FILES.put("secp256k1_divq", Op.SECP256K1_DIVQ_INLINE);
    FILES.put("secp256k1_mulr", Op.SECP256K1_MULR_INLINE);
    FILES.put("secp256k1_squarer", Op.SECP256K1_SQUARER_INLINE);
    FILES.put("secp256k1_divr", Op.SECP256K1_DIVR_INLINE);
    FILES.put("secp256k1_glvr_adv", Op.SECP256K1_GLVR_ADV_INLINE);
  }

  /** Source operand bindings (rd, rs1, rs2): two, so a generator can't pass by hard-coding registers. */
  static final int[][] BINDINGS = {{10, 11, 12}, {5, 13, 31}};

  /** A row as compared: opcode, rd, rs1, rs2, immediate flags (rd 1, rs1 2, rs2 4), shift offset. */
  record EmitRow(int op, long rd, long rs1, long rs2, int immFlags, int shiftOffset) {
    static EmitRow of(VInstr v) {
      return new EmitRow(v.opcode, v.rd, v.rs1, v.rs2, (v.rdImm ? 1 : 0) | (v.rs1Imm ? 2 : 0) | (v.rs2Imm ? 4 : 0),
          v.shiftOffset);
    }

    @Override public String toString() {
      return String.format("%s %s %s %s", Op.name(op), (immFlags & 1) != 0 ? "#" + Long.toHexString(rd) : "x" + rd,
          (immFlags & 2) != 0 ? "#" + Long.toHexString(rs1) : "x" + rs1, (immFlags & 4) != 0 ? "#" + Long.toHexString(rs2) : "x" + rs2);
    }
  }

  // ---- the .inc reader ----

  static long reg(String t, int[] b) {
    t = t.trim();
    if (t.startsWith("Reg{")) return Long.parseLong(t.substring(4, t.length() - 1));
    switch (t) {
      case "rd": return b[0];
      case "rs1": return b[1];
      case "rs2": return b[2];
      case "x0": return 0;
      default: throw new IllegalArgumentException("register token " + t);
    }
  }

  static long imm(String t) {
    t = t.trim();
    if (!t.endsWith("ll")) throw new IllegalArgumentException("immediate token " + t);
    t = t.substring(0, t.length() - 2);
    boolean neg = t.startsWith("-");
    if (neg) t = t.substring(1);
    BigInteger v = t.startsWith("0x") ? new BigInteger(t.substring(2), 16) : new BigInteger(t);
    return (neg ? v.negate() : v).longValue();
  }

  /** One emit_* line to its row, as the emitter in virtual.cpp:174-218 builds it. */
  static EmitRow parseLine(String line, int[] b) {
    int open = line.indexOf('('), close = line.lastIndexOf(')');
    String kind = line.substring(0, open);
    String[] a = line.substring(open + 1, close).split(",");
    if (kind.equals("emit_advice")) return new EmitRow(Op.VirtualAdvice, reg(a[0], b), 0, 0, 6, 0);
    int op = Op.byName(a[0].trim().substring("isa::".length()));
    switch (kind) {
      case "emit_r": return new EmitRow(op, reg(a[1], b), reg(a[2], b), reg(a[3], b), 0, 0);
      case "emit_i": case "emit_ld": return new EmitRow(op, reg(a[1], b), reg(a[2], b), imm(a[3]), 4, 0);
      case "emit_s": return new EmitRow(op, imm(a[3]), reg(a[1], b), reg(a[2], b), 1, 0);
      case "emit_b": return new EmitRow(op, imm(a[1]), reg(a[2], b), reg(a[3], b), 1, 0);
      case "emit_u":
        if (op == Op.LUI || op == Op.AUIPC) return new EmitRow(op, reg(a[1], b), imm(a[2]), 0, 6, 0);
        return new EmitRow(op, reg(a[1], b), 0, imm(a[2]), 4, 0);
      default: throw new IllegalArgumentException("unknown emit form " + kind);
    }
  }

  static List<EmitRow> readInc(Path file, int[] b) throws IOException {
    List<EmitRow> rows = new ArrayList<>();
    for (String line : Files.readAllLines(file)) {
      line = line.trim();
      if (!line.isEmpty()) rows.add(parseLine(line, b));
    }
    return rows;
  }

  static List<EmitRow> generate(int op, int[] b) {
    List<EmitRow> rows = new ArrayList<>();
    for (VInstr v : Expander.expand(new Instruction(op, b[0], b[1], b[2], false, false, false))) rows.add(EmitRow.of(v));
    return rows;
  }

  static boolean written(List<EmitRow> rows) { return rows.isEmpty() || rows.get(0).op() != Inlines.NOT_WRITTEN; }

  static long digest(List<EmitRow> rows) {
    long h = 0;
    for (EmitRow r : rows) {
      h = Row.mix(h, r.op());
      h = Row.mix(h, r.rd());
      h = Row.mix(h, r.rs1());
      h = Row.mix(h, r.rs2());
      h = Row.mix(h, r.immFlags() | (long) r.shiftOffset() << 8);
    }
    return h;
  }

  public static void main(String[] args) throws IOException {
    int failed = 0, passed = 0, pending = 0;
    if (args.length >= 1) {
      Path dir = Path.of(args[0]);
      StringBuilder digests = new StringBuilder("# inline rows digest-binding-1 digest-binding-2 (InlineEmitTest, from jolt-cpp 821e1bd's .inc)\n");
      for (Map.Entry<String, Integer> e : FILES.entrySet()) {
        long[] d = new long[BINDINGS.length];
        int expectedRows = 0;
        String result = null;
        for (int bi = 0; bi < BINDINGS.length; bi++) {
          List<EmitRow> want = readInc(dir.resolve(e.getKey() + ".inc"), BINDINGS[bi]);
          expectedRows = want.size();
          d[bi] = digest(want);
          List<EmitRow> got = generate(e.getValue(), BINDINGS[bi]);
          if (!written(got)) { result = "pending (not written)"; continue; }
          if (result != null) continue;
          if (got.size() != want.size()) result = "FAIL: " + got.size() + " rows, .inc has " + want.size();
          for (int i = 0; result == null && i < want.size(); i++) {
            if (!got.get(i).equals(want.get(i))) result = "FAIL at row " + i + ": " + got.get(i) + "  .inc: " + want.get(i);
          }
        }
        if (result == null) { result = "ok"; passed++; }
        else if (result.startsWith("pending")) pending++;
        else failed++;
        System.out.printf("  %-26s %5d rows  %s%n", e.getKey(), expectedRows, result);
        digests.append(String.format("%s %d %016x %016x%n", e.getKey(), expectedRows, d[0], d[1]));
      }
      if (args.length >= 2 && args[1].equals("--write-digests")) {
        Files.writeString(DIGESTS, digests.toString());
        System.out.println("wrote " + DIGESTS);
      }
    } else {
      for (String line : Files.readAllLines(DIGESTS)) {
        if (line.startsWith("#") || line.isBlank()) continue;
        String[] f = line.split(" ");
        int op = FILES.get(f[0]);
        String result = "ok";
        for (int bi = 0; bi < BINDINGS.length; bi++) {
          List<EmitRow> got = generate(op, BINDINGS[bi]);
          if (!written(got)) { result = "pending (not written)"; break; }
          if (got.size() != Integer.parseInt(f[1]) || digest(got) != Long.parseUnsignedLong(f[2 + bi], 16)) {
            result = "FAIL (digest; run with the jolt-cpp inlines directory for the first differing row)";
          }
        }
        if (result.equals("ok")) passed++;
        else if (result.startsWith("pending")) pending++;
        else failed++;
        System.out.printf("  %-26s %5s rows  %s%n", f[0], f[1], result);
      }
    }
    System.out.printf("inline emit test: %d ok, %d pending, %d failed%n", passed, pending, failed);
    if (failed > 0) System.exit(1);
  }
}
