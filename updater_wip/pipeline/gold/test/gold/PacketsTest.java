package gold;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.TreeMap;
import java.util.TreeSet;

/**
 * Tests of the link's packets (WORKER2): the packetizer (Packets) against the reader (PacketFile), which share no code.
 * <ol>
 *   <li>CRC-32C: the JDK's and the reader's own on the published values and on random strings;</li>
 *   <li>the round trip on made-up frame files of every length around the cuts, and the packetizer's own rule;</li>
 *   <li>damage is caught, the reader naming the check: each field of a packet, with the checks left and with the checks
 *       made right again; packets dropped, repeated and swapped; a frame ending inside a packet; the file cut short at
 *       every byte; every single bit of the small files changed in turn;</li>
 *   <li>the wrap of the packet number, by --first and by a file long enough to pass 0x3FFFE;</li>
 *   <li>idle zeros in many counts, and refused with noIdle;</li>
 *   <li>packets of length 0, which take no number; the two streams' own numbers; other legal cuts;</li>
 *   <li>the vectors: every "expect" of VECTORS.txt got from the reader; HOST-LINK.md's three example headers from the
 *       packetizer, byte for byte, and taken by the reader.</li>
 * </ol>
 * Usage: PacketsTest [&lt;frame file&gt;]: the field damage is done on that file's packets (a real guest's), or on
 * small.frames'.
 */
public final class PacketsTest {
  static int failures;
  static long checks;

  public static void main(String[] args) throws Exception {
    crc();
    roundTrip();
    byte[] real = args.length > 0 ? Files.readAllBytes(Path.of(args[0])) : PacketVectors.smallFrames();
    fields(real);
    order();
    frameEnd();
    truncation();
    everyBit();
    wrap();
    idle();
    empty();
    streams();
    restarts();
    cuts();
    vectors();
    System.out.println(failures == 0 ? "packets test: ok (" + checks + " checks)" : "packets test: " + failures + " FAILED");
    System.exit(failures == 0 ? 0 : 1);
  }

  static void expect(boolean ok, String what) {
    checks++;
    if (!ok) {
      failures++;
      if (failures <= 40) System.out.println("FAIL " + what);
    }
  }

  static PacketFile.Options options(int first) {
    PacketFile.Options o = new PacketFile.Options();
    o.first = first;
    return o;
  }

  /** The reader's refusal of a file, or null if it takes it. */
  static PacketFile.Refused refused(byte[] file, PacketFile.Options o) {
    try {
      PacketFile.read(file, o);
      return null;
    } catch (PacketFile.Refused r) {
      return r;
    }
  }

  static void refusedAs(byte[] file, PacketFile.Options o, String check, long packet, long at, String what) {
    PacketFile.Refused r = refused(file, o);
    expect(r != null && r.check.equals(check) && (packet < 0 || r.packet == packet) && (at < 0 || r.at == at), what + ": expected "
        + check + (packet < 0 ? "" : " at packet " + packet + ", byte " + at) + ", got " + (r == null ? "no refusal" : r.getMessage()));
  }

  /** The file read back: taken, and its frames these. */
  static PacketFile good(byte[] file, PacketFile.Options o, byte[] frames, String what) {
    try {
      PacketFile p = PacketFile.read(file, o);
      expect(Arrays.equals(p.frameBytes, frames), what + ": the frames read back differ");
      return p;
    } catch (PacketFile.Refused r) {
      expect(false, what + ": " + r.getMessage());
      return null;
    }
  }

  /** A made-up frame file: frames with these many words of data. */
  static byte[] frames(Random rnd, int... dataWords) {
    java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
    for (int w : dataWords) {
      byte[] d = new byte[16 * w];
      rnd.nextBytes(d);
      Frames.frame(out, 1 + rnd.nextInt(9), Frames.NONE, d);
    }
    return out.toByteArray();
  }

  // ---- 1. CRC-32C ----

  static void crc() {
    byte[] nine = "123456789".getBytes(java.nio.charset.StandardCharsets.US_ASCII), ff = new byte[32], up = new byte[32];
    Arrays.fill(ff, (byte) 0xff);
    for (int i = 0; i < 32; i++) up[i] = (byte) i;
    Object[][] known = {{nine, 0xE3069283}, {new byte[0], 0}, {new byte[16], 0x42709AEA}, {new byte[32], 0x8A9136AA},
        {ff, 0x62A8AB43}, {up, 0x46DD794E}};
    for (Object[] k : known) {
      byte[] b = (byte[]) k[0];
      int want = (Integer) k[1];
      expect(Packets.crc32c(b, 0, b.length) == want, "the JDK's CRC-32C on a published value");
      expect(PacketFile.crc32c(b, 0, b.length) == want, "the reader's CRC-32C on a published value");
    }
    Random rnd = new Random(1);
    for (int n = 0; n < 700; n++) {
      byte[] b = new byte[n + 20];
      rnd.nextBytes(b);
      int c = Packets.crc32c(b, 7, n);
      expect(c == PacketFile.crc32c(b, 7, n), "the two CRC-32Cs differ on " + n + " bytes");
      byte[] with = Arrays.copyOfRange(b, 7, 7 + n + 4);
      Packets.le32(with, n, c);
      expect(PacketFile.crc32c(with, 0, n + 4) == 0x48674BC7, "the CRC-32C of a string and its own CRC");
    }
    System.out.println("CRC-32C: the JDK's and the reader's agree on the published values and on 700 random strings");
  }

