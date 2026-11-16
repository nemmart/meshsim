package gold;

import java.io.ByteArrayOutputStream;

/**
 * The host's frames for v1.4 (v1.4_wip/planning/HOST-FRAMES.md), the writer's side: the frame header, the 128-bit
 * padding, strings and little-endian fields. The reader and the reference (FrameFile, Reference) decode on their own
 * and share nothing with this.
 *
 * <pre>
 *   header   one 128-bit word: [7:0] compression (0 none, 1 LRU); [63:8] reserved, 0; [95:64] u32 type;
 *            [127:96] u32 length of the data in 128-bit words
 *   data     zero-padded to a whole number of 128-bit words
 * </pre>
 *
 * Lengths and counts are 32 bits, never 64 (owner): a value that doesn't fit is an error, never truncated.
 */
public final class Frames {
  public static final int PROGRAM_HEADER = 1, PROGRAM_DATA = 2, PROOF_HEADER = 3, TABLE_HEADER = 4, TABLE_DATA = 5,
      DATA_IMAGE = 6, REGISTERS = 7, ADVICE = 8, SEGMENTS = 9;
  public static final String[] NAMES = {"", "program image header", "program image data", "proof header",
      "jump table header", "jump table data", "data image", "register states", "advice logs", "memory segments"};
  /** The compression types (the header's [7:0]). */
  public static final int NONE = 0, LRU = 1;
  /** N, the number of ASICs and so of segments: system configuration, in no frame (owner). */
  public static final int[] ASIC_COUNTS = {64, 128, 256};
  public static final int DEFAULT_ASICS = 128;
  public static final int FORMAT_VERSION = 1;
  public static final int PAGE_BYTES = 4096, PAGE_WORDS = 512;

  private Frames() {}

  /** A u32 count or length: rejects anything that doesn't fit in 32 bits. */
  public static int u32(long v, String what) {
    if (v < 0 || v > 0xffffffffL) throw new IllegalStateException(what + " = " + v + " doesn't fit in 32 bits");
    return (int) v;
  }

  /** 128-bit words holding n bytes. */
  public static long words128(long bytes) { return (bytes + 15) / 16; }

  /** Appends one frame: the 128-bit header, the data, the padding. Returns the frame's bytes. */
  public static long frame(ByteArrayOutputStream out, int type, int compression, byte[] data) {
    long words = words128(data.length);
    Bytes h = new Bytes();
    h.u64(compression);
    h.u32(type);
    h.u32(u32(words, NAMES[type] + "'s length in 128-bit words"));
    out.writeBytes(h.toArray());
    out.writeBytes(data);
    out.writeBytes(new byte[(int) (16 * words - data.length)]);
    return 16 * (words + 1);
  }

  /** A little-endian byte builder. */
  public static final class Bytes {
    private final ByteArrayOutputStream b = new ByteArrayOutputStream();

    public Bytes u32(long v) {
      for (int i = 0; i < 4; i++) b.write((int) (v >>> (8 * i)));
      return this;
    }

    public Bytes u64(long v) {
      for (int i = 0; i < 8; i++) b.write((int) (v >>> (8 * i)));
      return this;
    }

    public Bytes raw(byte[] x) { b.writeBytes(x); return this; }

    /** A string: u32 length in bytes, the bytes (UTF-8), padded to a whole number of 32-bit words. */
    public Bytes string(String s) {
      byte[] x = s.getBytes(java.nio.charset.StandardCharsets.UTF_8);
      u32(Frames.u32(x.length, "a string's length"));
      b.writeBytes(x);
      b.writeBytes(new byte[(4 - x.length % 4) % 4]);
      return this;
    }

    public int size() { return b.size(); }

    public byte[] toArray() { return b.toByteArray(); }
  }

  /** u64 words as little-endian bytes. */
  public static byte[] bytes(long[] words) {
    byte[] out = new byte[8 * words.length];
    for (int i = 0; i < words.length; i++) for (int j = 0; j < 8; j++) out[8 * i + j] = (byte) (words[i] >>> (8 * j));
    return out;
  }
}
