package gold;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * A frame file decoded (HOST-FRAMES.md as ruled on 2026-11-16), for the reader and the reference. Written from the
 * document, not from the writer: it shares no code with Frames, FrameWriter, UpdateWords or FileTree. Every length it
 * reads is checked against the bytes there are; a frame it can't decode is an error naming the frame and the byte.
 *
 * <p>The order: 1, 2 (the program image), then 4 and 5 on their own if a base table is established, then 3 to 9. N,
 * the number of segments, is in no frame: it is frame 7's length divided by 68.
 */
public final class FrameFile {
  /** One frame as found: its type, compression, where its header starts, its data (the padding included). */
  public static final class Frame {
    public final int type, compression;
    public final long offset;
    public final long length128;
    public final byte[] data;

    Frame(int type, int compression, long offset, long length128, byte[] data) {
      this.type = type;
      this.compression = compression;
      this.offset = offset;
      this.length128 = length128;
      this.data = data;
    }

    public boolean lru() { return compression == 1; }
  }

  /** Frame 4. */
  public static final class TableHeader {
    public String path, base;
    public int k, p1, p3;
    public long words, words128, pages;
  }

  public final List<Frame> frames = new ArrayList<>();
  public int n;
  public Frame programFrame, baseHeaderFrame, baseDataFrame;

  // frame 1
  public String programStem;
  public long fileLength, programWords, program128, programPages;
  // the base table's frame 4, if any
  public TableHeader baseHeader;
  // frame 3
  public int version;
  public String stem, workdir;
  public long[] params;
  public long dataWords, data128, dataPages;
  public long[] segWords;
  public long seg128;
  public long[] pagesWritten;
  public long[][] perAsic;
  // frame 4 (the run's)
  public TableHeader table;
  // frames 7, 8
  public byte[][] records;
  public long[] adviceCounts;
  public long[][] advice;

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
      long low = h.u64();
      int type = (int) h.u32();
      long len = h.u32();
      int compression = (int) (low & 0xff);
      if ((low >>> 8) != 0) throw new IllegalArgumentException("the header at byte " + pos + ": reserved bits [63:8] aren't 0");
      boolean words = type == 2 || type == 5 || type == 6 || type == 9;
      if (compression > (words ? 1 : 0)) {
        throw new IllegalArgumentException("frame " + type + " at byte " + pos + ": compression " + compression
            + (words ? " (0 or 1)" : " (only frames 2, 5, 6 and 9 are compressed)"));
      }
      if (pos + 16 + 16 * len > file.length) {
        throw new IllegalArgumentException("frame " + type + " at byte " + pos + ": " + len + " 128-bit words run past the end");
      }
      byte[] data = java.util.Arrays.copyOfRange(file, (int) pos + 16, (int) (pos + 16 + 16 * len));
      f.frames.add(new Frame(type, compression, pos, len, data));
      pos += 16 + 16 * len;
    }
    int[] order = f.frames.size() == 11 ? new int[] {1, 2, 4, 5, 3, 4, 5, 6, 7, 8, 9} : new int[] {1, 2, 3, 4, 5, 6, 7, 8, 9};
    if (f.frames.size() != order.length) throw new IllegalArgumentException(f.frames.size() + " frames: 9, or 11 with a base table");
    for (int i = 0; i < order.length; i++) {
      if (f.frames.get(i).type != order[i]) {
        throw new IllegalArgumentException("frame " + (i + 1) + " in the file has type " + f.frames.get(i).type + ", " + order[i] + " expected");
      }
    }
    f.programFrame = f.frames.get(1);
    if (order.length == 11) { f.baseHeaderFrame = f.frames.get(2); f.baseDataFrame = f.frames.get(3); }
    long records = f.get(7).length128;
    if (records % 68 != 0) throw new IllegalArgumentException("frame 7: " + records + " 128-bit words isn't a whole number of 68-word records");
    f.n = (int) (records / 68);
    if (f.n != 64 && f.n != 128 && f.n != 256) throw new IllegalArgumentException("frame 7 holds " + f.n + " records: N is 64, 128 or 256");
    f.decode();
    return f;
  }

  /** The proof's frame of this type (for 4 and 5, the run's, not the base table's). */
  public Frame get(int type) {
    for (int i = frames.size() - 1; i >= 0; i--) if (frames.get(i).type == type) return frames.get(i);
    return null;
  }

  static TableHeader tableHeader(Frame fr, String where) {
    Cursor c = new Cursor(fr.data, where);
    TableHeader t = new TableHeader();
    t.path = c.string();
    t.base = c.string();
    t.k = (int) c.u32();
    t.p1 = (int) c.u32();
    t.p3 = (int) c.u32();
    t.words = c.u32();
    t.words128 = c.u32();
    t.pages = c.u32();
    c.endPadding();
    if (t.k > 26) throw new IllegalArgumentException(where + ": k " + t.k);
    return t;
  }

  private void decode() {
    Cursor c = new Cursor(get(1).data, "frame 1");
    programStem = c.string();
    fileLength = c.u32();
    programWords = c.u32();
    program128 = c.u32();
    programPages = c.u32();
    c.endPadding();

    if (baseHeaderFrame != null) baseHeader = tableHeader(baseHeaderFrame, "the base table's frame 4");

    c = new Cursor(get(3).data, "frame 3");
    version = (int) c.u32();
    stem = c.string();
    workdir = c.string();
    int nf = (int) c.u32();
    params = new long[nf];
    for (int i = 0; i < nf; i++) params[i] = c.u64();
    dataWords = c.u32();
    data128 = c.u32();
    dataPages = c.u32();
    segWords = new long[n];
    for (int i = 0; i < n; i++) segWords[i] = c.u32();
    seg128 = c.u32();
    pagesWritten = new long[n];
    for (int i = 0; i < n; i++) pagesWritten[i] = c.u32();
    perAsic = new long[n][3];
    for (int i = 0; i < n; i++) for (int j = 0; j < 3; j++) perAsic[i][j] = c.u32();
    c.endPadding();

    table = tableHeader(get(4), "frame 4");

    c = new Cursor(get(7).data, "frame 7");
    records = new byte[n][];
    for (int i = 0; i < n; i++) records[i] = c.bytes(68 * 16);
    c.endPadding();

    c = new Cursor(get(8).data, "frame 8");
    adviceCounts = new long[n];
    advice = new long[n][];
    for (int i = 0; i < n; i++) adviceCounts[i] = c.u32();
    for (int i = 0; i < n; i++) {
      advice[i] = new long[(int) adviceCounts[i]];
      for (int j = 0; j < advice[i].length; j++) advice[i][j] = c.u64();
      if (advice[i].length % 2 == 1 && c.u64() != 0) throw new IllegalArgumentException("frame 8: log " + i + "'s padding isn't zero");
    }
    c.endPadding();
  }

  /** Frame 7's record s, 128-bit word w, bits [hi:lo] (at most 64 bits). */
  public long recordBits(int s, int w, int lo, int hi) {
    byte[] r = records[s];
    long v = 0;
    for (int bit = hi; bit >= lo; bit--) v = v << 1 | ((r[16 * w + bit / 8] >>> (bit % 8)) & 1);
    return v;
  }
}