  // ---- 2. the round trip ----

  static void roundTrip() {
    Random rnd = new Random(2);
    int files = 0;
    List<byte[]> made = new ArrayList<>();
    made.add(new byte[0]);
    for (int w = 0; w <= 70; w++) made.add(frames(rnd, w));
    for (int trial = 0; trial < 60; trial++) {
      int[] lens = new int[1 + rnd.nextInt(40)];
      for (int i = 0; i < lens.length; i++) lens[i] = rnd.nextInt(8) == 0 ? 2000 + rnd.nextInt(3000) : rnd.nextInt(100);
      made.add(frames(rnd, lens));
    }
    for (byte[] f : made) {
      Packets.Result r = Packets.packetize(f, new Packets.Options());
      PacketFile p = good(r.file, options(1), f, "round trip");
      if (p == null) continue;
      long want = 0, frames = 0;
      for (int[] c : Packets.cuts(f, new int[] {32})) frames += c[2];
      for (long pos = 0; pos < f.length; ) {
        long total = 1 + Packets.u32(f, pos + 12);
        want += (total + 31) / 32;
        pos += 16 * total;
      }
      expect(p.packets == want && r.packets == want, "the packets: a frame of W words is ceil(W/32) packets");
      expect(r.file.length == f.length + 16 * want, "the packet file is the frames and one header word a packet");
      expect(p.frames == frames && p.words == f.length / 16, "the frames and words found");
      expect(p.shortInside == 0 && p.full + p.shortLast == p.packets, "every packet holds 32 words or is a frame's last");
      expect(p.onStream[2] == p.packets && p.onStream[1] == 0 && p.empty == 0 && p.idleBytes == 0 && p.flagged == 0
          && p.acknowledged == 0, "all on stream 2, no idle bytes, no flags, no acknowledge");
      expect(want == 0 || (p.firstNumber[2] == 1 && p.lastNumber[2] == want), "numbers from 1");
      files++;
    }
    // the packetizer refuses what is not a frame file
    for (byte[] bad : new byte[][] {new byte[17], Arrays.copyOf(frames(rnd, 5), 80), Arrays.copyOf(frames(rnd, 40, 3), 16 * 43)}) {
      boolean threw = false;
      try {
        Packets.packetize(bad, new Packets.Options());
      } catch (IllegalArgumentException e) {
        threw = true;
      }
      expect(threw, "the packetizer takes a frame file that is cut short");
    }
    System.out.println("round trip: " + files + " made-up frame files (an empty one; one frame of 1 to 71 words; 60 of many frames),"
        + " the frames byte-identical, the packetizer's rule kept");
  }

  // ---- 3. damage ----

  /** Where each packet of a file with no idle bytes starts. */
  static List<Integer> starts(byte[] file) {
    List<Integer> s = new ArrayList<>();
    for (int pos = 0; pos < file.length; pos += 16 + 16 * (file[pos + 1] & 0xff)) s.add(pos);
    return s;
  }

  /** The packet at a byte, as the writing side's fields, to change one and write it back with its checks right. */
  static Packets.Packet at(byte[] file, int a) {
    Packets.Packet p = new Packets.Packet();
    p.stream = file[a] & 15;
    p.flags = (file[a] >> 4) & 15;
    int len = file[a + 1] & 0xff;
    p.number = (file[a + 2] & 0xff) | (file[a + 3] & 0xff) << 8 | (file[a + 4] & 3) << 16;          // [33:16]
    p.acknowledge = ((file[a + 4] >> 2) & 63) | (file[a + 5] & 0xff) << 6 | (file[a + 6] & 15) << 14;   // [51:34]
    p.hops = (file[a + 6] >> 4) & 15;                                                               // [55:52]
    p.stops = file[a + 7] & 3;                                                                      // [57:56]
    p.reserved = (file[a + 7] & 0xff) >> 2;                                                         // [63:58]
    int crc = (file[a + 12] & 0xff) | (file[a + 13] & 0xff) << 8 | (file[a + 14] & 0xff) << 16 | (file[a + 15] & 0xff) << 24;
    p.data = PacketFile.unmask(file, a + 16, 16 * len, crc);                                        // the data as held (28.6)
    return p;
  }

  interface Change { void to(Packets.Packet p); }

  /** The file with packet k (from 1) changed and its checks made right again; its data keeps its size. */
  static byte[] sealed(byte[] file, List<Integer> starts, int k, Change c) {
    byte[] b = file.clone();
    Packets.Packet p = at(b, starts.get(k - 1));
    c.to(p);
    byte[] all = p.bytes();                                                    // the header, and the data under its new mask
    System.arraycopy(all, 0, b, starts.get(k - 1), all.length);
    return b;
  }

