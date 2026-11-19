package gold;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Upstream Jolt's own inline tests (jolt-inlines/*: arkworks for secp256k1, the crates' reference implementations and
 * published vectors for the hashes), run on gold. Their vectors were captured from upstream's `cargo test` at 17a9459
 * (Quest task 096, a logging patch to InlineTestHarness::execute_inline; wip/planning/sessions/WORKER18-NOTES.md, "W5b"): each call's operand
 * registers and memory span before ("pre") and after ("post"). The tests assert every "post" against their
 * independent references, so a "post" is a checked expected output; a "pre" with no "post" is a call upstream's test
 * expected to panic.
 *
 * For each call, gold loads the "pre" memory and registers, expands the inline with its own Expander, runs the rows
 * on a Machine (secp256k1 advice computed by SecpCompute), and compares the whole span with "post"; a call upstream
 * expects to panic must abort in gold too.
 *
 * <pre>
 *   InlineVectorTest &lt;vectors.jsonl[.gz]&gt;     (default test/inline-vectors.jsonl.gz)
 * </pre>
 */
public final class InlineVectorTest {
  static final long PC = 0x8000_0000L;

  record Call(String test, int word, int rs1, int rs2, int rs3, long xRs1, long xRs2, long xRs3, long base, long[] pre,
              long[] post) {}

  static long[] words(String hex) {
    long[] w = new long[hex.length() / 16];
    for (int i = 0; i < w.length; i++) w[i] = Long.parseUnsignedLong(hex.substring(16 * i, 16 * i + 16), 16);
    return w;
  }

  static int num(Map<?, ?> m, String k) { return ((Long) m.get(k)).intValue(); }

  /** Pairs each "pre" with the following "post" (one test thread, so they are adjacent). */
  static List<Call> read(Path file) throws IOException {
    List<Call> calls = new ArrayList<>();
    Map<?, ?> pending = null;
    List<String> lines;
    if (file.toString().endsWith(".gz")) {
      try (var in = new java.util.zip.GZIPInputStream(Files.newInputStream(file))) {
        lines = new String(in.readAllBytes(), java.nio.charset.StandardCharsets.UTF_8).lines().toList();
      }
    } else {
      lines = Files.readAllLines(file);
    }
    for (String line : lines) {
      if (line.isBlank()) continue;
      Map<?, ?> m = (Map<?, ?>) Json.parse(line);
      if (m.get("kind").equals("pre")) {
        if (pending != null) calls.add(call(pending, null));
        pending = m;
      } else {
        calls.add(call(pending, m));
        pending = null;
      }
    }
    if (pending != null) calls.add(call(pending, null));
    return calls;
  }

  static Call call(Map<?, ?> pre, Map<?, ?> post) {
    int rs1 = num(pre, "rs1"), rs2 = num(pre, "rs2"), rs3 = num(pre, "rs3");
    int word = num(pre, "funct7") << 25 | rs2 << 20 | rs1 << 15 | num(pre, "funct3") << 12 | rs3 << 7 | num(pre, "opcode");
    return new Call((String) pre.get("test"), word, rs1, rs2, rs3, Json.u64(pre, "x_rs1"), Json.u64(pre, "x_rs2"),
        Json.u64(pre, "x_rs3"), Json.u64(pre, "base"), words((String) pre.get("words")),
        post == null ? null : words((String) post.get("words")));
  }

  /** Runs one call; null if gold agrees with upstream, else what differs. */
  static String check(Call c) {
    Instruction in = Decoder.decode32(c.word());
    if (!in.isValid() || !Expander.isInline(in.opcode)) return "not an inline gold knows: " + Integer.toHexString(c.word());
    Memory mem = new Memory();
    for (int i = 0; i < c.pre().length; i++) mem.store(c.base() + 8L * i, c.pre()[i]);
    Machine m = new Machine(mem, Bytecode.ofInstruction(PC, in, 4), PC, new SecpCompute());
    m.regs[c.rs1()] = c.xRs1();
    m.regs[c.rs2()] = c.xRs2();
    m.regs[c.rs3()] = c.xRs3();
    m.regs[0] = 0;
    int rows = Expander.numCycles(in);
    try {
      for (int r = 0; r < rows; r++) m.step();
    } catch (EmulationError e) {
      return c.post() == null ? null : "gold aborted, upstream didn't: " + e.getMessage();
    }
    if (c.post() == null) return "upstream panicked, gold didn't";
    for (int i = 0; i < c.post().length; i++) {
      long got = mem.load(c.base() + 8L * i);
      if (got != c.post()[i]) {
        return String.format("word %d (%#x): gold %016x, upstream %016x", i, c.base() + 8L * i, got, c.post()[i]);
      }
    }
    return null;
  }

  public static void main(String[] args) throws IOException {
    Path file = Path.of(args.length > 0 ? args[0] : "test/inline-vectors.jsonl.gz");
    if (!Files.exists(file)) {
      System.out.println("inline vector test: no vectors at " + file + " (skipped)");
      return;
    }
    List<Call> calls = read(file);
    Map<String, int[]> byInline = new LinkedHashMap<>();   // name -> {calls, ok, expected aborts}
    int failed = 0;
    for (Call c : calls) {
      Instruction in = Decoder.decode32(c.word());
      String name = Op.name(in.opcode);
      int[] t = byInline.computeIfAbsent(name, k -> new int[3]);
      t[0]++;
      String why = check(c);
      if (why == null) {
        t[1]++;
        if (c.post() == null) t[2]++;
      } else if (failed++ < 20) {
        System.out.printf("  FAIL %s (%s): %s%n", name, c.test(), why);
      }
    }
    for (Map.Entry<String, int[]> e : byInline.entrySet()) {
      int[] t = e.getValue();
      System.out.printf("  %-26s %5d calls, %5d agree (%d expected aborts)%n", e.getKey(), t[0], t[1], t[2]);
    }
    System.out.printf("inline vector test: %d calls, %d agree, %d differ%n", calls.size(), calls.size() - failed, failed);
    if (failed > 0) System.exit(1);
  }
}
