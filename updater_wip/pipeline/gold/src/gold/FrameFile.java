package gold;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.TreeMap;

/**
 * A frame file decoded (HOST-FRAMES.md), for the reader and the reference. Written from the document, not from the
 * writer: it shares no code with Frames, FrameWriter, UpdateWords or FileTree. Every length it reads is checked
 * against the bytes there are; a frame it can't decode is an error naming the frame and the byte.
 */
public final class FrameFile {
  public static final int SEGMENTS = 128;

  /** One frame as found: its type, where its header starts, its data (the padding included). */
  public static final class Frame {
    public final int type;
    public final long offset;
    public final long length128;
    public final byte[] data;

    Frame(int type, long offset, long length128, byte[] data) {
      this.type = type;
      this.offset = offset;
      this.length128 = length128;
      this.data = data;
    }
  }

  public final List<Frame> frames = new ArrayList<>();

  // frame 1
  public long fileLength;
  public TreeMap<Long, long[]> programPages = new TreeMap<>();
  // frame 2
  public boolean hasBase;
  public int baseK, baseP1, baseP3;
  public byte[] baseLines;
  // frame 3
  public int version;
  public String stem, workdir;
  public byte[] programId;
  public long[] params;
  public long dataWords, data128, dataPages;
  public boolean dataLru;
  public final long[] segWords = new long[SEGMENTS];
  public long seg128;
  public boolean segLru;
  public final long[] pagesWritten = new long[SEGMENTS];
  public final long[][] perAsic = new long[SEGMENTS][3];
  // frame 4
  public String basePath;
  public int k, p1, p3;
  public long tableWords, table128, tablePages;
  public boolean tableLru;
  // frames 7, 8
  public final byte[][] records = new byte[SEGMENTS][];
  public final long[] adviceCounts = new long[SEGMENTS];
  public final long[][] advice = new long[SEGMENTS][];

  /** A little-endian cursor over one frame's data. */
  static final class Cursor {
    final byte[] b;
    final String where;
    int at;

    Cursor(byte[] b, String where) { this.b = b; this.where = where; }

    void need(long n) {
      if (at + n > b.length) throw new IllegalArgumentException(where + ": needs " + n + " more bytes at byte " + at + " of " + b.length);
    }

    long u32() {
      need(4);
      long v = 0;
      for (int i = 0; i < 4; i++) v |= (b[at + i] & 0xffL) << (8 * i);
      at += 4;
      return v;
    }

    long u64() {
      need(8);
      long v = 0;
      for (int i = 0; i < 8; i++) v |= (b[at + i] & 0xffL) << (8 * i);
      at += 8;
      return v;
    }

    byte[] bytes(long n) {
      need(n);
      byte[] x = java.util.Arrays.copyOfRange(b, at, at + (int) n);
      at += (int) n;
      return x;
    }

    String string() {
      long n = u32();
      String s = new String(bytes(n), StandardCharsets.UTF_8);
      long pad = (4 - n % 4) % 4;
      for (byte x : bytes(pad)) if (x != 0) throw new IllegalArgumentException(where + ": a string's padding isn't zero");
      return s;
    }

    /** The rest must be zero: the padding to a whole 128-bit word, and no more than 15 bytes of it. */
    void endPadding() {
      if (b.length - at > 15) throw new IllegalArgumentException(where + ": " + (b.length - at) + " bytes left over after the data");
      for (int i = at; i < b.length; i++) if (b[i] != 0) throw new IllegalArgumentException(where + ": padding isn't zero");
    }
  }

  public static FrameFile parse(byte[] file) {
    FrameFile f = new FrameFile();
    long pos = 0;
    while (pos < file.length) {
      if (pos + 16 > file.length) throw new IllegalArgumentException("a frame header at byte " + pos + " runs past the end");
      Cursor h = new Cursor(java.util.Arrays.copyOfRange(file, (int) pos, (int) pos + 16), "the header at byte " + pos);
      long reserved = h.u64();
      int type = (int) h.u32();
      long len = h.u32();
      if (reserved != 0) throw new IllegalArgumentException("the header at byte " + pos + ": reserved bits [63:0] aren't 0");
      if (pos + 16 + 16 * len > file.length) {
        throw new IllegalArgumentException("frame " + type + " at byte " + pos + ": " + len + " 128-bit words run past the end");
      }
      byte[] data = java.util.Arrays.copyOfRange(file, (int) pos + 16, (int) (pos + 16 + 16 * len));
      f.frames.add(new Frame(type, pos, len, data));
      pos += 16 + 16 * len;
    }
    // the order: 1, 2 (optional), 3, 4, 5, 6, 7, 8, 9
    int[] order = f.frames.size() == 9 ? new int[] {1, 2, 3, 4, 5, 6, 7, 8, 9} : new int[] {1, 3, 4, 5, 6, 7, 8, 9};
    if (f.frames.size() != order.length) throw new IllegalArgumentException(f.frames.size() + " frames: 8 or 9 expected");
    for (int i = 0; i < order.length; i++) {
      if (f.frames.get(i).type != order[i]) {
        throw new IllegalArgumentException("frame " + (i + 1) + " in the file has type " + f.frames.get(i).type + ", " + order[i] + " expected");
      }
    }
    f.decode();
    return f;
  }

