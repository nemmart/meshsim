package gold;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.Locale;

/**
 * packets-read: reads a packet file, checks every packet (PacketFile) and writes the frames back out. It prints what
 * it found: the packets, the words, the frames and the overhead against the frames. A file that fails a check is
 * refused with one line naming the check, and nothing is written.
 *
 * <pre>
 *   packets-read &lt;packet file&gt; [&lt;frame file out&gt;] [--compare=&lt;frame file&gt;] [--first=N] [--no-idle] [--packets-only]
 *                [--list]
 * </pre>
 */
public final class PacketReader {
  private PacketReader() {}

  static boolean main(String[] args) throws IOException {
    PacketFile.Options o = new PacketFile.Options();
    String out = null, compare = null;
    for (int i = 1; i < args.length; i++) {
      if (args[i].startsWith("--compare=")) compare = args[i].substring(10);
      else if (args[i].startsWith("--first=")) o.first = Integer.decode(args[i].substring(8));   // the link's one run (28.4)
      else if (args[i].equals("--no-idle")) o.noIdle = true;
      else if (args[i].equals("--packets-only")) o.frames = false;
      else if (args[i].equals("--list")) o.list = true;
      else if (args[i].startsWith("--") || out != null) throw new IllegalArgumentException("unknown option " + args[i]);
      else out = args[i];
    }
    if (!o.frames && (out != null || compare != null)) throw new IllegalArgumentException("--packets-only reads no frames");
    byte[] file = Files.readAllBytes(Path.of(args[0]));
    PacketFile p;
    try {
      p = PacketFile.read(file, o);
    } catch (PacketFile.Refused r) {
      list(r.before);
      System.out.println(r.getMessage());
      return false;
    }
    list(p);
    print(p, o.frames);
    if (compare != null) {
      byte[] want = Files.readAllBytes(Path.of(compare));
      long d = firstDifference(p.frameBytes, want);
      if (d >= 0) {
        System.out.printf(Locale.ROOT, "REFUSED compare: the frames are %,d bytes, %s is %,d; the first difference is at byte %,d%n",
            p.frameBytes.length, compare, want.length, d);
        return false;
      }
      System.out.printf("the frames are byte-identical to %s%n", compare);
    }
    if (out != null) Files.write(Path.of(out), p.frameBytes);
    System.out.println("checks: ok");
    return true;
  }

  static long firstDifference(byte[] a, byte[] b) {
    int n = Math.min(a.length, b.length);
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return i;
    return a.length == b.length ? -1 : n;
  }

  static void print(PacketFile p, boolean frames) {
    System.out.printf(Locale.ROOT, "packet file: %,d bytes: %,d packets on stream 2%s, %,d on stream 1%s; %,d of length 0; %,d idle"
        + " bytes%n", p.bytes, p.onStream[2], numbers(p, 2), p.onStream[1], numbers(p, 1), p.empty, p.idleBytes);
    long frameBytes = 16 * p.words, over = p.bytes - frameBytes;
    if (frames) {
      System.out.printf(Locale.ROOT, "data: %,d words in %,d frames; %,d packets of 32 words, %,d shorter (%,d a frame's last)%n",
          p.words, p.frames, p.full, p.shortLast + p.shortInside, p.shortLast);
    } else {
      System.out.printf(Locale.ROOT, "data: %,d words on stream 2; the frames not looked at%n", p.words);
    }
    if (p.flagged != 0 || p.acknowledged != 0 || p.hopped != 0 || p.foregroundWords != 0) {
      System.out.printf(Locale.ROOT, "flags set on %,d packets, acknowledge on %,d, hops on %,d; %,d words on stream 1, kept nowhere%n",
          p.flagged, p.acknowledged, p.hopped, p.foregroundWords);
    }
    System.out.printf(Locale.ROOT, "against the frames: %,d bytes; overhead %,d bytes (%s): %,d header words, %,d idle bytes%s; the"
        + " frames' SHA-256 %s%n", frameBytes, over, frameBytes == 0 ? "no frames" : String.format(Locale.ROOT, "%.3f%%",
        100.0 * over / frameBytes), p.packets, p.idleBytes, p.foregroundWords == 0 ? "" : String.format(Locale.ROOT, ", %,d bytes"
        + " of stream 1's data", 16 * p.foregroundWords), frames ? sha256(p.frameBytes) : "not taken");
  }

  static String numbers(PacketFile p, int stream) {
    if (p.onStream[stream] == 0) return "";
    return String.format(" (numbers %d to %d%s)", p.firstNumber[stream], p.lastNumber[stream],
        p.wraps[stream] == 0 ? "" : ", passing 0x3FFFE " + (p.wraps[stream] == 1 ? "once" : p.wraps[stream] + " times"));
  }

  static void list(PacketFile p) {
    long k = 0;
    for (PacketFile.Packet q : p.list) {
      System.out.printf(Locale.ROOT, "packet %,d at byte %,d: stream %d, number %d, length %d, flags %d, acknowledge %d, hops %d, data"
          + " checksum 0x%08x, header CRC 0x%08x, stop bits %d%s%n", ++k, q.at, q.stream, q.number, q.length, q.flags, q.acknowledge, q.hops,
          q.dataChecksum, q.headerCrc, q.stops, q.frame == 0 ? "" : ", frame " + q.frame);
    }
  }

  static String sha256(byte[] b) {
    try {
      StringBuilder s = new StringBuilder();
      for (byte x : MessageDigest.getInstance("SHA-256").digest(b)) s.append(String.format("%02x", x));
      return s.toString();
    } catch (java.security.NoSuchAlgorithmException e) {
      throw new IllegalStateException(e);
    }
  }
}