  static byte[] flipped(byte[] file, int at, int mask) {
    byte[] b = file.clone();
    b[at] ^= (byte) mask;
    return b;
  }

  static void fields(byte[] frames) {
    byte[] file = Packets.packetize(frames, new Packets.Options()).file;
    List<Integer> s = starts(file);
    PacketFile.Options o = options(1);
    good(file, o, frames, "the file before any damage");
    int n = s.size();
    TreeSet<Integer> ks = new TreeSet<>(List.of(1, 2, n / 3 + 1, n / 2 + 1, n - 1, n));
    ks.removeIf(k -> k < 1 || k > n);
    long before = checks, taken = 0;
    for (int k : ks) {
      int a = s.get(k - 1), len = file[a + 1] & 0xff;
      String w = "packet " + k + " of " + n + ": ";
      // one bit, the checks left as they were
      int[][] header = {{0, 0x01}, {0, 0x10}, {0, 0x80}, {1, 0x01}, {1, 0x20}, {2, 0x01}, {3, 0x40}, {4, 0x02}, {4, 0x04}, {4, 0x80},
          {5, 0x02}, {6, 0x08}, {6, 0x10}, {6, 0x80}, {7, 0x01}, {7, 0x80}, {8, 0x01}, {9, 0x10}, {10, 0x04}, {11, 0x80}, {12, 0x01}, {13, 0x08}, {14, 0x20}, {15, 0x80}};
      for (int[] h : header) {
        refusedAs(flipped(file, a + h[0], h[1]), o, "header-crc", k, a, w + "header byte " + h[0] + " xor " + h[1]);
      }
      for (int d : new int[] {0, 1, 15, 16 * len / 2, 16 * len - 1}) {
        refusedAs(flipped(file, a + 16 + d, 0x01), o, "data-checksum", k, a, w + "data byte " + d);
        refusedAs(flipped(file, a + 16 + d, 0x80), o, "data-checksum", k, a, w + "data byte " + d);
      }
      // the first byte made zero: an idle byte; the header is then taken from the length byte, and the 16 bytes
      // from there fail the header CRC, which is tested before anything in them is believed
      refusedAs(flipped(file, a, 0x02), o, "header-crc", k, a + 1, w + "the first byte made zero");
      refusedAs(flipped(file, a, 0x02), noIdle(), "first-byte", k, a, w + "the first byte made zero, with noIdle");
      // one field wrong, both checks right
      refusedAs(sealed(file, s, k, p -> { p.stream = 0; p.flags = 1; }), o, "stream", k, a, w + "stream 0 with a flag");
      refusedAs(sealed(file, s, k, p -> p.stream = 3), o, "stream", k, a, w + "stream 3");
      refusedAs(sealed(file, s, k, p -> p.stream = 15), o, "stream", k, a, w + "stream 15");
      refusedAs(sealed(file, s, k, p -> p.length = 33), o, "length", k, a, w + "length 33");
      refusedAs(sealed(file, s, k, p -> p.length = 255), o, "length", k, a, w + "length 255");
      refusedAs(sealed(file, s, k, p -> p.reserved = 1), o, "reserved", k, a, w + "reserved bit 58");
      refusedAs(sealed(file, s, k, p -> p.reserved = 0x20), o, "reserved", k, a, w + "reserved bit 63");
      expect(refused(sealed(file, s, k, p -> p.stops = 3), o) == null, w + "the stop bits set: not checked");
      refusedAs(sealed(file, s, k, p -> p.dataChecksum = p.dataChecksumField() ^ 1), o, "data-checksum", k, a, w + "the data checksum");
      refusedAs(sealed(file, s, k, p -> p.number = 0), o, "number", k, a, w + "number 0");
      refusedAs(sealed(file, s, k, p -> p.number = 0x3FFFF), o, "number", k, a, w + "number 0x3FFFF");
      refusedAs(sealed(file, s, k, p -> p.number++), o, "number", k, a, w + "the number one ahead");
      if (k > 1) refusedAs(sealed(file, s, k, p -> p.number--), o, "number", k, a, w + "the last number again");
      refusedAs(sealed(file, s, k, p -> p.number ^= 0x20000), o, "number", k, a, w + "the number's top bit");
      // what is not checked: resend, lost and recovery, the acknowledge field, the hops field; restart is (28.9)
      refusedAs(sealed(file, s, k, p -> p.flags = Packets.RESTART), o, "restart", k, a, w + "restart set on a data link packet");
      byte[] fl = sealed(file, s, k, p -> { p.flags = 7; p.acknowledge = 0x3FFFF; p.hops = 15; });
      PacketFile p = good(fl, o, frames, w + "flags, acknowledge and hops set");
      expect(p != null && p.flagged == 1 && p.acknowledged == 1 && p.hopped == 1, w + "flags, acknowledge and hops counted");
      for (int h = 1; h < 16; h++) {
        final int hops = h;
        good(sealed(file, s, k, q -> q.hops = hops), o, frames, w + "hops " + h);
      }
      taken += 17;
    }
    long cases = checks - before - taken;
    // a data bit changed with both checks made right again is a good packet file of other frames: no check of the
    // link's can tell, and only --compare does
    byte[] changed = sealed(file, s, n, p -> p.data[0] ^= 4);
    expect(refused(changed, o) == null && !Arrays.equals(PacketFile.read(changed, o).frameBytes, frames), "a changed data bit with both checks right"
        + " is a good file of other frames");
    System.out.println("damage: " + cases + " changes to " + ks.size() + " packets of a file of " + n + " (" + file.length + " bytes): each"
        + " refused by the check it should be, at the right packet and byte; with every flag, an acknowledge number and hops set, taken");
  }

