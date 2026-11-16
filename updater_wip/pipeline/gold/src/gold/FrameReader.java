package gold;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * frames-read: prints every frame's fields, for a person to inspect; --check verifies the file against itself; --seg
 * checks frames 7, 8 and 9 against gold's v1.3 segment file for the same run. It decodes with FrameFile and counts
 * with Reference, never with the writer's code.
 */
public final class FrameReader {
  private FrameReader() {}

  static boolean main(String[] args) throws IOException {
    boolean check = false, brief = false;
    String seg = null;
    for (int i = 1; i < args.length; i++) {
      if (args[i].equals("--check")) check = true;
      else if (args[i].equals("--brief")) brief = true;
      else if (args[i].startsWith("--seg=")) seg = args[i].substring(6);
      else throw new IllegalArgumentException("unknown option " + args[i]);
    }
    byte[] bytes = Files.readAllBytes(Path.of(args[0]));
    FrameFile f = FrameFile.parse(bytes);
    print(f, bytes.length, brief);
    boolean ok = true;
    if (check) ok &= check(f);
    if (seg != null) ok &= againstSegmentFile(f, Files.readAllBytes(Path.of(seg)));
    return ok;
  }

  static final String[] FIELDS = {"log2_k", "lowest_address", "program_size", "max_trusted_advice_size",
      "trusted_advice_start", "trusted_advice_end", "max_untrusted_advice_size", "untrusted_advice_start",
      "untrusted_advice_end", "max_input_size", "max_output_size", "input_start", "input_end", "output_start",
      "output_end", "stack_size", "stack_end", "heap_size", "heap_end", "panic", "termination", "io_end"};

  static void print(FrameFile f, long size, boolean brief) {
    System.out.printf("frame file: %d bytes, %d frames%n", size, f.frames.size());
    for (FrameFile.Frame fr : f.frames) {
      System.out.printf("%nframe %d (%s) at byte %d: %d 128-bit words of data (%d bytes with its header)%n", fr.type,
          name(fr.type), fr.offset, fr.length128, 16 * (fr.length128 + 1));
      switch (fr.type) {
        case 1 -> {
          System.out.printf("  file length %d pages; %d pages listed%n", f.fileLength, f.programPages.size());
          if (!brief) for (long p : f.programPages.keySet()) System.out.printf("    offset %#x%n", p * 4096);
        }
        case 2 -> System.out.printf("  k %d (%d lines), p1 %#x, p3 %d%n", f.baseK, 1L << f.baseK, f.baseP1, f.baseP3);
        case 3 -> {
          System.out.printf("  format version %d%n  program stem %s%n  program id %s%n  working directory %s%n", f.version,
              f.stem, hex(f.programId), f.workdir);
          System.out.printf("  proof parameters, %d fields:%n", f.params.length);
          for (int i = 0; i < f.params.length; i++) {
            System.out.printf("    %-26s %#x%n", i < FIELDS.length ? FIELDS[i] : "field " + i, f.params[i]);
          }
          System.out.printf("  data image: %d 64-bit words, %d 128-bit words, %s, %d pages%n", f.dataWords, f.data128,
              f.dataLru ? "LRU" : "raw", f.dataPages);
          long total = 0;
          for (long n : f.segWords) total += n;
          System.out.printf("  memory segments: %d 64-bit words in all, %d 128-bit words, %s%n", total, f.seg128,
              f.segLru ? "LRU" : "raw");
          System.out.println("  per segment: 64-bit words, pages written; per ASIC: start, final, Tracer's copy pages");
          for (int s = 0; s < FrameFile.SEGMENTS; s++) {
            if (brief && f.segWords[s] == 0 && s > 0) continue;
            System.out.printf("    %3d  %9d words  %5d written   ASIC %6d %6d %6d%n", s, f.segWords[s], f.pagesWritten[s],
                f.perAsic[s][0], f.perAsic[s][1], f.perAsic[s][2]);
          }
        }
        case 4 -> System.out.printf("  base table \"%s\"%s%n  k %d (%d lines), p1 %#x, p3 %d%n  frame 5: %d 64-bit words, %d"
            + " 128-bit words, %s, %d pages%n", f.basePath, f.basePath.isEmpty() ? " (none: a full new table)" : "", f.k,
            1L << f.k, f.p1, f.p3, f.tableWords, f.table128, f.tableLru ? "LRU" : "raw", f.tablePages);
        case 5 -> runs(Reference.decode(fr.data, f.tableWords, f.tableLru), brief);
        case 6 -> runs(Reference.decode(fr.data, f.dataWords, f.dataLru), brief);
        case 7 -> {
          System.out.println("  segment: pc, instr_idx, next offset, advice values, log2 K, I/O region size");
          for (int s = 0; s < FrameFile.SEGMENTS; s++) {
            if (brief && s > 0 && f.recordBits(s, 0, 0, 63) == 0) continue;
            System.out.printf("    %3d  pc %#x  idx %d  off %d  advice %d  log2K %d  io %#x%n", s, f.recordBits(s, 0, 0, 63),
                f.recordBits(s, 0, 64, 95), f.recordBits(s, 0, 96, 111), f.recordBits(s, 1, 102, 124),
                f.recordBits(s, 66, 66, 71), f.recordBits(s, 66, 72, 102));
          }
        }
        case 8 -> {
          long total = 0;
          for (long n : f.adviceCounts) total += n;
          System.out.printf("  %d advice values in all; per segment:%s%n", total, compact(f.adviceCounts));
        }
        case 9 -> {
          long total = 0;
          for (long n : f.segWords) total += n;
          long[] all = Reference.decode(fr.data, total, f.segLru);
          int runs = 0;
          for (int i = 0; i < all.length; i += 1 + (int) (all[i] >>> 32)) runs++;
          System.out.printf("  %d words, %d runs, %s%n", all.length, runs, f.segLru ? "LRU" : "raw");
        }
        default -> System.out.println("  (unknown type)");
      }
    }
  }

