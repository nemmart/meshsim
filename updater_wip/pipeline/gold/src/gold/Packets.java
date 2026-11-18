package gold;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Locale;
import java.util.zip.CRC32C;

/**
 * The link packets of machine-spec chapter 28 (v1.37_wip/spec/host-link.md; v1.37's INTEGRATOR, stage 4: HOST-LINK.md's
 * packets before), the writing side: the packetizer, which wraps a frame file (chapter 29) in link packets as the
 * host's link end sends them first on a clean link whose other end only acknowledges. The reading side (PacketFile,
 * PacketReader) is written from the specification and shares nothing with this, its CRC-32C and its mask included;
 * the choices before chapter 28 are WORKER2's (updater_wip/planning/sessions/WORKER2-NOTES.md).
 *
 * <pre>
 *   a link packet  a header word of 128 bits, then 0 to 32 words of data; every word least significant byte first
 *   byte 0     stream [3:0] (1 foreground, 2 background), flags [7:4]: resend, lost, recovery, restart (28.3)
 *   byte 1     length, the data's 128-bit words
 *   bytes 2-4  number [33:16], 18 bits: one run for the link, both streams, 1 to 0x3FFFE, then 1 again (28.4)
 *   bytes 4-6  ack [51:34], 18 bits: the last number received in order; 0 for none yet
 *   byte 6     hops [55:52] in its high four bits: the ASICs that forwarded the link packet; whoever makes one writes 0
 *   byte 7     fg_stop [56], bg_stop [57]; reserved [63:58], 0
 *   bytes 8-11 data checksum: CRC-32C of the data as it is held, before the mask
 *   bytes 12-15 header CRC: CRC-32C of bytes 0 to 11
 *   the data   on the line XORed with the mask, a CRC-32C register started from header_crc | 0x10000 and fed zeros (28.6)
 * </pre>
 *
 * The packetizer's file: every link packet on stream 2; each frame starts a new one; a frame's link packets hold 32
 * words but its last, which holds what remains; numbers from 1; ack, hops, flags and the stop bits 0; no idle bytes,
 * and none of the link's own link packets (status, recovery, restart): what a capture of the host's line holds of its
 * data link packets, on a link that takes them all at once.
 */
public final class Packets {
  public static final int FOREGROUND = 1, BACKGROUND = 2;
  public static final int MAX_WORDS = 32;
  public static final int FIRST_NUMBER = 1, LAST_NUMBER = 0x3FFFE;

  private Packets() {}

  /** CRC-32C (Castagnoli): the JDK's. */
  public static int crc32c(byte[] b, int off, int len) {
    CRC32C c = new CRC32C();
    c.update(b, off, len);
    return (int) c.getValue();
  }

  /** The number after this one on a stream: after 0x3FFFE comes 1. */
  public static int next(int number) { return number == LAST_NUMBER ? FIRST_NUMBER : number + 1; }

  /** One packet with every field free, for the tests and the vectors: a wrong field can be given right checks. */
  public static final class Packet {
    public int stream = BACKGROUND, flags, number = FIRST_NUMBER, acknowledge, hops, stops, reserved;   // reserved: bits [63:58]
    public byte[] data = new byte[0];
    /** The length field; -1: the data's words. */
    public int length = -1;
    /** The check fields; null: the right value. */
    public Integer dataChecksum, headerCrc;

    public int lengthField() { return length >= 0 ? length : data.length / 16; }

    public int dataChecksumField() { return dataChecksum != null ? dataChecksum : crc32c(data, 0, data.length); }