  static PacketFile.Options noIdle() {
    PacketFile.Options o = new PacketFile.Options();
    o.noIdle = true;
    return o;
  }

  /** The file without packet k, with packet k twice, with packets k and k+1 swapped. */
  static void order() {
    byte[] frames = PacketVectors.smallFrames();
    byte[] file = Packets.packetize(frames, new Packets.Options()).file;
    List<Integer> s = starts(file);
    s.add(file.length);
    int n = s.size() - 1, cases = 0;
    for (int k = 1; k <= n; k++) {
      int a = s.get(k - 1), b = s.get(k);
      java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
      out.write(file, 0, a);
      out.write(file, b, file.length - b);
      if (k < n) {
        refusedAs(out.toByteArray(), options(1), "number", k, a, "packet " + k + " dropped");
      } else {
        // the last packet dropped: frame 9 (65 words) loses its last word
        refusedAs(out.toByteArray(), options(1), "truncated", n - 1, a, "the last packet dropped");
      }
      out.reset();
      out.write(file, 0, b);
      out.write(file, a, file.length - a);
      refusedAs(out.toByteArray(), options(1), "number", k + 1, b, "packet " + k + " sent twice");
      cases += 2;
      if (k < n) {
        int c = s.get(k + 1);
        out.reset();
        out.write(file, 0, a);
        out.write(file, b, c - b);
        out.write(file, a, b - a);
        out.write(file, c, file.length - c);
        refusedAs(out.toByteArray(), options(1), "number", k, a, "packets " + k + " and " + (k + 1) + " swapped");
        cases++;
      }
    }
    System.out.println("order: " + cases + " files with a packet dropped, sent twice or swapped with the next: each refused");
  }

  static void frameEnd() {
    Random rnd = new Random(3);
    // two frames of 3 and 2 words in one packet of 5; of 3 and 30 in one of 32 and one of 1
    byte[] f = frames(rnd, 2, 1);
    Packets.Writer w = new Packets.Writer();
    w.data(2, f, 0, 5);
    refusedAs(w.toArray(), options(1), "frame-end", 1, 0, "two frames in one packet");
    f = frames(rnd, 2, 29);
    w = new Packets.Writer();
    w.data(2, f, 0, 32).data(2, f, 16 * 32, 1);
    refusedAs(w.toArray(), options(1), "frame-end", 1, 0, "a frame ending inside its first packet");
    // a frame of 40 words: 32, then a packet of 9 that takes the next frame's header word
    f = frames(rnd, 39, 4);
    w = new Packets.Writer();
    w.data(2, f, 0, 32).data(2, f, 16 * 32, 9).data(2, f, 16 * 41, 4);
    refusedAs(w.toArray(), options(1), "frame-end", 2, 16 * 33, "a frame ending inside its second packet");
    // the same packets are fine when the frames are not looked at
    PacketFile.Options po = options(1);
    po.frames = false;
    expect(refused(w.toArray(), po) == null, "packets only: the frames are not looked at");
    // a frame whose packets stop early
    w = new Packets.Writer();
    w.data(2, f, 0, 32);
    refusedAs(w.toArray(), options(1), "truncated", 1, 16 * 33, "the file ends inside a frame");
    System.out.println("frame-end: a frame ending inside a packet is refused; a file ending inside a frame is truncated");
  }

  /** small.packets cut short at every byte: taken only when the cut is at a frame's end. */
  static void truncation() {
    byte[] frames = PacketVectors.smallFrames();
    byte[] file = Packets.packetize(frames, new Packets.Options()).file;
    TreeMap<Integer, Integer> ends = new TreeMap<>();                // packet-file byte where a frame ends -> frame-file byte
    int at = 0;
    List<int[]> cuts = Packets.cuts(frames, new int[] {32});
    ends.put(0, 0);
    for (int i = 0; i < cuts.size(); i++) {
      at += 16 + 16 * cuts.get(i)[1];
      if (i + 1 == cuts.size() || cuts.get(i + 1)[2] == 1) ends.put(at, cuts.get(i)[0] + 16 * cuts.get(i)[1]);
    }
    int taken = 0;
    for (int n = 0; n < file.length; n++) {
      byte[] cut = Arrays.copyOf(file, n);
      if (ends.containsKey(n)) {
        good(cut, options(1), Arrays.copyOf(frames, ends.get(n)), "cut at a frame's end, byte " + n);
        taken++;
      } else {
        refusedAs(cut, options(1), "truncated", -1, -1, "cut at byte " + n);
      }
    }
    System.out.println("truncation: small.packets cut short at each of its " + file.length + " bytes: refused as truncated, but at the "
        + taken + " frame ends, where it is a good file with fewer frames");
  }