  public Frame get(int type) {
    for (Frame fr : frames) if (fr.type == type) return fr;
    return null;
  }

  private void decode() {
    Cursor c = new Cursor(get(1).data, "frame 1");
    fileLength = c.u32();
    long n = c.u32();
    long last = -1;
    for (long i = 0; i < n; i++) {
      long off = c.u64();
      if (off % 4096 != 0 || (last >= 0 && off <= last)) throw new IllegalArgumentException("frame 1: page offset " + off + " not ascending in 4 KB");
      last = off;
      long[] w = new long[512];
      for (int j = 0; j < 512; j++) w[j] = c.u64();
      programPages.put(off / 4096, w);
    }
    c.endPadding();

    Frame b = get(2);
    if (b != null) {
      hasBase = true;
      c = new Cursor(b.data, "frame 2");
      baseK = (int) c.u32();
      baseP1 = (int) c.u32();
      baseP3 = (int) c.u32();
      baseLines = c.bytes(128L << baseK);
      c.endPadding();
    }

    c = new Cursor(get(3).data, "frame 3");
    version = (int) c.u32();
    stem = c.string();
    programId = c.bytes(32);
    workdir = c.string();
    int nf = (int) c.u32();
    params = new long[nf];
    for (int i = 0; i < nf; i++) params[i] = c.u64();
    dataWords = c.u32();
    data128 = c.u32();
    dataLru = flag(c.u32(), "frame 3's data image LRU");
    dataPages = c.u32();
    for (int i = 0; i < SEGMENTS; i++) segWords[i] = c.u32();
    seg128 = c.u32();
    segLru = flag(c.u32(), "frame 3's memory segments LRU");
    for (int i = 0; i < SEGMENTS; i++) pagesWritten[i] = c.u32();
    for (int i = 0; i < SEGMENTS; i++) for (int j = 0; j < 3; j++) perAsic[i][j] = c.u32();
    c.endPadding();

    c = new Cursor(get(4).data, "frame 4");
    basePath = c.string();
    k = (int) c.u32();
    p1 = (int) c.u32();
    p3 = (int) c.u32();
    tableWords = c.u32();
    table128 = c.u32();
    tableLru = flag(c.u32(), "frame 4's LRU");
    tablePages = c.u32();
    c.endPadding();

    c = new Cursor(get(7).data, "frame 7");
    for (int i = 0; i < SEGMENTS; i++) records[i] = c.bytes(68 * 16);
    c.endPadding();

    c = new Cursor(get(8).data, "frame 8");
    for (int i = 0; i < SEGMENTS; i++) adviceCounts[i] = c.u32();
    for (int i = 0; i < SEGMENTS; i++) {
      advice[i] = new long[(int) adviceCounts[i]];
      for (int j = 0; j < advice[i].length; j++) advice[i][j] = c.u64();
      if (advice[i].length % 2 == 1 && c.u64() != 0) throw new IllegalArgumentException("frame 8: log " + i + "'s padding isn't zero");
    }
    c.endPadding();
  }

  static boolean flag(long v, String what) {
    if (v > 1) throw new IllegalArgumentException(what + " is " + v + ", 0 or 1 expected");
    return v == 1;
  }

  /** Frame 7's record s, 128-bit word w, bits [hi:lo] (at most 64 bits). */
  public long recordBits(int s, int w, int lo, int hi) {
    byte[] r = records[s];
    long v = 0;
    for (int bit = hi; bit >= lo; bit--) v = v << 1 | ((r[16 * w + bit / 8] >>> (bit % 8)) & 1);
    return v;
  }
}
