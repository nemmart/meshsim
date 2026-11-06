package gold;

import java.io.ByteArrayOutputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Writes DECISIONS-v1.3 section 16's segment file for a run, with every choice WORKER16's segment_writer makes
 * (jolt-asic-planning worker16-w3 at ca30b5e; the rules are listed in wip/planning/sessions/WORKER18-NOTES.md, "Stage 2"):
 *
 * <pre>
 *   frame 1   page counts: per segment, the distinct 16 KB pages among its dirty cells (u32 each)
 *   per segment, in order:
 *   frame 2   processor state before its first row: pc, instr_idx, next offset, registers 0-127 (131 x u64)
 *   frame 3   its advice words, in row order (u64 each)
 *   frame 4   its update words raw (u64 each), or
 *   frame 5   the same words LRU-coded: u32 count, then the codes (frame 5 only if non-empty and strictly smaller)
 * </pre>
 *
 * Everything is computed here, the secp256k1 advice included (SecpCompute).
 */
public final class SegmentWriter {
  public static final int F_PAGE_COUNTS = 1, F_STATE = 2, F_ADVICE = 3, F_UPDATES_RAW = 4, F_UPDATES_LRU = 5,
      F_JUMP_TABLE = 6, F_PROOF_PARAMS = 7;
  static final int RECORD_WORDS = 68;
  static final int STATE_WORDS = 131;
  static final int PAGE_CELL_SHIFT = 11;         // a 16 KB page holds 2^11 cells (section 12)
  static final int LRU_ENTRIES = 32;

  /**
   * The file's format. V1_3 (DECISIONS-v1.3 section 16, 2026-11-05): frame 7 (the proof parameters) first, then frame 1
   * (the per-segment table: each segment's distinct 4 KB pages written), frame 6 (the proof's jump-target table), then
   * per segment frame 2 as v1.26.1's 68-word record (spec 4.21.2), frame 3 and frame 4 or 5. V1_2: the format WORKER16's
   * segment_writer ca30b5e writes (frame 1 of 16 KB page counts, frame 2 as 131 x u64, no frames 6 and 7).
   */
  public enum Format { V1_2, V1_3 }

  /** The program can't be proven: a store into code, or an access outside the provable range (section 25). */
  public static final class Rejected extends RuntimeException {
    private static final long serialVersionUID = 1L;
    Rejected(String m) { super(m); }
  }

  /** One segment's chunks. */
  static final class Segment {
    long firstRow, rows;
    int pages4k;
    final long[] state = new long[STATE_WORDS];
    final LongList advice = new LongList();
    final LongList updates = new LongList();
    int pages, frameType;
    byte[] updateData;
  }

  /** Statistics for the side file. */
  public long totalRows, adviceWords, updateWords;

  private final Program program;
  private final long[] cuts;
  private final Format format;
  /** After write(): the proof's parameters (v1.3). */
  public ProofParams params;
  public final List<Segment> segments = new ArrayList<>();

  public SegmentWriter(Program program, long[] cuts) {
    this(program, cuts, Format.V1_2);
  }

  public SegmentWriter(Program program, long[] cuts, Format format) {
    this.format = format;
    if (cuts.length == 0 || cuts[0] != 0) throw new IllegalArgumentException("the first cut must be 0");
    for (int k = 1; k < cuts.length; k++) {
      if (cuts[k] <= cuts[k - 1]) throw new IllegalArgumentException("cuts must increase");
    }
    this.program = program;
    this.cuts = cuts;
  }

  /** Default cuts: k * segmentRows for k < count (128 segments of 2^22 for a 2^29 proof). */
  public static long[] defaultCuts(long segmentRows, int count) {
    long[] c = new long[count];
    for (int k = 0; k < count; k++) c[k] = k * segmentRows;
    return c;
  }