  /** Every single bit of the small files changed in turn: every one refused. */
  static void everyBit() {
    Map<String, byte[]> all = PacketVectors.all();
    long files = 0;
    TreeMap<String, Long> by = new TreeMap<>();
    for (Map.Entry<String, byte[]> e : all.entrySet()) {
      if (!e.getKey().endsWith(".packets")) continue;
      PacketFile.Options o = options(e.getKey().equals("wrap.packets") ? 0x3FFFA : 1);
      byte[] f = e.getValue();
      for (int i = 0; i < f.length; i++) {
        for (int bit = 0; bit < 8; bit++) {
          f[i] ^= (byte) (1 << bit);
          PacketFile.Refused r = refused(f, o);
          f[i] ^= (byte) (1 << bit);
          expect(r != null, e.getKey() + " with bit " + bit + " of byte " + i + " changed is taken");
          if (r != null) by.merge(r.check, 1L, Long::sum);
          files++;
        }
      }
    }
    System.out.println("every bit: each single bit of the five small packet files changed in turn, " + files + " files, every one"
        + " refused: " + by);
  }

  // ---- 4. the wrap ----

  static void wrap() {
    Random rnd = new Random(4);
    byte[] f = frames(rnd, 100, 0, 31, 32, 200);
    for (int first : new int[] {0x3FFFA, 0x3FFFE, 0x3FFFD, 0x3FFF0, 0xFFFF, 0x10000, 2, 1}) {
      Packets.Options o = new Packets.Options();
      o.first = first;
      Packets.Result r = Packets.packetize(f, o);
      PacketFile p = good(r.file, options(first), f, "numbers from " + first);
      long over = first + r.packets - 1 - 0x3FFFE;
      expect(p != null && p.firstNumber[2] == first && p.lastNumber[2] == (over > 0 ? over : first + r.packets - 1)
          && p.wraps[2] == (over > 0 || first + r.packets - 1 == 0x3FFFE ? 1 : 0), "the numbers from " + first);
      if (first != 1) refusedAs(r.file, options(1), "number", 1, 0, "a file numbered from " + first + " read as from 1");
      if (first != 2) refusedAs(r.file, options(2), "number", 1, 0, "a file numbered from " + first + " read as from 2");
    }
    for (int bad : new int[] {0, 0x3FFFF, 0xFFFFE, -1}) {
      boolean threw = false;
      try {
        Packets.Options o = new Packets.Options();
        o.first = bad;
        Packets.packetize(f, o);
      } catch (IllegalArgumentException e) {
        threw = true;
      }
      expect(threw, "the packetizer numbers from " + bad);
    }
    // long enough to pass 0x3FFFE from 1: 262,200 frames of one word, a packet each
    int n = 262_200;
    byte[] many = new byte[16 * n];
    for (int i = 0; i < n; i++) {
      many[16 * i] = 0;
      many[16 * i + 8] = (byte) (1 + i % 9);                      // the type; the length is 0
      many[16 * i + 1] = (byte) i;                                // reserved bits, made different frame to frame: the
      many[16 * i + 2] = (byte) (i >> 8);                         // packets alone are tested here, not the frames
      many[16 * i + 3] = (byte) (i >> 16);
    }
    Packets.Result r = Packets.packetize(many, new Packets.Options());
    PacketFile p = good(r.file, options(1), many, "the long file");
    expect(r.packets == n && p != null && p.packets == n && p.firstNumber[2] == 1 && p.lastNumber[2] == n - 0x3FFFE && p.wraps[2] == 1,
        "the long file: " + n + " packets, the last numbered " + (n - 0x3FFFE));
    // the packet after 0x3FFFE is numbered 1, at byte 32 * 0x3FFFE: numbered 0x3FFFF instead, it is refused
    int a = 32 * 0x3FFFE;
    expect(at(r.file, a).number == 1 && at(r.file, a - 32).number == 0x3FFFE, "the long file: 1 after 0x3FFFE");
    expect(at(r.file, 32 * 0xFFFF).number == 0x10000, "the long file: 0x10000 after 0xFFFF");
    List<Integer> one = List.of(a);
    refusedAs(sealed(r.file, one, 1, q -> q.number = 0x3FFFF), options(1), "number", 0x3FFFF, a, "0x3FFFF after 0x3FFFE");
    refusedAs(sealed(r.file, one, 1, q -> q.number = 0), options(1), "number", 0x3FFFF, a, "0 after 0x3FFFE");
    refusedAs(sealed(r.file, one, 1, q -> q.number = 2), options(1), "number", 0x3FFFF, a, "2 after 0x3FFFE");
    System.out.println("wrap: files numbered from 0x3FFFA, 0x3FFFE, 0x3FFFD, 0x3FFF0, 0xFFFF, 0x10000, 2 and 1 read back with the same first"
        + " number and refused with another; a file of " + n + " packets numbered from 1 passes 0x3FFFE to 1 and ends at " + (n - 0x3FFFE));
  }

