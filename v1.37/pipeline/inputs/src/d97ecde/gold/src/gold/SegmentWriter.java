package gold;

import java.io.ByteArrayOutputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Writes DECISIONS-v1.3 section 16's segment file for a run, with every choice WORKER16's segment_writer makes
 * (jolt-asic-planning worker16-w3 at ca30b5e; the rules are listed in WORKER18/NOTES.md, "Stage 2"):
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
  public static final int F_PAGE_COUNTS = 1, F_STATE = 2, F_ADVICE = 3, F_UPDATES_RAW = 4, F_UPDATES_LRU = 5;
  static final int STATE_WORDS = 131;
  static final int PAGE_CELL_SHIFT = 11;         // a 16 KB page holds 2^11 cells (section 12)
  static final int LRU_ENTRIES = 32;

  /** One segment's chunks. */
  static final class Segment {
    long firstRow, rows;
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
  public final List<Segment> segments = new ArrayList<>();

  public SegmentWriter(Program program, long[] cuts) {
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
        m.step();
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
      for (int c : cells) {
        if ((c >>> PAGE_CELL_SHIFT) != lastPage) { sg.pages++; lastPage = c >>> PAGE_CELL_SHIFT; }
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