  /** Runs the program over all segments, then returns the file. */
  public byte[] write() {
    Machine m = new Machine(program, new SecpCompute());
    JumpTable jumps = format == Format.V1_3 ? new JumpTable(program) : null;
    final long lowest = program.layout.lowestAddress();
    final long spanCells = ((1L << 32) - lowest) / 8 + 1;
    final long[] dirtyBits = new long[(int) ((spanCells + 63) / 64)];
    final IntList dirty = new IntList();

    for (int k = 0; k < cuts.length; k++) {
      Segment sg = new Segment();
      segments.add(sg);
      sg.firstRow = cuts[k];
      long end = k + 1 < cuts.length ? cuts[k + 1] : Long.MAX_VALUE;
      if (!m.halted()) {                       // past the end of the run: pc 0, instr_idx 0, offset 0
        if (m.cycle != cuts[k]) throw new IllegalStateException("cut " + cuts[k] + " not reached");
        sg.state[0] = m.pc;
        sg.state[1] = m.index;
        sg.state[2] = m.index - m.code.indexOf(m.pc);
      }
      for (int r = 1; r < Isa.NUM_REGS; r++) sg.state[3 + r] = m.regs[r];

      while (m.cycle < end && !m.halted()) {
        VInstr before = m.code.at(m.index);
        long beforePc = m.pc;
        m.step();
        if (jumps != null) jumps.observe(before, beforePc, m);
        sg.rows++;
        int op = (int) (m.row[1] & 0xff);
        if (op == Op.VirtualAdvice) sg.advice.add(m.row[3]);       // every advice row: the word its rd takes
        if (Isa.isStore(op)) {
          long addr = m.row[7];
          if (addr < lowest || ((addr - lowest) & 7) != 0) throw new EmulationError(String.format("store below lowest address: %#x", addr));
          int cell = (int) ((addr - lowest) >>> 3);
          if ((dirtyBits[cell >>> 6] >>> (cell & 63) & 1) == 0) {
            dirtyBits[cell >>> 6] |= 1L << (cell & 63);
            dirty.add(cell);
          }
        }
      }
      totalRows += sg.rows;

      // update words: the dirty cells' maximal runs in address order, each a header and the values at the end
      int[] cells = dirty.toSortedArray();
      long lastPage = -1;
      for (int a = 0; a < cells.length; ) {
        int b = a + 1;
        while (b < cells.length && cells[b] == cells[b - 1] + 1) b++;
        sg.updates.add((cells[a] & 0xffffffffL) | ((long) (b - a) << 32));
        for (int j = a; j < b; j++) sg.updates.add(program.memory.load(lowest + 8L * cells[j]));
        a = b;
      }
      long last4k = -1;
      for (int c : cells) {
        if ((c >>> PAGE_CELL_SHIFT) != lastPage) { sg.pages++; lastPage = c >>> PAGE_CELL_SHIFT; }
        if ((c >>> 9) != last4k) { sg.pages4k++; last4k = c >>> 9; }   // 4 KB pages: 512 cells
        if (format == Format.V1_3) {                // stores into code: the write list against the Text ranges, per segment
          long a = lowest + 8L * c;
          for (long[] r : program.textRanges) {
            if (a + 8 > r[0] && a < r[1]) {
              throw new Rejected(String.format("segment %d writes %#x, in the code (a Text section): stores into code "
                  + "are rejected (DECISIONS-v1.3 section 25)", k, a));
            }
          }
        }
        dirtyBits[c >>> 6] &= ~(1L << (c & 63));
      }
      dirty.clear();

      long[] words = sg.updates.toArray();
      byte[] lru = lruEncode(words);
      long rawBytes = 8L * words.length, lruBytes = (lru.length + 3) / 4 * 4L;
      if (words.length > 0 && lruBytes < rawBytes) {        // strictly smaller; a tie or an empty list: raw
        sg.frameType = F_UPDATES_LRU;
        sg.updateData = lru;
      } else {
        sg.frameType = F_UPDATES_RAW;
        sg.updateData = toBytes(words);
      }
      adviceWords += sg.advice.size();
      updateWords += words.length;
    }

    ByteArrayOutputStream file = new ByteArrayOutputStream();
    if (format == Format.V1_3) {
      params = new ProofParams(program, m);        // K by a16z's prover rule, after the run (section 25's checks)
      try {
        params.check(m);
      } catch (ProofParams.Unprovable e) {
        throw new Rejected(e.getMessage());
      }
      ioRegionSize(params);                           // rejects an I/O region of 2 GiB or more before writing
      frame(file, F_PROOF_PARAMS, params.frame7());
      byte[] counts = new byte[4 * segments.size()];
      for (int k = 0; k < segments.size(); k++) putInt(counts, 4 * k, segments.get(k).pages4k);
      frame(file, F_PAGE_COUNTS, counts);
      frame(file, F_JUMP_TABLE, jumps.frame6());
      for (Segment sg : segments) {
        frame(file, F_STATE, stateRecord(sg));
        frame(file, F_ADVICE, toBytes(sg.advice.toArray()));
        frame(file, sg.frameType, sg.updateData);
      }
      return file.toByteArray();
    }
    byte[] counts = new byte[4 * segments.size()];
    for (int k = 0; k < segments.size(); k++) putInt(counts, 4 * k, segments.get(k).pages);
    frame(file, F_PAGE_COUNTS, counts);
    for (Segment sg : segments) {
      frame(file, F_STATE, toBytes(sg.state));
      frame(file, F_ADVICE, toBytes(sg.advice.toArray()));
      frame(file, sg.frameType, sg.updateData);
    }
    return file.toByteArray();
  }

