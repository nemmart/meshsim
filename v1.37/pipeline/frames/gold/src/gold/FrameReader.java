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
    System.out.printf("frame file: %d bytes, %d frames, N = %d (from frame 7)%n", size, f.frames.size(), f.n);
    for (FrameFile.Frame fr : f.frames) {
      boolean baseFrame = fr == f.baseHeaderFrame || fr == f.baseDataFrame;
      System.out.printf("%nframe %d (%s%s) at byte %d: %d 128-bit words of data (%d bytes with its header)%s%n", fr.type,
          name(fr.type), baseFrame ? ", the base table on its own" : "", fr.offset, fr.length128, 16 * (fr.length128 + 1),
          fr.type == 2 || fr.type == 5 || fr.type == 6 || fr.type == 9 ? (fr.lru() ? ", LRU" : ", no compression") : "");
      switch (fr.type) {
        case 1 -> System.out.printf("  program stem %s%n  file length %d pages%n  frame 2: %d 64-bit words, %d 128-bit words,"
            + " %d pages%n", f.programStem, f.fileLength, f.programWords, f.program128, f.programPages);
        case 2 -> runs(Reference.decode(fr, f.programWords), brief);
        case 3 -> {
          System.out.printf("  format version %d%n  program stem %s%n  working directory %s%n", f.version, f.stem, f.workdir);
          System.out.printf("  proof parameters, %d fields:%n", f.params.length);
          for (int i = 0; i < f.params.length; i++) {
            System.out.printf("    %-26s %#x%n", i < FIELDS.length ? FIELDS[i] : "field " + i, f.params[i]);
          }
          System.out.printf("  data image: %d 64-bit words, %d 128-bit words, %d pages%n", f.dataWords, f.data128, f.dataPages);
          long total = 0;
          for (long x : f.segWords) total += x;
          System.out.printf("  memory segments: %d 64-bit words in all, %d 128-bit words%n", total, f.seg128);
          System.out.println("  per segment: 64-bit words, pages written; per ASIC: start, final, Tracer's copy pages");
          for (int s = 0; s < f.n; s++) {
            if (brief && f.segWords[s] == 0 && s > 0) continue;
            System.out.printf("    %3d  %9d words  %5d written   ASIC %6d %6d %6d%n", s, f.segWords[s], f.pagesWritten[s],
                f.perAsic[s][0], f.perAsic[s][1], f.perAsic[s][2]);
          }
        }
        case 4 -> {
          FrameFile.TableHeader t = baseFrame ? f.baseHeader : f.table;
          System.out.printf("  path \"%s\"%s%n  base table \"%s\"%s%n  k %d (%d lines), p1 %#x, p3 %d%n  frame 5: %d 64-bit"
              + " words, %d 128-bit words, %d pages%n", t.path, t.path.isEmpty() ? " (a run's table)" : "", t.base,
              t.base.isEmpty() ? " (none: a full new table)" : "", t.k, 1L << t.k, t.p1, t.p3, t.words, t.words128, t.pages);
        }
        case 5 -> runs(Reference.decode(fr, baseFrame ? f.baseHeader.words : f.table.words), brief);
        case 6 -> runs(Reference.decode(fr, f.dataWords), brief);
        case 7 -> {
          System.out.println("  segment: pc, instr_idx, next offset, advice values, log2 K, I/O region size");
          for (int s = 0; s < f.n; s++) {
            if (brief && s > 0 && f.recordBits(s, 0, 0, 63) == 0) continue;
            System.out.printf("    %3d  pc %#x  idx %d  off %d  advice %d  log2K %d  io %#x%n", s, f.recordBits(s, 0, 0, 63),
                f.recordBits(s, 0, 64, 95), f.recordBits(s, 0, 96, 111), f.recordBits(s, 1, 102, 124),
                f.recordBits(s, 66, 66, 71), f.recordBits(s, 66, 72, 102));
          }
        }
        case 8 -> {
          long total = 0;
          for (long x : f.adviceCounts) total += x;
          System.out.printf("  %d advice values in all; per segment:%s%n", total, compact(f.adviceCounts));
        }
        case 9 -> {
          long total = 0;
          for (long x : f.segWords) total += x;
          long[] all = Reference.decode(fr, total);
          int runs = 0;
          for (int i = 0; i < all.length; i += 1 + (int) (all[i] >>> 32)) runs++;
          System.out.printf("  %d words, %d runs%n", all.length, runs);
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
    String[] n = {"?", "program image header", "program image data", "proof header", "jump table header",
        "jump table data", "data image", "register states", "advice logs", "memory segments"};
    return type >= 1 && type <= 9 ? n[type] : "?";
  }

  /** The file against itself: every length, every count of frames 1, 3 and 4, the advice counts' two places. */
  static boolean check(FrameFile f) {
    List<String> bad = new ArrayList<>();
    long total = 0;
    for (long x : f.segWords) total += x;
    lengths(bad, "frame 2", f.programFrame, f.programWords, f.program128);
    if (f.baseHeader != null) lengths(bad, "the base table's frame 5", f.baseDataFrame, f.baseHeader.words, f.baseHeader.words128);
    lengths(bad, "frame 5", f.get(5), f.table.words, f.table.words128);
    lengths(bad, "frame 6", f.get(6), f.dataWords, f.data128);
    lengths(bad, "frame 9", f.get(9), total, f.seg128);
    if (!f.stem.equals(f.programStem)) bad.add("frame 3's program stem isn't frame 1's");
    // frame 8: the counts and the logs
    long adv = (4L * f.n + 15) / 16;
    for (long x : f.adviceCounts) adv += (x + 1) / 2;
    if (f.get(8).length128 != adv) bad.add("frame 8: " + f.get(8).length128 + " 128-bit words, " + adv + " from its counts");
    // the advice counts: frame 7's word 1 and frame 8's table
    for (int s = 0; s < f.n; s++) {
      long w1 = f.recordBits(s, 1, 102, 124);
      if (w1 != f.adviceCounts[s]) bad.add("segment " + s + ": frame 7's advice field " + w1 + ", frame 8's count " + f.adviceCounts[s]);
    }
    // frame 7's word 66 against frame 3's parameters
    for (int s = 0; s < f.n; s++) {
      if (f.recordBits(s, 66, 66, 71) != f.params[0] || f.recordBits(s, 66, 72, 102) != 0x8000_0000L - f.params[1]) {
        bad.add("segment " + s + ": frame 7's word 66 doesn't match frame 3's log2 K and lowest_address");
      }
    }
    // every page count, by the reference's trees, for every s
    Reference r = new Reference(f);
    for (int s = 0; s < f.n; s++) {
      r.forSegment(s);
      bad.addAll(r.checkCounts(s));
    }
    for (String b : bad) System.out.println("CHECK " + b);
    System.out.printf("%ncheck: %s (lengths, counts of frames 1, 3 and 4, page counts by copy-on-write trees for every s,"
        + " the advice counts in frames 7 and 8)%n", bad.isEmpty() ? "ok" : bad.size() + " FAILED");
    return bad.isEmpty();
  }

  static void lengths(List<String> bad, String what, FrameFile.Frame fr, long words, long header128) {
    long bytes;
    if (fr.lru()) {
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
    if (header128 != fr.length128) bad.add(what + ": the header frame says " + header128 + " 128-bit words, the frame has " + fr.length128);
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
    for (int s = f.n; s < n; s++) {                                  // past N: only segments past the run's end
      long pc = new FrameFile.Cursor(records.get(s), "segment file").u64();
      if (pc != 0 || advice.get(s).length != 0 || updates.get(s).length != 0) {
        bad.add("the segment file's segment " + s + " is past N = " + f.n + " and not empty");
      }
    }
    for (int s = 0; s < Math.min(n, f.n); s++) {
      if (!Arrays.equals(records.get(s), f.records[s])) bad.add("segment " + s + ": frame 7's record differs from the segment file's frame 2");
      if (!Arrays.equals(advice.get(s), f.advice[s])) bad.add("segment " + s + ": frame 8's log differs from the segment file's frame 3");
      if (!Arrays.equals(updates.get(s), r.segments[s])) bad.add("segment " + s + ": frame 9's words differ from the segment file's frame 4/5");
    }
    for (int s = n; s < f.n; s++) {
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