  static void runs(long[] u, boolean brief) {
    int shown = 0, runs = 0;
    for (int i = 0; i < u.length; i += 1 + (int) (u[i] >>> 32)) {
      runs++;
      if (!brief || shown < 8) {
        System.out.printf("    run at word %d (byte %#x), %d words%n", u[i] & 0xffffffffL, 8 * (u[i] & 0xffffffffL), u[i] >>> 32);
        shown++;
      }
    }
    System.out.printf("  %d words, %d runs%n", u.length, runs);
  }

  static String compact(long[] a) {
    StringBuilder b = new StringBuilder();
    for (long x : a) b.append(' ').append(x);
    return b.toString();
  }

  static String name(int type) {
    String[] n = {"?", "ELF", "base jump table", "proof header", "jump table header", "jump table data", "data image",
        "register states", "advice logs", "memory segments"};
    return type >= 1 && type <= 9 ? n[type] : "?";
  }

  static String hex(byte[] b) {
    StringBuilder s = new StringBuilder();
    for (byte x : b) s.append(String.format("%02x", x));
    return s.toString();
  }

  /** The file against itself: every length, every count of frames 3 and 4, the advice counts' two places. */
  static boolean check(FrameFile f) {
    List<String> bad = new ArrayList<>();
    // the 128-bit lengths of the update-word frames, from the codes they hold
    long total = 0;
    for (long n : f.segWords) total += n;
    lengths(bad, "frame 5", f.get(5), f.tableWords, f.tableLru, f.table128);
    lengths(bad, "frame 6", f.get(6), f.dataWords, f.dataLru, f.data128);
    lengths(bad, "frame 9", f.get(9), total, f.segLru, f.seg128);
    // frame 7 is 128 records of 68 words, frame 8 the counts and the logs, frame 1 its pages
    if (f.get(7).length128 != 128 * 68) bad.add("frame 7: " + f.get(7).length128 + " 128-bit words, 8,704 expected");
    long adv = 32;
    for (long n : f.adviceCounts) adv += (n + 1) / 2;
    if (f.get(8).length128 != adv) bad.add("frame 8: " + f.get(8).length128 + " 128-bit words, " + adv + " from its counts");
    long f1 = (8 + 4104L * f.programPages.size() + 15) / 16;
    if (f.get(1).length128 != f1) bad.add("frame 1: " + f.get(1).length128 + " 128-bit words, " + f1 + " from its pages");
    if (f.hasBase && f.get(2).length128 != (12 + (128L << f.baseK) + 15) / 16) bad.add("frame 2's length");
    // the program id
    if (!hex(f.programId).equals(Reference.sha256(Arrays.copyOf(f.get(1).data, (int) (8 + 4104L * f.programPages.size()))))) {
      bad.add("frame 3's program id isn't the SHA-256 of frame 1's data");
    }
    // the advice counts: frame 7's word 1 and frame 8's table
    for (int s = 0; s < FrameFile.SEGMENTS; s++) {
      long w1 = f.recordBits(s, 1, 102, 124);
      if (w1 != f.adviceCounts[s]) bad.add("segment " + s + ": frame 7's advice field " + w1 + ", frame 8's count " + f.adviceCounts[s]);
    }
    // frame 7's word 66 against frame 3's parameters
    for (int s = 0; s < FrameFile.SEGMENTS; s++) {
      if (f.recordBits(s, 66, 66, 71) != f.params[0] || f.recordBits(s, 66, 72, 102) != 0x8000_0000L - f.params[1]) {
        bad.add("segment " + s + ": frame 7's word 66 doesn't match frame 3's log2 K and lowest_address");
      }
    }
    // every page count, by the reference's trees, for every s
    Reference r = new Reference(f);
    for (int s = 0; s < FrameFile.SEGMENTS; s++) {
      r.forSegment(s);
      List<String> b = r.checkCounts(s);
      if (s > 0) b.removeIf(x -> x.startsWith("jump table") || x.startsWith("data image"));
      bad.addAll(b);
    }
    for (String b : bad) System.out.println("CHECK " + b);
    System.out.printf("%ncheck: %s (lengths, counts of frames 3 and 4, page counts by copy-on-write trees for every s,"
        + " the advice counts in frames 7 and 8)%n", bad.isEmpty() ? "ok" : bad.size() + " FAILED");
    return bad.isEmpty();
  }