    /** Header bytes 0 to 11: what the header CRC is over. */
    public byte[] headerLow() {
      byte[] h = new byte[12];
      h[0] = (byte) ((stream & 15) | (flags & 15) << 4);
      h[1] = (byte) lengthField();
      h[2] = (byte) number;
      h[3] = (byte) (number >>> 8);
      h[4] = (byte) (((number >>> 16) & 3) | (acknowledge & 63) << 2);   // number [33:32], acknowledge [39:34]
      h[5] = (byte) (acknowledge >>> 6);                                 // acknowledge [47:40]
      h[6] = (byte) (((acknowledge >>> 14) & 15) | (hops & 15) << 4);     // acknowledge [51:48], hops [55:52]
      h[7] = (byte) ((stops & 3) | (reserved & 63) << 2);               // fg_stop [56], bg_stop [57], reserved [63:58]
      le32(h, 8, dataChecksumField());
      return h;
    }

    public int headerCrcField() { return headerCrc != null ? headerCrc : crc32c(headerLow(), 0, 12); }

    /** The 16 header bytes in link order. */
    public byte[] header() {
      byte[] h = java.util.Arrays.copyOf(headerLow(), 16);
      le32(h, 12, headerCrcField());
      return h;
    }

    /** The whole link packet as it goes on the line: the header, then the data under its mask (28.6). */
    public byte[] bytes() {
      byte[] p = java.util.Arrays.copyOf(header(), 16 + data.length);
      byte[] m = mask(headerCrcField(), data.length);
      for (int i = 0; i < data.length; i++) p[16 + i] = (byte) (data[i] ^ m[i]);
      return p;
    }
  }

  /** The mask of a link packet's data (28.6): `n` bytes from a register started at header_crc | 0x10000, each the
   *  register's low byte after eight shifts of the CRC-32C step with no input. */
  public static byte[] mask(int headerCrc, int n) {
    byte[] m = new byte[n];
    int reg = headerCrc | 0x10000;
    for (int i = 0; i < n; i++) {
      for (int k = 0; k < 8; k++) reg = (reg & 1) != 0 ? (reg >>> 1) ^ 0x82F63B78 : reg >>> 1;
      m[i] = (byte) reg;
    }
    return m;
  }

  static void le32(byte[] b, int at, int v) {
    for (int i = 0; i < 4; i++) b[at + i] = (byte) (v >>> (8 * i));
  }

  /** A packet file built a link packet at a time; it keeps the link's one run of numbers (28.4). */
  public static final class Writer {
    private final ByteArrayOutputStream out = new ByteArrayOutputStream();
    private int next = FIRST_NUMBER;
    public long packets, words, idleBytes;

    /** The number the next data link packet takes, whatever its stream. */
    public Writer first(int number) {
      if (number < FIRST_NUMBER || number > LAST_NUMBER) {
        throw new IllegalArgumentException("a link packet's number is 1 to 0x3FFFE, not " + number);
      }
      next = number;
      return this;
    }

    public int next() { return next; }

    /** Idle: zero bytes between packets. */
    public Writer idle(int bytes) {
      out.writeBytes(new byte[bytes]);
      idleBytes += bytes;
      return this;
    }

    /** A link packet of 1 to 32 words of data: it takes the link's next number. */
    public Writer data(int stream, byte[] b, int off, int words) {
      if (words < 1 || words > MAX_WORDS) throw new IllegalArgumentException("a link packet's data is 1 to 32 words, not " + words);
      Packet p = new Packet();
      p.stream = stream;
      p.number = next;
      p.data = java.util.Arrays.copyOfRange(b, off, off + 16 * words);
      next = Packets.next(next);
      out.writeBytes(p.bytes());
      packets++;
      this.words += words;
      return this;
    }

    /** A link packet of length 0: its number field holds the number the next link packet with data will carry, and
     *  it takes none (28.3). */
    public Writer empty(int stream) {
      Packet p = new Packet();
      p.stream = stream;
      p.number = next;
      out.writeBytes(p.bytes());
      packets++;
      return this;
    }

    /** Any packet as given: the numbering is not touched. */
    public Writer raw(Packet p) {
      out.writeBytes(p.bytes());
      packets++;
      return this;
    }

    public byte[] toArray() { return out.toByteArray(); }
  }