  // ---- 5. idle zeros ----

  static void idle() {
    Random rnd = new Random(5);
    byte[] f = frames(rnd, 0, 40, 31, 100, 1);
    int[] counts = {1, 2, 15, 16, 17, 31, 32, 33, 1023, 1024, 1025, 5000};
    for (int n : counts) {
      Packets.Options o = new Packets.Options();
      o.idle = n;
      Packets.Result r = Packets.packetize(f, o);
      PacketFile p = good(r.file, options(1), f, n + " idle bytes between packets");
      expect(p != null && p.idleBytes == n * (r.packets - 1) && r.idleBytes == p.idleBytes && r.file.length == f.length + 16 * r.packets
          + p.idleBytes, n + " idle bytes between packets: counted");
      refusedAs(r.file, noIdle(), "first-byte", 2, 32, n + " idle bytes between packets, with noIdle");
    }
    // a different count before every packet, before the first and after the last
    int files = 0;
    for (int trial = 0; trial < 200; trial++) {
      Packets.Writer w = new Packets.Writer();
      long idle = 0;
      for (int[] c : Packets.cuts(f, new int[] {32})) {
        int n = rnd.nextInt(4) == 0 ? 0 : rnd.nextInt(3) == 0 ? rnd.nextInt(2000) : rnd.nextInt(40);
        w.idle(n).data(2, f, c[0], c[1]);
        idle += n;
      }
      int tail = rnd.nextInt(50);
      w.idle(tail);
      PacketFile p = good(w.toArray(), options(1), f, "idle bytes of any count");
      expect(p != null && p.idleBytes == idle + tail, "idle bytes of any count: counted");
      files++;
    }
    expect(refused(new byte[1000], options(1)) == null, "a file of zeros alone is a file of no packets");
    expect(refused(Packets.packetize(f, new Packets.Options()).file, noIdle()) == null, "noIdle takes a file with no idle bytes");
    System.out.println("idle: " + counts.length + " counts of zero bytes between packets (1 to 5000, with ones that are not whole words),"
        + " and " + files + " files with a different count before every packet, before the first and after the last; noIdle"
        + " refuses them");
  }

  // ---- 6. packets of length 0, the two streams, other cuts ----

  static void empty() {
    Random rnd = new Random(6);
    byte[] f = frames(rnd, 3, 40, 0);
    List<int[]> cuts = Packets.cuts(f, new int[] {32});
    // empties everywhere: before the first packet, between, several together, inside a frame, at the end
    Packets.Writer w = new Packets.Writer();
    w.empty(2).empty(2);
    for (int[] c : cuts) w.data(2, f, c[0], c[1]).empty(2).idle(3).empty(2);
    PacketFile p = good(w.toArray(), options(1), f, "packets of length 0 everywhere");
    expect(p != null && p.empty == 2 + 2 * cuts.size() && p.packets == p.empty + cuts.size() && p.lastNumber[2] == cuts.size() + 1,
        "packets of length 0 take no number: the last holds the number after the last data packet's");
    // an empty holds the next data packet's number, not the last one's, and not the one after
    for (int wrong : new int[] {-1, 1}) {
      w = new Packets.Writer();
      w.data(2, f, cuts.get(0)[0], cuts.get(0)[1]);
      Packets.Packet e = new Packets.Packet();
      e.number = 2 + wrong;
      w.raw(e);
      w.data(2, f, cuts.get(1)[0], cuts.get(1)[1]);
      refusedAs(w.toArray(), options(1), "number", 2, 16 * (1 + cuts.get(0)[1]), "a packet of length 0 holding " + (2 + wrong));
    }
    // the data packet after an empty has the empty's number: a file where the empty took a number is refused
    w = new Packets.Writer();
    w.data(2, f, cuts.get(0)[0], cuts.get(0)[1]).empty(2);
    w.first(3).data(2, f, cuts.get(1)[0], cuts.get(1)[1]);
    refusedAs(w.toArray(), options(1), "number", 3, -1, "a data packet numbered as if the packet of length 0 had taken a number");
    // an empty with a data checksum, and one at the wrap
    Packets.Packet e = new Packets.Packet();
    e.dataChecksum = 1;
    refusedAs(e.bytes(), options(1), "data-checksum", 1, 0, "a packet of length 0 with a data checksum of 1");
    w = new Packets.Writer().first(0x3FFFE);
    w.empty(2).data(2, f, cuts.get(0)[0], cuts.get(0)[1]).empty(2);
    for (int i = 1; i < cuts.size(); i++) w.data(2, f, cuts.get(i)[0], cuts.get(i)[1]);
    p = good(w.toArray(), options(0x3FFFE), f, "packets of length 0 around the wrap");
    expect(p != null && at(w.toArray(), 16 + 16 * (1 + cuts.get(0)[1])).number == 1, "after 0x3FFFE a packet of"
        + " length 0 holds 1");
    System.out.println("length 0: taken anywhere, taking no number; refused when it holds the last number or the one after the next");
  }

