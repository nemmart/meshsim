package gold;

import java.io.ByteArrayOutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * A packet file read and checked: chapter 28's link packets (v1.37_wip/spec/host-link.md; v1.37's INTEGRATOR, stage
 * 4), for the reader. Written from the specification and from WORKER2's NOTES.md sections 2 to 4, not from the
 * packetizer: it shares no code with Packets, and its CRC-32C and its mask are its own, from the definitions.
 *
 * <p>Where a header is expected, zero bytes are idle and skipped; the first byte that is not zero starts a header.
 * Then, in this order, a packet is refused for:
 *
 * <pre>
 *   truncated      the file ends inside a header or inside the packet's data (and, at the end, inside a frame)
 *   header-crc     the CRC-32C of header bytes 0 to 11 is not bytes 12 to 15
 *   stream         the stream is not 1 or 2
 *   length         the length is over 32
 *   reserved       bits [63:58] are not 0 (bits [57:56] are the stop bits, read and not tested)
 *   data-checksum  the CRC-32C of the data, its mask taken off (28.6), is not bytes 8 to 11
 *   restart        a link packet with `restart` (28.9) that is not the restart link packet: stream 1, length 32,
 *                  number 1, hops 0, its data the first 32 primes as bytes sixteen times over; or one inside a
 *                  frame on stream 2. The restart link packet takes no number, its data goes to no queue, and the
 *                  link's numbers start again from 1 after it
 *   number         a link packet with data: not the link's next number, whatever its stream (28.4: one run for the
 *                  link, 1 first, 1 after 0x3FFFE); one of length 0: not the number the next link packet with data
 *                  will carry (it takes none)
 *   frame-end      stream 2: a frame ends before the end of the packet its last word is in
 *   first-byte     only with noIdle: a zero byte where a header is expected
 * </pre>
 *
 * The hops field, bits [55:52], is read and not tested: no value of it is an error (HOST-LINK.md, "The chain, and the
 * hops field"). The number is bits [33:16], 18 bits, and the ack field bits [51:34] (28.3).
 *
 * <p>Frames are tracked on stream 2 only: the first data word after a frame's end is a frame's header word, whose
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
    /** The number the first link packet must have (one run for both streams, 28.4). */
    public int first = 1;
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
    public int stream, flags, length, number, acknowledge, hops, stops, dataChecksum, headerCrc;
  }

  public long bytes, packets, idleBytes, empty, flagged, acknowledged, hopped, restarts;
  /** Stream 2: the frames, their words, and the packets by size: of 32 words, shorter and a frame's last, shorter
   *  and not (the packetizer writes none of those). */
  public long frames, words, full, shortLast, shortInside;
  public long foregroundWords;
  /** By stream: the packets, the first and last number, how often the numbers passed 0x3FFFE. */
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

  /** The i-th prime from 2 (i from 0): the restart link packet's byte i (28.9), found here by trial division. */
  static int prime(int i) {
    int n = 1;
    for (int found = -1; found < i; ) {
      n++;
      boolean is = true;
      for (int d = 2; d * d <= n && is; d++) is = n % d != 0;
      if (is) found++;
    }
    return n;
  }

  /** CRC-32C: polynomial 0x1EDC6F41 reflected (0x82F63B78), initial value and final XOR 0xFFFFFFFF, each byte least
   *  significant bit first. CRC-32C("123456789") = 0xE3069283. A byte at a time by a table of the bit-by-bit steps. */
  public static int crc32c(byte[] b, int off, int len) {
    int c = 0xFFFFFFFF;
    for (int i = off; i < off + len; i++) c = TABLE[(c ^ b[i]) & 0xff] ^ (c >>> 8);
    return ~c;
  }

  /** The data's mask (28.6): a CRC-32C register from header_crc | 0x10000, fed zeros, its low byte after each eight
   *  steps. Taken off `n` bytes of `b` at `off`, into a new array. */
  static byte[] unmask(byte[] b, int off, int n, int headerCrc) {
    byte[] d = new byte[n];
    int reg = headerCrc | 0x10000;
    for (int i = 0; i < n; i++) {
      for (int k = 0; k < 8; k++) reg = (reg & 1) != 0 ? (reg >>> 1) ^ 0x82F63B78 : reg >>> 1;
      d[i] = (byte) (b[off + i] ^ reg);
    }
    return d;
  }

  private static int u32(byte[] b, int at) {
    return (b[at] & 0xff) | (b[at + 1] & 0xff) << 8 | (b[at + 2] & 0xff) << 16 | (b[at + 3] & 0xff) << 24;
  }

  public static PacketFile read(byte[] f, Options o) {
    PacketFile p = new PacketFile();
    p.bytes = f.length;
    int expect = o.first;
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
      // the header as a 64-bit word, bytes 0 to 7, least significant first; then each field by its bits
      long low = 0;
      for (int i = 7; i >= 0; i--) low = low << 8 | (f[pos + i] & 0xffL);
      int number = (int) ((low >>> 16) & 0x3FFFF);              // [33:16]
      int acknowledge = (int) ((low >>> 34) & 0x3FFFF);          // [51:34]
      int hops = (int) ((low >>> 52) & 15);                      // [55:52], not tested
      int stops = f[pos + 7] & 3, reserved = (f[pos + 7] & 0xff) >> 2;   // [57:56] fg_stop, bg_stop; [63:58]
      if (stream != 1 && stream != 2) throw new Refused("stream", k, pos, "stream " + stream + " (1 or 2)", p);
      if (length > 32) throw new Refused("length", k, pos, "length " + length + " (0 to 32)", p);
      if (reserved != 0) throw new Refused("reserved", k, pos, String.format("reserved bits [63:58] are 0x%02x, not 0", reserved), p);
      if (f.length - pos - 16 < 16 * length) {
        throw new Refused("truncated", k, pos, "the file ends inside the packet's data, after " + (f.length - pos - 16) + " of its "
            + 16 * length + " bytes", p);
      }
      byte[] data = unmask(f, pos + 16, 16 * length, headerCrc);
      int dataChecksum = u32(f, pos + 8), sum = crc32c(data, 0, data.length);
      if (dataChecksum != sum) {
        throw new Refused("data-checksum", k, pos, String.format("stored 0x%08x, computed 0x%08x", dataChecksum, sum), p);
      }
      if ((flags & 8) != 0) {                                    // restart (28.9)
        boolean pattern = stream == 1 && length == 32 && number == 1 && hops == 0;
        for (int i = 0; pattern && i < data.length; i++) pattern = (data[i] & 0xff) == prime(i % 32);
        if (!pattern) throw new Refused("restart", k, pos, "restart is set and this is not the restart link packet", p);
        if (left != 0) throw new Refused("restart", k, pos, String.format(Locale.ROOT, "a restart link packet inside frame %,d", p.frames), p);
        p.packets++;
        p.onStream[1]++;
        p.flagged++;
        p.restarts++;
        if (acknowledge != 0) p.acknowledged++;
        expect = 1;                                              // the numbers start again
      } else {
        if (number == 0 || number == 0x3FFFF) {
          throw new Refused("number", k, pos, String.format("number 0x%05x: a packet number is 1 to 0x3FFFE", number), p);
        }
        if (number != expect) {
          throw new Refused("number", k, pos, String.format("number %d on stream %d, %d expected%s", number, stream, expect,
              length == 0 ? " (a link packet of length 0 holds the number of the next link packet with data)" : ""), p);
        }
        p.packets++;
        p.onStream[stream]++;
        if (flags != 0) p.flagged++;
        if (acknowledge != 0) p.acknowledged++;
        if (hops != 0) p.hopped++;
        if (p.onStream[stream] == 1) p.firstNumber[stream] = number;
        p.lastNumber[stream] = number;
        if (length == 0) {
          p.empty++;                                               // it takes no number
        } else {
          expect = number == 0x3FFFE ? 1 : number + 1;
          if (number == 0x3FFFE) p.wraps[stream]++;
          if (stream == 1) {
            p.foregroundWords += length;
          } else {
            p.words += length;
            if (o.frames) {
              if (left == 0) {                                     // a frame starts: this word is its header word
                long total = 1 + (u32(data, 12) & 0xffffffffL);
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
              out.write(data, 0, 16 * length);
              if (length == 32) p.full++;
              else if (left == 0) p.shortLast++;
              else p.shortInside++;
            }
          }
        }
      }                                                          // the restart link packet's, or an ordinary one's
      if (o.list) {
        Packet q = new Packet();
        q.at = pos;
        q.stream = stream;
        q.flags = flags;
        q.length = length;
        q.number = number;
        q.acknowledge = acknowledge;
        q.hops = hops;
        q.stops = stops;
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