  /** What to write. */
  public static final class Options {
    /** Zero bytes between every two packets; none before the first or after the last. */
    public int idle;
    /** The first packet's number. */
    public int first = FIRST_NUMBER;
    /** For the tests: the packets' sizes in words, taken in turn and round again; a frame's last holds what remains.
     *  The packetizer's rule is {32}. */
    public int[] cut = {MAX_WORDS};
  }

  /** A packet file and what is in it. */
  public static final class Result {
    public byte[] file;
    public long frames, words, packets, idleBytes;
    public int firstNumber, lastNumber;
  }

  static long u32(byte[] b, long at) {
    long v = 0;
    for (int i = 0; i < 4; i++) v |= (b[(int) at + i] & 0xffL) << (8 * i);
    return v;
  }

  /** The frame file cut into packets, in order: each {the byte the packet's data starts at, its words, 1 if it is a
   *  frame's first}. It reads each frame's length (its header word's [127:96], in 128-bit words) and nothing else of
   *  the frames. */
  public static java.util.List<int[]> cuts(byte[] frames, int[] cut) {
    if (frames.length % 16 != 0) {
      throw new IllegalArgumentException("the frame file is " + frames.length + " bytes, not whole 128-bit words");
    }
    java.util.List<int[]> out = new java.util.ArrayList<>();
    long pos = 0;
    int k = 0;
    while (pos < frames.length) {
      long total = 1 + u32(frames, pos + 12);
      if (pos + 16 * total > frames.length) {
        throw new IllegalArgumentException("the frame at byte " + pos + " has " + (total - 1) + " words of data and runs past the"
            + " end of the file");
      }
      for (long done = 0; done < total; ) {
        int n = (int) Math.min(cut[k++ % cut.length], total - done);
        out.add(new int[] {(int) (pos + 16 * done), n, done == 0 ? 1 : 0});
        done += n;
      }
      pos += 16 * total;
    }
    return out;
  }

  /** The frame file as packets on stream 2. */
  public static Result packetize(byte[] frames, Options o) {
    Writer w = new Writer().first(o.first);
    Result r = new Result();
    r.firstNumber = r.lastNumber = o.first;
    for (int[] c : cuts(frames, o.cut)) {
      if (w.packets > 0) w.idle(o.idle);
      r.lastNumber = w.next();
      w.data(BACKGROUND, frames, c[0], c[1]);
      r.frames += c[2];
    }
    r.file = w.toArray();
    r.words = w.words;
    r.packets = w.packets;
    r.idleBytes = w.idleBytes;
    return r;
  }

  /** packets: &lt;frame file&gt; &lt;packet file&gt; [--idle=N] [--first=N] */
  static boolean main(String[] args) throws IOException {
    Options o = new Options();
    for (int i = 2; i < args.length; i++) {
      if (args[i].startsWith("--idle=")) o.idle = Integer.decode(args[i].substring(7));
      else if (args[i].startsWith("--first=")) o.first = Integer.decode(args[i].substring(8));
      else throw new IllegalArgumentException("unknown option " + args[i]);
    }
    if (o.idle < 0) throw new IllegalArgumentException("--idle=" + o.idle);
    byte[] frames = Files.readAllBytes(Path.of(args[0]));
    Result r;
    try {
      r = packetize(frames, o);
    } catch (IllegalArgumentException e) {
      System.out.println("packets: " + e.getMessage());
      return false;
    }
    Files.write(Path.of(args[1]), r.file);
    long over = r.file.length - frames.length;
    System.out.printf(Locale.ROOT, "packets: %,d frames, %,d words in %,d packets on stream 2 (numbers %d to %d); %,d idle bytes%n",
        r.frames, r.words, r.packets, r.firstNumber, r.lastNumber, r.idleBytes);
    System.out.printf(Locale.ROOT, "packet file %,d bytes, the frame file %,d: overhead %,d bytes (%s); SHA-256 %s%n", r.file.length,
        frames.length, over, frames.length == 0 ? "no frames" : String.format(Locale.ROOT, "%.3f%%", 100.0 * over / frames.length),
        PageImage.sha256(r.file));
    return true;
  }
}