  static void restarts() {
    Random rnd = new Random(11);
    byte[] f = frames(rnd, 40, 3, 70);
    List<int[]> cuts = Packets.cuts(f, new int[] {32});
    // a restart first, and one between two frames: the numbers start again from 1 after each
    Packets.Writer w = new Packets.Writer().first(77).restart(Packets.LOST);
    int middle = -1;
    for (int i = 0; i < cuts.size(); i++) {
      if (cuts.get(i)[2] == 1 && middle < 0 && i >= cuts.size() / 2) { middle = i; w.restart(Packets.RECOVERY); }
      w.data(2, f, cuts.get(i)[0], cuts.get(i)[1]);
    }
    byte[] file = w.toArray();
    PacketFile p = good(file, options(5), f, "restart link packets before and between frames");
    expect(p != null && p.restarts == 2 && p.foregroundWords == 0 && p.onStream[1] == 2, "two restart link packets: counted, their words"
        + " nowhere");
    // one inside a frame is refused, and the numbers after one go on from 1, not from where they were
    List<Integer> s = starts(file);
    int inside = -1;
    for (int i = 1; i < cuts.size() && inside < 0; i++) if (cuts.get(i)[2] == 0) inside = i;
    if (inside > 0) {
      Packets.Writer x = new Packets.Writer();
      for (int i = 0; i < cuts.size(); i++) {
        if (i == inside) x.restart(Packets.LOST);
        x.data(2, f, cuts.get(i)[0], cuts.get(i)[1]);
      }
      refusedAs(x.toArray(), options(1), "restart", inside + 1, starts(x.toArray()).get(inside), "a restart link packet inside a frame");
    }
    refusedAs(sealed(file, s, 2, q -> q.number = 78), options(1), "number", 2, s.get(1), "after a restart, the old run's next number");
    System.out.println("restarts: the restart link packet takes no number, starts the numbers again, and may not cut a frame");
  }

  static void streams() {
    Random rnd = new Random(7);
    byte[] f = frames(rnd, 70, 5, 33), fg = new byte[16 * 32];
    rnd.nextBytes(fg);
    List<int[]> cuts = Packets.cuts(f, new int[] {32});
    Packets.Writer w = new Packets.Writer();
    long words = 0;
    for (int[] c : cuts) {
      int n = 1 + rnd.nextInt(32);
      w.data(1, fg, 0, n).data(2, f, c[0], c[1]).empty(1);
      words += n;
    }
    byte[] file = w.toArray();
    PacketFile p = good(file, options(1), f, "stream 1's packets among stream 2's");
    // one run of numbers for the link (28.4): stream 1's data 1, 3, 5, ..., stream 2's 2, 4, 6, ..., each empty the next
    expect(p != null && p.onStream[1] == 2 * cuts.size() && p.onStream[2] == cuts.size() && p.foregroundWords == words
        && p.firstNumber[1] == 1 && p.lastNumber[1] == 2 * cuts.size() + 1 && p.lastNumber[2] == 2 * cuts.size(), "one run of numbers");
    // --first is the link's: the first link packet, stream 1's, read as from 2
    refusedAs(file, options(2), "number", 1, -1, "the link read as from 2");
    // stream 1's second data link packet numbered as if stream 1 had its own run (2 in place of 3)
    List<Integer> s = starts(file);
    refusedAs(sealed(file, s, 4, q -> q.number = 2), options(1), "number", 4, s.get(3), "stream 1's second link packet numbered 2");
    System.out.println("two streams: one run of numbers for both; stream 1's data is counted and kept nowhere");
  }

  static void cuts() {
    Random rnd = new Random(8);
    int files = 0;
    for (int trial = 0; trial < 300; trial++) {
      int[] lens = new int[1 + rnd.nextInt(12)];
      for (int i = 0; i < lens.length; i++) lens[i] = rnd.nextInt(150);
      byte[] f = frames(rnd, lens);
      Packets.Options o = new Packets.Options();
      o.cut = new int[1 + rnd.nextInt(7)];
      for (int i = 0; i < o.cut.length; i++) o.cut[i] = 1 + rnd.nextInt(32);
      Packets.Result r = Packets.packetize(f, o);
      PacketFile p = good(r.file, options(1), f, "a legal cut");
      expect(p != null && p.packets == r.packets && p.full + p.shortLast + p.shortInside == p.packets, "a legal cut: the packets");
      files++;
    }
    System.out.println("other cuts: " + files + " files cut into packets of any legal lengths, taken, the frames byte-identical");
  }

  // ---- 7. the vectors ----

