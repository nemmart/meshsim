package gold;

import java.io.ByteArrayOutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * A packet file read and checked (v1.4_wip/planning/HOST-LINK.md), for the reader. Written from the document and from
 * WORKER2's NOTES.md sections 2 to 4, not from the packetizer: it shares no code with Packets, and its CRC-32C is its
 * own, from the definition.
 *
 * <p>Where a header is expected, zero bytes are idle and skipped; the first byte that is not zero starts a header.
 * Then, in this order, a packet is refused for:
 *
 * <pre>
 *   truncated      the file ends inside a header or inside the packet's data (and, at the end, inside a frame)
 *   header-crc     the CRC-32C of header bytes 0 to 11 is not bytes 12 to 15
 *   stream         the stream is not 1 or 2
 *   length         the length is over 32
 *   reserved       bits [63:56] are not 0
 *   data-checksum  the CRC-32C of the data is not bytes 8 to 11
 *   number         a packet with data: not the next number on its stream (1 first, 1 after 0xFFFFE);
 *                  a packet of length 0: not the number the stream's next packet with data will carry (it takes none)
 *   frame-end      stream 2: a frame ends before the end of the packet its last word is in
 *   first-byte     only with noIdle: a zero byte where a header is expected
 * </pre>
 *
 * Frames are tracked on stream 2 only: the first data word after a frame's end is a frame's header word, whose
 * [127:96] is the frame's length in words. Stream 1's data is checked and counted, and kept nowhere.
 */
public final class PacketFile {
  /** A packet, or the file, that failed a check. */
  public static final class Refused extends RuntimeException {
    private static final long serialVersionUID = 1;
    /** The check's name, as in the class comment. */
    public final String check;
    /** The packet, counted from 1 in the file; the byte it starts at. */
    public final long packet, at;
    /** What was read before it. */
    public final transient PacketFile before;

    Refused(String check, long packet, long at, String detail, PacketFile before) {
      super(String.format(Locale.ROOT, "REFUSED %s: packet %,d at byte %,d: %s", check, packet, at, detail));
      this.check = check;
      this.packet = packet;
      this.at = at;
      this.before = before;
    }
  }

  /** How to read. */
  public static final class Options {
    /** The number each stream's first packet must have. */
    public int first = 1, firstForeground = 1;
    /** A zero byte where a header is expected is refused, not skipped. */
    public boolean noIdle;
    /** Track the frames on stream 2 and keep their bytes; false: the packets alone, as Ingress sees them. */
    public boolean frames = true;
    /** Keep every packet's fields. */
    public boolean list;
  }

  /** One packet's fields as read. */
  public static final class Packet {
    public long at, frame;
    public int stream, flags, length, number, acknowledge, dataChecksum, headerCrc;
  }

  public long bytes, packets, idleBytes, empty, flagged, acknowledged;
  /** Stream 2: the frames, their words, and the packets by size: of 32 words, shorter and a frame's last, shorter
   *  and not (the packetizer writes none of those). */
  public long frames, words, full, shortLast, shortInside;
  public long foregroundWords;
  /** By stream: the packets, the first and last number, how often the numbers passed 0xFFFFE. */
  public final long[] onStream = new long[3];
  public final int[] firstNumber = new int[3], lastNumber = new int[3], wraps = new int[3];
  /** The frames' bytes. */
  public byte[] frameBytes = new byte[0];
  public final List<Packet> list = new ArrayList<>();

  private static final int[] TABLE = new int[256];
  static {
    for (int i = 0; i < 256; i++) {
      int c = i;
      for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? (c >>> 1) ^ 0x82F63B78 : c >>> 1;
      TABLE[i] = c;
    }
  }

  /** CRC-32C: polynomial 0x1EDC6F41 reflected (0x82F63B78), initial value and final XOR 0xFFFFFFFF, each byte least
   *  significant bit first. CRC-32C("123456789") = 0xE3069283. A byte at a time by a table of the bit-by-bit steps. */
  public static int crc32c(byte[] b, int off, int len) {
    int c = 0xFFFFFFFF;
    for (int i = off; i < off + len; i++) c = TABLE[(c ^ b[i]) & 0xff] ^ (c >>> 8);
    return ~c;
  }

  private static int u32(byte[] b, int at) {
    return (b[at] & 0xff) | (b[at + 1] & 0xff) << 8 | (b[at + 2] & 0xff) << 16 | (b[at + 3] & 0xff) << 24;
  }