  /**
   * Frame 5's data: a u32 word count, then per word, least significant bit first: a hit is 1 and the lowest matching
   * entry's index (5 bits), the entry moving to the front; a miss is 0, a size bit (1: 64 bits, 0: 32) and the value,
   * pushed at the front. The table starts all zero.
   */
  static byte[] lruEncode(long[] words) {
    long[] table = new long[LRU_ENTRIES];
    BitWriter bits = new BitWriter();
    for (long w : words) {
      int hit = -1;
      for (int i = 0; i < LRU_ENTRIES; i++) if (table[i] == w) { hit = i; break; }
      if (hit >= 0) {
        bits.put(1, 1);
        bits.put(hit, 5);
        System.arraycopy(table, 0, table, 1, hit);
      } else {
        bits.put(0, 1);
        if ((w >>> 32) != 0) { bits.put(1, 1); bits.put(w, 64); } else { bits.put(0, 1); bits.put(w, 32); }
        System.arraycopy(table, 0, table, 1, LRU_ENTRIES - 1);
      }
      table[0] = w;
    }
    byte[] codes = bits.toByteArray();
    byte[] out = new byte[4 + codes.length];
    putInt(out, 0, words.length);
    System.arraycopy(codes, 0, out, 4, codes.length);
    return out;
  }

  /**
   * Frame 2 at v1.3: v1.26.1's 68-word record, 128-bit words, little-endian (spec 4.21.2; DECISIONS-v1.3 section 23):
   * word 0 pc [63:0], instr_idx [95:64], next offset [111:96]; word 1 only the advice field [124:102], the segment's
   * advice values (frame 3's count); words 2-65 registers 2i, 2i + 1; word 66 log2 K [71:66] and the I/O region's
   * size, 0x8000_0000 - lowest_address, exact in bytes at [102:72] ([127:103] spare; spec 4.21.2, 66f399e), from
   * frame 7, the roots and depths 0 for C&C 3 (owner, 2026-11-05); word 67 the template bitmap, 0.
   */
  byte[] stateRecord(Segment sg) {
    long[] w = new long[2 * RECORD_WORDS];               // each 128-bit word as (low u64, high u64)
    w[0] = sg.state[0];
    w[1] = (sg.state[1] & 0xffffffffL) | (sg.state[2] & 0xffffL) << 32;
    w[3] = (long) sg.advice.size() << (102 - 64);
    for (int r = 0; r < Isa.NUM_REGS; r++) w[4 + r] = sg.state[3 + r];
    w[2 * 66 + 1] = (long) params.log2K << (66 - 64) | ioRegionSize(params) << (72 - 64);
    return toBytes(w);
  }

  /** Word 66's I/O region size, 0x8000_0000 - lowest_address: 31 bits, so a region of 2 GiB or more is rejected. */
  static long ioRegionSize(ProofParams p) {
    long size = Layout.RAM_START - p.lowestAddress;
    if (size <= 0 || size >= 1L << 31) {
      throw new Rejected(String.format("the I/O region below 0x80000000 is %#x bytes (lowest_address %#x): word 66 "
          + "holds less than 2 GiB (spec 4.21.2)", size, p.lowestAddress));
    }
    return size;
  }

  /** A frame: u32 length in words (header included), u32 type, the data zero-padded to whole words. */
  static void frame(ByteArrayOutputStream out, int type, byte[] data) {
    int words = 2 + (data.length + 3) / 4;
    byte[] header = new byte[8];
    putInt(header, 0, words);
    putInt(header, 4, type);
    out.writeBytes(header);
    out.writeBytes(data);
    out.writeBytes(new byte[4 * (words - 2) - data.length]);
  }

  static void putInt(byte[] b, int at, int v) {
    for (int i = 0; i < 4; i++) b[at + i] = (byte) (v >>> (8 * i));
  }

  static byte[] toBytes(long[] words) {
    byte[] b = new byte[8 * words.length];
    for (int i = 0; i < words.length; i++) for (int j = 0; j < 8; j++) b[8 * i + j] = (byte) (words[i] >>> (8 * j));
    return b;
  }

  /** Bits appended least significant first within each byte. */
  static final class BitWriter {
    private byte[] bytes = new byte[64];
    private long nbits;

    void put(long v, int n) {
      for (int i = 0; i < n; i++) {
        int at = (int) (nbits >>> 3);
        if (at == bytes.length) bytes = Arrays.copyOf(bytes, 2 * bytes.length);
        if (((v >>> i) & 1) != 0) bytes[at] |= (byte) (1 << (nbits & 7));
        nbits++;
      }
    }

    byte[] toByteArray() { return Arrays.copyOf(bytes, (int) ((nbits + 7) >>> 3)); }
  }

  /** Growable primitive lists (no boxing over hundreds of thousands of words). */
  static final class LongList {
    private long[] a = new long[16];
    private int n;
    void add(long v) { if (n == a.length) a = Arrays.copyOf(a, 2 * n); a[n++] = v; }
    int size() { return n; }
    long[] toArray() { return Arrays.copyOf(a, n); }
  }

  static final class IntList {
    private int[] a = new int[16];
    private int n;
    void add(int v) { if (n == a.length) a = Arrays.copyOf(a, 2 * n); a[n++] = v; }
    void clear() { n = 0; }
    int[] toSortedArray() { int[] s = Arrays.copyOf(a, n); Arrays.sort(s); return s; }
  }
}