  static void vectors() {
    int packets = 0;
    for (PacketVectors.Vector v : PacketVectors.vectors()) {
      PacketFile.Options o = new PacketFile.Options();
      o.frames = false;
      o.first = v.next;
      byte[] b = v.p.bytes();
      PacketFile.Refused r = refused(b, o);
      String got = r == null ? "ok" : r.check;
      expect(got.equals(v.expect), "vector " + v.name + ": expect " + v.expect + ", the reader says " + (r == null ? "ok" : r.getMessage()));
      if (r == null) {
        // the number expected after it: a data packet with that number is taken next, and no other
        int after = (v.p.flags & Packets.RESTART) != 0 ? Packets.FIRST_NUMBER     // the numbers start again (28.9)
            : v.p.data.length == 0 ? v.next : Packets.next(v.next);
        for (int n : new int[] {after, Packets.next(after), v.next == after ? 0 : v.next}) {
          Packets.Packet q = new Packets.Packet();
          q.stream = v.p.stream;
          q.number = n;
          q.data = new byte[16];
          byte[] two = Arrays.copyOf(b, b.length + 32);
          System.arraycopy(q.bytes(), 0, two, b.length, 32);
          expect((refused(two, o) == null) == (n == after), "vector " + v.name + ": the number after it");
        }
      }
      packets++;
    }
    byte[] frames = PacketVectors.smallFrames();
    List<PacketVectors.Whole> files = PacketVectors.files();
    for (PacketVectors.Whole w : files) {
      PacketFile p = good(w.bytes, options(w.options.isEmpty() ? 1 : 0x3FFFA), frames, w.name);
      long[] c = PacketVectors.count(w.bytes);
      expect(p != null && p.packets == c[0] && p.onStream[2] == c[1] && p.onStream[1] == c[2] && p.empty == c[3] && p.idleBytes == c[4]
          && p.words == c[5] && p.foregroundWords == c[6] && p.restarts == c[7] && p.frames == PacketVectors.SMALL.length,
          w.name + ": its counts");
    }
    // 28.9's restart link packet: its data checksum, copied from the chapter, and its data's first two words
    Packets.Packet rp = Packets.restartPacket(Packets.LOST);
    expect(rp.dataChecksumField() == 0xC8981D21 && rp.data.length == 512 && rp.data[0] == 2 && rp.data[31] == (byte) 0x83
        && rp.data[32] == 2 && rp.data[511] == (byte) 0x83, "28.9's restart link packet: data checksum 0xC8981D21, the primes 2 to 131");
    // machine-spec 28.5's four example headers and 28.6's masked data lines, copied from v1.37_wip/spec/host-link.md:
    // the packetizer's bytes on the line
    String[][] examples = {
        {"host-link-example", "02 01 01 00 00 00 00 00 EA 9A 70 42 A0 8C 06 E1 2A 54 C0 74 C0 1C 58 2F A6 5D 27 37 39 C9 47 1F"},
        {"host-link-example-one-hop", "02 01 01 00 00 00 10 00 EA 9A 70 42 9E BC 0A 21"},
        {"host-link-example-hops-3", "11 01 CD AB 56 55 35 02 EA 9A 70 42 D2 BF BA 0A"},
        {"host-link-example-length-0", "01 00 05 00 10 00 00 01 00 00 00 00 57 09 4D 4E"},
        {"host-link-mask-example", "01 01 07 00 0C 00 00 00 EB 08 C9 D9 9E 94 2A 36 A4 D0 6E 3C 10 A4 2F 89 F5 74 21 9E DE 43 E4 C0"},
        {"host-link-mask-example-forwarded",
            "01 01 07 00 10 00 10 00 EB 08 C9 D9 A3 2B 76 11 79 F1 F2 82 77 D9 B4 AF 91 8F 7C 00 39 96 A1 51"}};
    for (String[] e : examples) {
      PacketVectors.Vector found = null;
      for (PacketVectors.Vector v : PacketVectors.vectors()) if (v.name.equals(e[0])) found = v;
      String[] hex = e[1].split(" ");
      byte[] want = new byte[hex.length];
      for (int i = 0; i < hex.length; i++) want[i] = (byte) Integer.parseInt(hex[i], 16);
      byte[] got = found == null ? new byte[0] : found.p.bytes();
      expect(Arrays.equals(Arrays.copyOf(got, want.length), want), "28.5/28.6's " + e[0] + ": the packetizer's bytes on the line");
    }
    List<PacketVectors.Damage> damage = PacketVectors.damage(files);
    for (PacketVectors.Damage d : damage) {
      byte[] f = null;
      for (PacketVectors.Whole w : files) if (w.name.equals(d.file)) f = w.bytes;
      refusedAs(flipped(f, d.at, d.xor), options(1), d.expect, d.packet, d.where, "damage " + d.file + " byte " + d.at + " xor " + d.xor);
    }
    System.out.println("vectors: " + packets + " single packets, " + files.size() + " whole files and " + damage.size() + " damaged files:"
        + " the reader says what VECTORS.txt expects of each");
  }
}