  public static PacketFile read(byte[] f, Options o) {
    PacketFile p = new PacketFile();
    p.bytes = f.length;
    int[] expect = {0, o.firstForeground, o.first};
    ByteArrayOutputStream out = new ByteArrayOutputStream();
    long left = 0;                                               // words of the current frame still to come
    int pos = 0;
    while (true) {
      int z = pos;
      while (z < f.length && f[z] == 0) z++;
      if (z > pos && o.noIdle) throw new Refused("first-byte", p.packets + 1, pos, "a zero byte where a header is expected", p);
      p.idleBytes += z - pos;
      pos = z;
      if (pos == f.length) break;
      long k = p.packets + 1;
      if (f.length - pos < 16) {
        throw new Refused("truncated", k, pos, "the file ends inside a header, after " + (f.length - pos) + " of its 16 bytes", p);
      }
      int headerCrc = u32(f, pos + 12), crc = crc32c(f, pos, 12);
      if (headerCrc != crc) {
        throw new Refused("header-crc", k, pos, String.format("stored 0x%08x, computed 0x%08x", headerCrc, crc), p);
      }
      int stream = f[pos] & 15, flags = (f[pos] >> 4) & 15, length = f[pos + 1] & 0xff;
      int number = (f[pos + 2] & 0xff) | (f[pos + 3] & 0xff) << 8 | (f[pos + 4] & 15) << 16;
      int acknowledge = ((f[pos + 4] >> 4) & 15) | (f[pos + 5] & 0xff) << 4 | (f[pos + 6] & 0xff) << 12;
      int reserved = f[pos + 7] & 0xff;
      if (stream != 1 && stream != 2) throw new Refused("stream", k, pos, "stream " + stream + " (1 or 2)", p);
      if (length > 32) throw new Refused("length", k, pos, "length " + length + " (0 to 32)", p);
      if (reserved != 0) throw new Refused("reserved", k, pos, String.format("reserved bits [63:56] are 0x%02x, not 0", reserved), p);
      if (f.length - pos - 16 < 16 * length) {
        throw new Refused("truncated", k, pos, "the file ends inside the packet's data, after " + (f.length - pos - 16) + " of its "
            + 16 * length + " bytes", p);
      }
      int dataChecksum = u32(f, pos + 8), sum = crc32c(f, pos + 16, 16 * length);
      if (dataChecksum != sum) {
        throw new Refused("data-checksum", k, pos, String.format("stored 0x%08x, computed 0x%08x", dataChecksum, sum), p);
      }
      if (number == 0 || number == 0xFFFFF) {
        throw new Refused("number", k, pos, String.format("number 0x%05x: a packet number is 1 to 0xFFFFE", number), p);
      }
      if (number != expect[stream]) {
        throw new Refused("number", k, pos, String.format("number %d on stream %d, %d expected%s", number, stream, expect[stream],
            length == 0 ? " (a packet of length 0 holds the number of the next packet with data)" : ""), p);
      }
      p.packets++;
      p.onStream[stream]++;
      if (flags != 0) p.flagged++;
      if (acknowledge != 0) p.acknowledged++;
      if (p.onStream[stream] == 1) p.firstNumber[stream] = number;
      p.lastNumber[stream] = number;
      if (length == 0) {
        p.empty++;                                               // it takes no number
      } else {
        expect[stream] = number == 0xFFFFE ? 1 : number + 1;
        if (number == 0xFFFFE) p.wraps[stream]++;
        if (stream == 1) {
          p.foregroundWords += length;
        } else {
          p.words += length;
          if (o.frames) {
            if (left == 0) {                                     // a frame starts: this word is its header word
              long total = 1 + (u32(f, pos + 16 + 12) & 0xffffffffL);
              p.frames++;
              if (length > total) {
                throw new Refused("frame-end", k, pos, String.format(Locale.ROOT, "frame %,d is %,d words and ends inside this packet"
                    + " of %d", p.frames, total, length), p);
              }
              left = total - length;
            } else {
              if (length > left) {
                throw new Refused("frame-end", k, pos, String.format(Locale.ROOT, "frame %,d has %,d words to come and ends inside this"
                    + " packet of %d", p.frames, left, length), p);
              }
              left -= length;
            }
            out.write(f, pos + 16, 16 * length);
            if (length == 32) p.full++;
            else if (left == 0) p.shortLast++;
            else p.shortInside++;
          }
        }
      }
      if (o.list) {
        Packet q = new Packet();
        q.at = pos;
        q.stream = stream;
        q.flags = flags;
        q.length = length;
        q.number = number;
        q.acknowledge = acknowledge;
        q.dataChecksum = dataChecksum;
        q.headerCrc = headerCrc;
        q.frame = stream == 2 && length > 0 && o.frames ? p.frames : 0;
        p.list.add(q);
      }
      pos += 16 + 16 * length;
    }
    if (left > 0) {
      throw new Refused("truncated", p.packets, f.length, String.format(Locale.ROOT, "the file ends inside frame %,d, %,d of its words"
          + " missing", p.frames, left), p);
    }
    p.frameBytes = out.toByteArray();
    return p;
  }
}
