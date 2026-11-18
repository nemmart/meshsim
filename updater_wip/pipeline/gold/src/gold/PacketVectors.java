package gold;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * packets-vectors: the test vectors for an Ingress written from the documents (WORKER3's black box): VECTORS.txt and a
 * few small whole packet files, the same bytes every time. They are made by the writing side (Packets); every
 * "expect" is by construction, and the tests then get each from the reading side (PacketsTest) and from a third
 * implementation in Python (test/packets/check_vectors.py). The committed copies are test/packets/; the build
 * regenerates them and compares.
 */
public final class PacketVectors {
  private PacketVectors() {}

  /** SplitMix64: the vectors' data. */
  static final class Rng {
    private long s;

    Rng(long seed) { s = seed; }

    long next() {
      long z = (s += 0x9E3779B97F4A7C15L);
      z = (z ^ (z >>> 30)) * 0xBF58476D1CE4E5B9L;
      z = (z ^ (z >>> 27)) * 0x94D049BB133111EBL;
      return z ^ (z >>> 31);
    }

    byte[] words(int n) {
      byte[] b = new byte[16 * n];
      for (int i = 0; i < b.length; i += 8) {
        long v = next();
        for (int j = 0; j < 8; j++) b[i + j] = (byte) (v >>> (8 * j));
      }
      return b;
    }
  }

  /** One packet, what the receiver expects before it, and what it must say. */
  static final class Vector {
    final String name, note, expect;
    final Packets.Packet p;
    final int next;

    Vector(String name, String note, int next, Packets.Packet p, String expect) {
      this.name = name;
      this.note = note;
      this.next = next;
      this.p = p;
      this.expect = expect;
    }
  }

  static Packets.Packet packet(int stream, int number, byte[] data) {
    Packets.Packet p = new Packets.Packet();
    p.stream = stream;
    p.number = number;
    p.data = data;
    return p;
  }

  /** The good packet the bad ones are made from: stream 2, number 5, three words. */
  static Packets.Packet base() { return packet(2, 5, new Rng(3).words(3)); }

  /** A bad packet from base(): the change, with both checks left as the good packet's. */
  interface Change { void to(Packets.Packet p); }

  static Packets.Packet damaged(Change c) {
    Packets.Packet p = base();
    p.dataChecksum = p.dataChecksumField();
    p.headerCrc = p.headerCrcField();
    c.to(p);
    return p;
  }

  /** A bad packet from base(): the change, with both checks made right again. */
  static Packets.Packet sealed(Change c) {
    Packets.Packet p = base();
    c.to(p);
    return p;
  }

  static List<Vector> vectors() {
    List<Vector> v = new ArrayList<>();
    Rng r = new Rng(1);
    byte[] ff = new byte[16 * 32];
    Arrays.fill(ff, (byte) 0xff);
    // ---- good ----
    v.add(new Vector("one-zero-word", "the first packet of a file: one word of data, all zeros", 1, packet(2, 1, new byte[16]), "ok"));
    v.add(new Vector("two-words", "", 2, packet(2, 2, r.words(2)), "ok"));
    v.add(new Vector("full", "32 words, the most a packet holds", 3, packet(2, 3, r.words(32)), "ok"));
    v.add(new Vector("full-of-ones", "32 words of all ones; a number with a digit in every place", 0x12345, packet(2, 0x12345, ff), "ok"));
    v.add(new Vector("last-number", "the last number, 0x3FFFE: the next is 1", 0x3FFFE, packet(2, 0x3FFFE, r.words(4)), "ok"));
    v.add(new Vector("after-the-wrap", "number 1 again, after 0x3FFFE", 1, packet(2, 1, r.words(4)), "ok"));
    v.add(new Vector("number-above-0xffff", "number 0x10000: the first that needs bit [32], byte 4's low two bits", 0x10000,
        packet(2, 0x10000, r.words(1)), "ok"));
    v.add(new Vector("foreground", "stream 1", 9, packet(1, 9, r.words(1)), "ok"));
    Packets.Packet fl = packet(2, 6, r.words(2));
    fl.flags = 0xF;
    fl.acknowledge = 0x2BCDE;
    v.add(new Vector("flags-and-acknowledge", "every flag set and an acknowledge number: neither is checked", 6, fl, "ok"));
    Packets.Packet hp = packet(2, 6, r.words(1));
    hp.hops = 3;
    v.add(new Vector("hops-3", "hops 3: not tested, no value of it is an error", 6, hp, "ok"));
    Packets.Packet hf = packet(2, 0x2ABCD, r.words(2));
    hf.hops = 15;
    hf.acknowledge = 0x3FFFF;
    v.add(new Vector("hops-15", "hops 15 and the acknowledge field all ones, number 0x2ABCD: every bit of bytes 4 to 6 used", 0x2ABCD,
        hf, "ok"));
    // machine-spec 28.5's four worked examples, and 28.6's two of the mask (v1.37 stage 4: chapter 28)
    v.add(new Vector("host-link-example", "28.5's example 1: stream 2, length 1, number 1, ack 0, the data one zero word; on the line"
        + " the data is the mask itself (28.6's example 1)", 1, packet(2, 1, new byte[16]), "ok"));
    Packets.Packet h1 = packet(2, 1, new byte[16]);
    h1.hops = 1;
    v.add(new Vector("host-link-example-one-hop", "28.5's example 2: the same after one hop, hops 1", 1, h1, "ok"));
    Packets.Packet h3 = packet(1, 0x2ABCD, new byte[16]);
    h3.acknowledge = 0x15555;
    h3.hops = 3;
    h3.flags = 1;                                                 // resend
    h3.stops = 2;                                                 // bg_stop
    v.add(new Vector("host-link-example-hops-3", "28.5's example 3: foreground, length 1, number 0x2ABCD, ack 0x15555, hops 3, resend"
        + " and bg_stop set", 0x2ABCD, h3, "ok"));
    Packets.Packet h4 = packet(1, 5, new byte[0]);
    h4.acknowledge = 4;
    h4.stops = 1;                                                 // fg_stop
    v.add(new Vector("host-link-example-length-0", "28.5's example 4: length 0, stream 1, the next number 5, ack 4, fg_stop set", 5, h4,
        "ok"));
    byte[] up16 = new byte[16];
    for (int i = 0; i < 16; i++) up16[i] = (byte) i;
    Packets.Packet m2 = packet(1, 7, up16);
    m2.acknowledge = 3;
    v.add(new Vector("host-link-mask-example", "28.6's example 2: foreground, length 1, number 7, ack 3, the data 00 to 0F under its mask",
        7, m2, "ok"));
    Packets.Packet m3 = packet(1, 7, up16.clone());
    m3.acknowledge = 4;
    m3.hops = 1;
    v.add(new Vector("host-link-mask-example-forwarded", "28.6's example 3: the same data forwarded, hops 1, ack 4: a new header, a new"
        + " mask", 7, m3, "ok"));
    Packets.Packet st = packet(2, 6, r.words(1));
    st.stops = 3;
    v.add(new Vector("stop-bits", "both stop bits set: not tested, no value of them is an error (28.3: byte 7's bits 0 and 1)", 6, st,
        "ok"));
    v.add(new Vector("empty", "length 0: its number is the one the next packet with data will carry, and it takes none", 7,
        packet(2, 7, new byte[0]), "ok"));
    v.add(new Vector("empty-first", "length 0 before any data: number 1, and 1 is still to come", 1, packet(2, 1, new byte[0]), "ok"));
    v.add(new Vector("empty-at-the-last-number", "length 0 when 0x3FFFE is next", 0x3FFFE, packet(2, 0x3FFFE, new byte[0]), "ok"));
    v.add(new Vector("empty-foreground", "length 0 on stream 1", 4, packet(1, 4, new byte[0]), "ok"));
    // ---- bad: one bit changed in the good packet "base", the checks left as they were ----
    v.add(new Vector("base", "the good packet the bad ones below are made from", 5, base(), "ok"));
    v.add(new Vector("bit-stream", "base with header bit 0 changed (the stream becomes 3)", 5, damaged(p -> p.stream = 3), "header-crc"));
    v.add(new Vector("bit-flags", "base with header bit 4 changed (the resend flag)", 5, damaged(p -> p.flags = 1), "header-crc"));
    v.add(new Vector("bit-length", "base with header bit 8 changed (the length becomes 2)", 5, damaged(p -> p.length = 2), "header-crc"));
    v.add(new Vector("bit-number-low", "base with header bit 16 changed (the number becomes 4)", 5, damaged(p -> p.number = 4), "header-crc"));
    v.add(new Vector("bit-number-high", "base with header bit 33 changed (the number's top bit)", 5, damaged(p -> p.number = 0x20005),
        "header-crc"));
    v.add(new Vector("bit-acknowledge", "base with header bit 40 changed (in the acknowledge field)", 5, damaged(p -> p.acknowledge = 0x40),
        "header-crc"));
    v.add(new Vector("bit-hops", "base with header bit 52 changed (hops becomes 1): the header CRC covers it", 5, damaged(p -> p.hops = 1),
        "header-crc"));
    v.add(new Vector("bit-reserved", "base with header bit 63 changed (reserved)", 5, damaged(p -> p.reserved = 0x20), "header-crc"));
    v.add(new Vector("bit-data-checksum", "base with header bit 64 changed (the data checksum's lowest bit): the header CRC covers it", 5,
        damaged(p -> p.dataChecksum ^= 1), "header-crc"));
    v.add(new Vector("bit-header-crc", "base with header bit 127 changed (the header CRC's top bit)", 5,
        damaged(p -> p.headerCrc ^= 0x80000000), "header-crc"));
    v.add(new Vector("bit-data-first", "base with the lowest bit of its first data byte changed", 5, damaged(p -> p.data[0] ^= 1),
        "data-checksum"));
    v.add(new Vector("bit-data-last", "base with the top bit of its last data byte changed", 5,
        damaged(p -> p.data[p.data.length - 1] ^= (byte) 0x80), "data-checksum"));
    // ---- bad: one field wrong, both checks right for it ----
    v.add(new Vector("stream-0", "stream 0 with the resend flag set, so the first byte is 0x10; both checks right", 5, sealed(p -> {
      p.stream = 0;
      p.flags = 1;
    }), "stream"));
    v.add(new Vector("stream-3", "stream 3; both checks right", 5, sealed(p -> p.stream = 3), "stream"));
    v.add(new Vector("stream-15", "stream 15; both checks right", 5, sealed(p -> p.stream = 15), "stream"));
    v.add(new Vector("length-33", "length 33, with 33 words of data; both checks right", 5, sealed(p -> p.data = new Rng(4).words(33)),
        "length"));
    v.add(new Vector("length-64", "length 64, with 64 words of data; both checks right", 5, sealed(p -> p.data = new Rng(5).words(64)),
        "length"));
    v.add(new Vector("reserved-low", "reserved bit [58] set; both checks right", 5, sealed(p -> p.reserved = 0x01), "reserved"));
    v.add(new Vector("reserved-high", "reserved bit [63] set; both checks right", 5, sealed(p -> p.reserved = 0x20), "reserved"));
    v.add(new Vector("data-checksum-wrong", "the data checksum one bit off, the header CRC right for it", 5,
        sealed(p -> p.dataChecksum = p.dataChecksumField() ^ 0x00010000), "data-checksum"));
    v.add(new Vector("number-0", "number 0; both checks right", 5, sealed(p -> p.number = 0), "number"));
    v.add(new Vector("number-all-ones", "number 0x3FFFF; both checks right", 5, sealed(p -> p.number = 0x3FFFF), "number"));
    v.add(new Vector("number-again", "the last packet's number again (4 when 5 is next)", 5, sealed(p -> p.number = 4), "number"));
    v.add(new Vector("number-skipped", "a number skipped (6 when 5 is next)", 5, sealed(p -> p.number = 6), "number"));
    v.add(new Vector("number-no-wrap", "0x3FFFF after 0x3FFFE, where 1 is next", 1, sealed(p -> p.number = 0x3FFFF), "number"));
    v.add(new Vector("number-2-after-the-wrap", "2 after 0x3FFFE, where 1 is next", 1, sealed(p -> p.number = 2), "number"));
    v.add(new Vector("number-0x3ffff-with-hops", "number 0x3FFFF with hops 15: the hops field does not make it good", 5, sealed(p -> {
      p.number = 0x3FFFF;
      p.hops = 15;
    }), "number"));
    v.add(new Vector("foreground-wrong-number", "stream 1 with a number that is not the link's next (one run for both streams, 28.4)", 5,
        sealed(p -> {
      p.stream = 1;
      p.number = 7;
    }), "number"));
    // ---- bad: length 0 ----
    v.add(new Vector("empty-with-the-last-number", "length 0 holding the last packet's number (4) and not the next one's (5)", 5,
        packet(2, 4, new byte[0]), "number"));
    v.add(new Vector("empty-one-ahead", "length 0 holding 6 when 5 is next", 5, packet(2, 6, new byte[0]), "number"));
    v.add(new Vector("empty-number-0", "length 0 with number 0", 5, packet(2, 0, new byte[0]), "number"));
    Packets.Packet e = packet(2, 5, new byte[0]);
    e.dataChecksum = 0x42709AEA;
    v.add(new Vector("empty-with-a-checksum", "length 0 with a data checksum that is not 0x00000000 (it is one zero word's); the header"
        + " CRC right for it", 5, e, "data-checksum"));
    return v;
  }

  /** small.frames: nine frames of types 1 to 9 with 0, 1, 2, 30, 31, 32, 33, 63 and 64 words of data, so that with
   *  its header word a frame is 1, 2, 3, 31, 32, 33, 34, 64 and 65 words: every way a frame meets a packet's end. The
   *  headers are well-formed; the data is made up, and the file is not a proof. */
  static final int[] SMALL = {0, 1, 2, 30, 31, 32, 33, 63, 64};

  static byte[] smallFrames() {
    ByteArrayOutputStream out = new ByteArrayOutputStream();
    Rng r = new Rng(2);
    for (int i = 0; i < SMALL.length; i++) Frames.frame(out, i + 1, Frames.NONE, r.words(SMALL[i]));
    return out.toByteArray();
  }

  /** A whole file and what is in it. */
  static final class Whole {
    final String name, note, options;
    final byte[] bytes;

    Whole(String name, String note, String options, byte[] bytes) {
      this.name = name;
      this.note = note;
      this.options = options;
      this.bytes = bytes;
    }
  }

  static final int[] IDLE = {1, 15, 16, 17, 31, 33, 1024, 1025, 0, 32, 7, 2, 16};
  static final int[] SPLIT = {1, 7, 32, 3, 16, 31, 2, 32, 5};
  static final int[] FOREGROUND = {1, 32, 5, 2};

  static List<Whole> files() {
    byte[] frames = smallFrames();
    List<Whole> out = new ArrayList<>();
    out.add(new Whole("small.packets", "small.frames as the packetizer writes it", "", Packets.packetize(frames, new Packets.Options()).file));

    // zeros before the first packet, between the packets in runs of every kind, and after the last; three packets
    // of length 0: one between two frames, two together inside a frame
    Packets.Writer w = new Packets.Writer();
    List<int[]> cuts = Packets.cuts(frames, new int[] {32});
    w.idle(3);
    for (int i = 0; i < cuts.size(); i++) {
      int[] c = cuts.get(i);
      if (i > 0) w.idle(IDLE[(i - 1) % IDLE.length]);
      if (i == 5) w.empty(2).idle(16);                           // before frame 6's packet
      if (i == 8) w.empty(2).empty(2).idle(5);                   // between frame 7's two packets
      w.data(2, frames, c[0], c[1]);
    }
    w.idle(40);
    out.add(new Whole("small-idle.packets", "small.frames with zero bytes before the first packet (3), between the packets (runs of 1, 15,"
        + " 16, 17, 31, 33, 1024, 1025, 0, 32, 7, 2 and 16) and after the last (40), and three packets of length 0: one between two"
        + " frames, two together inside a frame", "", w.toArray()));

    Packets.Options split = new Packets.Options();
    split.cut = SPLIT;
    out.add(new Whole("small-split.packets", "small.frames cut into packets of 1, 7, 32, 3, 16, 31, 2, 32 and 5 words in turn (a frame's"
        + " last holding what remains): legal on the link, and never written by the packetizer", "", Packets.packetize(frames, split).file));

    Packets.Options wrap = new Packets.Options();
    wrap.first = 0x3FFFA;
    out.add(new Whole("wrap.packets", "small.frames numbered from 0x3FFFA: the numbers pass 0x3FFFE and go on from 1", "first 0x3fffa",
        Packets.packetize(frames, wrap).file));

    // stream 1's link packets among stream 2's, one run of numbers for both (28.4)
    w = new Packets.Writer();
    Rng r = new Rng(6);
    for (int i = 0; i < cuts.size(); i++) {
      int[] c = cuts.get(i);
      w.data(2, frames, c[0], c[1]);
      if (i % 2 == 1) {
        int n = FOREGROUND[(i / 2) % FOREGROUND.length];
        w.data(1, r.words(n), 0, n);
      }
      if (i == 6) w.empty(1);
    }
    out.add(new Whole("two-streams.packets", "small.frames on stream 2 with link packets of stream 1 among them (made-up data; one of"
        + " length 0): one run of numbers from 1 for both streams (28.4). The proof's file has no stream 1; this is for a reader that takes"
        + " both", "",
        w.toArray()));
    return out;
  }

  /** One change to a whole file and what a reader must then say: the check, the packet (from 1) and its byte. */
  static final class Damage {
    final String file, expect, note;
    final int at, xor, packet, where;

    Damage(String file, int at, int xor, String expect, int packet, int where, String note) {
      this.file = file;
      this.at = at;
      this.xor = xor;
      this.expect = expect;
      this.packet = packet;
      this.where = where;
      this.note = note;
    }
  }

  /** Where each packet of a file without idle bytes starts, by the length bytes. */
  static List<Integer> starts(byte[] file) {
    List<Integer> s = new ArrayList<>();
    for (int pos = 0; pos < file.length; pos += 16 + 16 * (file[pos + 1] & 0xff)) s.add(pos);
    return s;
  }

  static List<Damage> damage(List<Whole> files) {
    List<Damage> d = new ArrayList<>();
    byte[] small = files.get(0).bytes;
    List<Integer> at = starts(small);
    String f = "small.packets";
    for (int k : new int[] {1, 7, 14}) {                          // a first packet, one inside a frame, the last
      int a = at.get(k - 1), len = small[a + 1] & 0xff;
      d.add(new Damage(f, a, 0x01, "header-crc", k, a, "the stream"));
      d.add(new Damage(f, a + 1, 0x01, "header-crc", k, a, "the length"));
      d.add(new Damage(f, a + 2, 0x01, "header-crc", k, a, "the packet number"));
      d.add(new Damage(f, a + 4, 0x02, "header-crc", k, a, "the packet number's top bit"));
      d.add(new Damage(f, a + 5, 0x04, "header-crc", k, a, "the acknowledge field"));
      d.add(new Damage(f, a + 6, 0x10, "header-crc", k, a, "the hops field"));
      d.add(new Damage(f, a + 7, 0x80, "header-crc", k, a, "a reserved bit"));
      d.add(new Damage(f, a + 8, 0x01, "header-crc", k, a, "the data checksum"));
      d.add(new Damage(f, a + 15, 0x80, "header-crc", k, a, "the header CRC"));
      d.add(new Damage(f, a + 16, 0x01, "data-checksum", k, a, "the first data byte"));
      d.add(new Damage(f, a + 16 + 16 * len - 1, 0x80, "data-checksum", k, a, "the last data byte"));
      // the first byte made zero: it is then an idle byte, and the header is taken to start at the length byte
      d.add(new Damage(f, a, 0x02, "header-crc", k, a + 1, "the first byte made zero: it is skipped as idle, and a header is taken"
          + " to start at the next byte"));
    }
    // an idle byte made non-zero, in small-idle.packets' run of 1,024
    byte[] idle = files.get(1).bytes;
    int run = 0, start = -1;
    for (int i = 0; i < idle.length && start < 0; i++) {
      run = idle[i] == 0 ? run + 1 : 0;
      if (run == 1024) start = i - 1023;
    }
    int k = 0;                                                    // the packets before the run
    for (int pos = 0; pos < start; ) {
      if (idle[pos] == 0) { pos++; continue; }
      k++;
      pos += 16 + 16 * (idle[pos + 1] & 0xff);
    }
    d.add(new Damage("small-idle.packets", start + 100, 0x02, "header-crc", k + 1, start + 100, "an idle byte made 0x02: a header is"
        + " taken to start there"));
    d.add(new Damage("small-idle.packets", start + 100, 0x10, "header-crc", k + 1, start + 100, "an idle byte made 0x10 (stream 0): the"
        + " header CRC is tested first"));
    return d;
  }

  static String hex(byte[] b, int off, int len) {
    StringBuilder s = new StringBuilder();
    for (int i = off; i < off + len; i++) s.append(String.format("%02x", b[i]));
    return s.toString();
  }

  static String hex(byte[] b) { return hex(b, 0, b.length); }

  /** A note in lines of at most 110 characters. */
  static List<String> wrapped(String note) {
    List<String> lines = new ArrayList<>();
    StringBuilder line = new StringBuilder();
    for (String word : note.split(" ")) {
      if (line.length() > 0 && line.length() + 1 + word.length() > 110) {
        lines.add(line.toString());
        line.setLength(0);
      }
      line.append(line.length() > 0 ? " " : "").append(word);
    }
    lines.add(line.toString());
    return lines;
  }

  static final String HEAD = """
      # Test vectors for chapter 28's link packets (v1.37_wip/spec/host-link.md, 28.3 to 28.6; v1.37's INTEGRATOR, stage 4;
      # v1.4_wip/planning/HOST-LINK.md's packets before), for a reader written from the specification. Made by gold's
      # packets-vectors command (updater_wip/pipeline/gold, WORKER2); do not edit: the build regenerates this file and
      # compares. The choices behind it are in updater_wip/planning/sessions/WORKER2-NOTES.md.
      #
      # A LINK PACKET is a header word of 128 bits, then 0 to 32 words of data. Every word goes on the link least
      # significant byte first: byte i of a word holds its bits [8i+7:8i]. The header word:
      #   byte 0       stream [3:0] (1 foreground, 2 background; never 0); flags [7:4]: resend, lost, recovery, restart
      #   byte 1       length [15:8]: the data's 128-bit words, 0 to 32
      #   bytes 2-4    number [33:16], 18 bits, 1 to 0x3FFFE (byte 4's low two bits are its top two): ONE RUN FOR THE
      #                LINK, both streams (28.4)
      #   bytes 4-6    ack [51:34], 18 bits (byte 4's high six bits are its low six; byte 6's low four its top four)
      #   byte 6       hops [55:52] in its high four bits: the ASICs that forwarded the link packet; its maker writes 0
      #   byte 7       fg_stop [56], bg_stop [57]; reserved [63:58], 0
      #   bytes 8-11   data checksum [95:64]: the CRC-32C of the data as it is held, before the mask; of no data, 0
      #   bytes 12-15  header CRC [127:96]: the CRC-32C of header bytes 0 to 11, in link order
      # THE DATA ON THE LINE is masked (28.6): each byte XORed with the low byte of a register started at
      #   header_crc | 0x10000 and stepped 8 times as the CRC-32C register is with a zero bit (reg = (reg >> 1) XOR
      #   0x82F63B78 if its bit 0 is 1, else reg >> 1) before each byte. "data" below is as held; "bytes", on the line.
      #
      # CRC-32C (Castagnoli; iSCSI's): polynomial 0x1EDC6F41, reflected; initial value 0xFFFFFFFF; final XOR 0xFFFFFFFF.
      #   crc = 0xFFFFFFFF
      #   for each byte b, in order:  crc = crc XOR b
      #                               8 times:  crc = (crc >> 1) XOR 0x82F63B78 if crc's bit 0 is 1, else crc >> 1
      #   result = crc XOR 0xFFFFFFFF, stored least significant byte first
      # The CRC-32C of all 16 header bytes of a link packet whose header CRC is right is always 0x48674BC7.
      #
      # THE CHECKS, in the order gold's reader makes them; "expect" below is "ok" or the first one that fails:
      #   header-crc     the CRC-32C of bytes 0 to 11 is not bytes 12 to 15
      #   stream         the stream is not 1 or 2
      #   length         the length is over 32
      #   reserved       bits [63:58] are not 0
      #   data-checksum  the CRC-32C of the data, its mask taken off, is not bytes 8 to 11
      #   number         a link packet with data: its number is not the link's next (1 first; after 0x3FFFE, 1),
      #                  whatever its stream. One of length 0: its number is not the one the next link packet with data
      #                  will carry; it takes no number, and the next link packet with data carries the same one
      #   frame-end      whole files only: on stream 2 a frame ends before the end of the link packet its last word is in
      #   truncated      whole files only: the file ends inside a link packet or inside a frame
      # The flags, the ack field, the hops field and the stop bits are not checked: no value of them is an error. Between
      # link packets, zero bytes are idle and are skipped, in any number: the first byte that is not zero starts a header.
      #
      # THIS FILE: a line is a key and its value; "#" starts a comment; hex strings are bytes in link order.
      #   crc32c <bytes, or - for none> <the CRC-32C, as a number>
      #   packet <name> ... end     one link packet alone. Frames are not looked at here.
      #       next-number        the number the receiver expects next on the link, before the link packet
      #       stream, flags, length, number, acknowledge, hops, stops, reserved     the header's fields as the bytes hold
      #                          them (stops: fg_stop + 2 bg_stop; reserved: bits [63:58])
      #       data               one line for each word of data, as held (before the mask)
      #       data-checksum      the header's field;   data-crc32c    the CRC-32C of the data (equal in a good one)
      #       header-0-11        header bytes 0 to 11: what the header CRC is over
      #       header-crc         the header's field;   header-crc32c  the CRC-32C of bytes 0 to 11
      #       header-word        the header as one 128-bit number, most significant digit first
      #       bytes              the whole link packet as it goes on the line (the data masked), 16 bytes to a line
      #       expect             ok, or the check that refuses it
      #       next-number-after  for ok: the number expected next on the link after this link packet
      #   file <name> ... end       a whole packet file beside this one; its frames are small.frames
      #   damage <file> byte <offset> xor <mask> expect <check> packet <n> at <offset>
      #                             the file with that one byte changed: the check that must refuse it, at which link
      #                             packet (counted from 1 in the file) and the byte where it is taken to start
      """;

  static String text(List<Vector> vectors, byte[] frames, List<Whole> files, List<Damage> damage) {
    StringBuilder s = new StringBuilder(HEAD);
    s.append("\n# ---- CRC-32C ----\n");
    byte[] up = new byte[32], down = new byte[32], ff = new byte[32];
    for (int i = 0; i < 32; i++) {
      up[i] = (byte) i;
      down[i] = (byte) (31 - i);
      ff[i] = (byte) 0xff;
    }
    Object[][] crcs = {{new byte[0], "no bytes"}, {"123456789".getBytes(StandardCharsets.US_ASCII), "the ASCII string 123456789"},
        {new byte[1], "one zero byte"}, {new byte[16], "one zero word"}, {new byte[32], "32 zero bytes"}, {ff, "32 bytes of 0xFF"},
        {up, "0x00 up to 0x1F"}, {down, "0x1F down to 0x00"}};
    for (Object[] c : crcs) {
      byte[] b = (byte[]) c[0];
      s.append(String.format("crc32c %s 0x%08x    # %s%n", b.length == 0 ? "-" : hex(b), Packets.crc32c(b, 0, b.length), c[1]));
    }
    s.append("\n# ---- single packets ----\n");
    for (Vector v : vectors) {
      Packets.Packet p = v.p;
      byte[] low = p.headerLow(), header = p.header(), all = p.bytes();
      s.append(String.format("%npacket %s%n", v.name));
      if (!v.note.isEmpty()) for (String line : wrapped(v.note)) s.append("  # ").append(line).append('\n');
      s.append(String.format("  next-number        0x%05x%n", v.next));
      s.append(String.format("  stream             %d%n  flags              %d%n  length             %d%n", p.stream, p.flags, p.lengthField()));
      s.append(String.format("  number             0x%05x%n  acknowledge        0x%05x%n  hops               %d%n  stops              %d%n"
          + "  reserved           0x%02x%n", p.number, p.acknowledge, p.hops, p.stops, p.reserved));
      for (int i = 0; i < p.data.length; i += 16) s.append("  data               ").append(hex(p.data, i, 16)).append('\n');
      s.append(String.format("  data-checksum      0x%08x%n  data-crc32c        0x%08x%n", p.dataChecksumField(),
          Packets.crc32c(p.data, 0, p.data.length)));
      s.append("  header-0-11        ").append(hex(low)).append('\n');
      s.append(String.format("  header-crc         0x%08x%n  header-crc32c      0x%08x%n", p.headerCrcField(), Packets.crc32c(low, 0, 12)));
      StringBuilder word = new StringBuilder();
      for (int i = 15; i >= 0; i--) word.append(String.format("%02x", header[i]));
      s.append("  header-word        0x").append(word).append('\n');
      for (int i = 0; i < all.length; i += 16) s.append("  bytes              ").append(hex(all, i, 16)).append('\n');
      s.append("  expect             ").append(v.expect).append('\n');
      if (v.expect.equals("ok")) {
        s.append(String.format("  next-number-after  0x%05x%n", p.data.length == 0 ? v.next : Packets.next(v.next)));
      }
      s.append("end\n");
    }
    s.append("\n# ---- whole files ----\n");
    s.append(String.format("%n# small.frames: %d bytes, SHA-256 %s: nine frames of types 1 to 9 with 0, 1, 2, 30, 31, 32, 33, 63 and 64%n"
        + "# words of data (1, 2, 3, 31, 32, 33, 34, 64 and 65 with the header word). Made-up data: for Ingress alone, not a proof.%n",
        frames.length, PageImage.sha256(frames)));
    for (Whole w : files) {
      long[] c = count(w.bytes);
      s.append(String.format("%nfile %s%n", w.name));
      for (String line : wrapped(w.note)) s.append("  # ").append(line).append('\n');
      s.append(String.format("  bytes              %d%n  sha256             %s%n", w.bytes.length, PageImage.sha256(w.bytes)));
      s.append(String.format("  first-number       0x%05x%n", w.options.isEmpty() ? 1 : 0x3FFFA));
      s.append(String.format("  packets            %d%n  packets-stream-2   %d%n  packets-stream-1   %d%n  packets-length-0   %d%n", c[0],
          c[1], c[2], c[3]));
      s.append(String.format("  idle-bytes         %d%n  words-stream-2     %d%n  words-stream-1     %d%n  frames             %d%n", c[4],
          c[5], c[6], SMALL.length));
      s.append("  frames-file        small.frames\n  expect             ok\nend\n");
    }
    s.append("\n# ---- whole files, one byte changed ----\n\n");
    for (Damage d : damage) {
      s.append(String.format("damage %s byte %d xor 0x%02x expect %s packet %d at %d    # %s%n", d.file, d.at, d.xor, d.expect, d.packet,
          d.where, d.note));
    }
    return s.toString();
  }

  /** A good file's counts, by walking it: packets, on stream 2, on stream 1, of length 0, idle bytes, stream 2's
   *  words, stream 1's. */
  static long[] count(byte[] f) {
    long[] c = new long[7];
    for (int pos = 0; pos < f.length; ) {
      if (f[pos] == 0) { c[4]++; pos++; continue; }
      int stream = f[pos] & 15, len = f[pos + 1] & 0xff;
      c[0]++;
      c[stream == 2 ? 1 : 2]++;
      if (len == 0) c[3]++;
      c[stream == 2 ? 5 : 6] += len;
      pos += 16 + 16 * len;
    }
    return c;
  }

  /** Every file by name, VECTORS.txt first. */
  static Map<String, byte[]> all() {
    byte[] frames = smallFrames();
    List<Whole> files = files();
    Map<String, byte[]> out = new LinkedHashMap<>();
    out.put("VECTORS.txt", text(vectors(), frames, files, damage(files)).getBytes(StandardCharsets.US_ASCII));
    out.put("small.frames", frames);
    for (Whole w : files) out.put(w.name, w.bytes);
    return out;
  }

  static boolean main(String[] args) throws IOException {
    Path dir = Path.of(args[0]);
    Files.createDirectories(dir);
    for (Map.Entry<String, byte[]> e : all().entrySet()) {
      Files.write(dir.resolve(e.getKey()), e.getValue());
      System.out.printf("%s: %d bytes, SHA-256 %s%n", e.getKey(), e.getValue().length, PageImage.sha256(e.getValue()));
    }
    return true;
  }
}