  static void lengths(List<String> bad, String what, FrameFile.Frame fr, long words, boolean lru, long header128) {
    long bytes;
    if (lru) {
      long[] used = new long[1];
      Reference.decodeLru(fr.data, words, used);
      bytes = (used[0] + 7) / 8;
      for (long i = used[0]; i < 8L * fr.data.length; i++) {
        if (Reference.bits(fr.data, i, 1) != 0) { bad.add(what + ": non-zero bits after the codes"); break; }
      }
    } else {
      Reference.decodeRaw(fr.data, words);
      bytes = 8 * words;
    }
    long need = (bytes + 15) / 16;
    if (fr.length128 != need) bad.add(what + ": " + fr.length128 + " 128-bit words, the codes need " + need);
    if (header128 != fr.length128) bad.add(what + ": the header says " + header128 + " 128-bit words, the frame has " + fr.length128);
  }

  /** Frames 7, 8 and 9 against gold's v1.3 segment file (section 16's 32-bit framing; frames 7, 1, 6, then per
   *  segment 2, 3, and 4 or 5): the same records, advice and update words. Segments past its count must be empty. */
  static boolean againstSegmentFile(FrameFile f, byte[] seg) {
    List<long[]> state = new ArrayList<>();
    List<byte[]> records = new ArrayList<>();
    List<long[]> advice = new ArrayList<>(), updates = new ArrayList<>();
    int pos = 0;
    while (pos < seg.length) {
      FrameFile.Cursor h = new FrameFile.Cursor(Arrays.copyOfRange(seg, pos, pos + 8), "segment file");
      int words = (int) h.u32(), type = (int) h.u32();
      byte[] data = Arrays.copyOfRange(seg, pos + 8, pos + 4 * words);
      pos += 4 * words;
      switch (type) {
        case 2 -> records.add(data);
        case 3 -> advice.add(Reference.decodeRaw(data, data.length / 8));
        case 4 -> updates.add(Reference.decodeRaw(data, data.length / 8));
        case 5 -> {
          FrameFile.Cursor c = new FrameFile.Cursor(data, "segment file frame 5");
          long n = c.u32();
          updates.add(Reference.decodeLru(Arrays.copyOfRange(data, 4, data.length), n, null));
        }
        default -> { }
      }
    }
    List<String> bad = new ArrayList<>();
    Reference r = new Reference(f);
    int n = records.size();
    if (n > FrameFile.SEGMENTS) bad.add("the segment file has " + n + " segments");
    for (int s = 0; s < Math.min(n, FrameFile.SEGMENTS); s++) {
      if (!Arrays.equals(records.get(s), f.records[s])) bad.add("segment " + s + ": frame 7's record differs from the segment file's frame 2");
      if (!Arrays.equals(advice.get(s), f.advice[s])) bad.add("segment " + s + ": frame 8's log differs from the segment file's frame 3");
      if (!Arrays.equals(updates.get(s), r.segments[s])) bad.add("segment " + s + ": frame 9's words differ from the segment file's frame 4/5");
    }
    for (int s = n; s < FrameFile.SEGMENTS; s++) {
      if (f.adviceCounts[s] != 0 || r.segments[s].length != 0 || f.recordBits(s, 0, 0, 63) != 0) {
        bad.add("segment " + s + ", past the segment file's " + n + ": not empty");
      }
    }
    for (String b : bad) System.out.println("SEG " + b);
    System.out.printf("against the segment file (%d segments): %s%n", n, bad.isEmpty() ? "ok" : bad.size() + " FAILED");
    state.clear();
    return bad.isEmpty();
  }
}
