package jumpmap;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * Reads section 16's segment file as gold writes it: frame 1, then per segment frames 2, 3 and one of 4 or 5. Keeps
 * each segment's processor state (frame 2) and its update words (frame 4 raw, or frame 5 decoded: a 32-entry true-LRU
 * table, all zero at the frame's start; a hit is 1 and a 5-bit index, a miss 0, a size bit (1: 64) and the value).
 */
public final class SegmentFile {
  public static final int STATE_WORDS = 131;

  public static final class Segment {
    public long[] state;
    public long[] updates;
    public int updateFrame;
    public long adviceWords;
  }

  public final List<Segment> segments = new ArrayList<>();

  public SegmentFile(Path file) throws IOException {
    ByteBuffer b = ByteBuffer.wrap(Files.readAllBytes(file)).order(ByteOrder.LITTLE_ENDIAN);
    Segment cur = null;
    boolean sawPageCounts = false;
    while (b.remaining() > 0) {
      if (b.remaining() < 8) throw new IOException("truncated frame header");
      int at = b.position();
      long words = b.getInt() & 0xffffffffL;
      int type = b.getInt();
      if (words < 2 || at + 4 * words > b.limit()) throw new IOException("bad frame length at byte " + at);
      int dataBytes = (int) (4 * (words - 2));
      ByteBuffer d = b.slice(at + 8, dataBytes).order(ByteOrder.LITTLE_ENDIAN);
      b.position(at + 8 + dataBytes);
      switch (type) {
        case 1: sawPageCounts = true; break;
        case 6: case 7: break;                                         // not written by gold yet; ignored
        case 2: {
          if (!sawPageCounts) throw new IOException("frame 2 before frame 1");
          if (dataBytes != 8 * STATE_WORDS) throw new IOException("frame 2 of " + dataBytes + " bytes");
          cur = new Segment();
          cur.state = new long[STATE_WORDS];
          for (int i = 0; i < STATE_WORDS; i++) cur.state[i] = d.getLong();
          segments.add(cur);
          break;
        }
        case 3: need(cur, type); cur.adviceWords = dataBytes / 8; break;
        case 4: {
          need(cur, type);
          long[] u = new long[dataBytes / 8];
          for (int i = 0; i < u.length; i++) u[i] = d.getLong();
          cur.updates = u; cur.updateFrame = 4;
          break;
        }
        case 5: need(cur, type); cur.updates = lruDecode(d); cur.updateFrame = 5; break;
        default: throw new IOException("unknown frame type " + type);
      }
    }
    for (Segment s : segments) if (s.updates == null) throw new IOException("a segment without updates");
  }

  private static void need(Segment s, int type) throws IOException {
    if (s == null) throw new IOException("frame " + type + " before any frame 2");
  }

  static long[] lruDecode(ByteBuffer d) {
    int count = d.getInt();
    long[] table = new long[32];
    long[] out = new long[count];
    long bit = 32;                                   // bit position in d (after the count)
    for (int i = 0; i < count; i++) {
      long w;
      if (bits(d, bit, 1) == 1) {
        int idx = (int) bits(d, bit + 1, 5);
        bit += 6;
        w = table[idx];
        System.arraycopy(table, 0, table, 1, idx);
      } else {
        boolean wide = bits(d, bit + 1, 1) == 1;
        bit += 2;
        w = bits(d, bit, wide ? 64 : 32);
        bit += wide ? 64 : 32;
        System.arraycopy(table, 0, table, 1, 31);
      }
      table[0] = w;
      out[i] = w;
    }
    return out;
  }

  /** n bits at bit position p, least significant bit first within each byte. */
  private static long bits(ByteBuffer d, long p, int n) {
    long v = 0;
    for (int i = 0; i < n; i++) {
      long q = p + i;
      if (((d.get((int) (q >>> 3)) >>> (q & 7)) & 1) != 0) v |= 1L << i;
    }
    return v;
  }
}
