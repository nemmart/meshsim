package gold;

import java.util.HashSet;
import java.util.Set;

/**
 * A file's tree, counted (the writer's side): 4 KB data pages under 1,024-entry index pages, the depth following the
 * length (v1.36 spec 23.5; the fill port's capacity rule, line < 2^(5 + 10 depth)). The Reference builds the trees for
 * real and counts the pages it takes; the two share no code (owner, 2026-11-16).
 *
 * <p>The pages a run takes (HOST-FRAMES, "Files, lengths and page counts"; owner, 2026-11-16): the pages the Updater
 * takes from the free page queue, the data pages it writes and every index page above them, without the root, which
 * the file system's firmware makes before the run. Nothing when it writes nothing. A page written many times counts
 * once. ROOT_COUNTED is the one place that says whether the root is counted.
 */
public final class FileTree {
  /** The root is made by the firmware, not taken by the run (owner, 2026-11-16; was C11). */
  public static final boolean ROOT_COUNTED = false;
  public static final int FANOUT_BITS = 10;

  private FileTree() {}

  /** The smallest depth d >= 1 with 1,024^d >= the length in pages (choice C3: an index level always). */
  public static int depth(long lengthPages) {
    int d = 1;
    while ((1L << (FANOUT_BITS * d)) < lengthPages) d++;
    return d;
  }

  /** An image's length in pages (HOST-FRAMES: a16z's max_ram_k cells in bytes, in 4 KB pages; at least 1). */
  public static long imagePages(Layout l) {
    long maxRamK = ProofParams.nextPow2((l.heapEnd - l.lowestAddress()) / 8);
    return Math.max(1, maxRamK * 8 / Frames.PAGE_BYTES);
  }

  /** A jump table's length in pages: its 2^k lines of 128 bytes, at least 1. */
  public static long tablePages(int k) { return Math.max(1, (1L << (k + 7)) / Frames.PAGE_BYTES); }

  /** The pages a job writing these data pages takes in a file of this depth: the data pages and the index pages above. */
  public static long pagesTaken(Set<Long> dataPages, int depth) {
    if (dataPages.isEmpty()) return 0;
    long n = dataPages.size();
    for (int level = 0; level < depth; level++) {          // level 0 is the root
      Set<Long> index = new HashSet<>();
      for (long p : dataPages) index.add(p >>> (FANOUT_BITS * (depth - level)));
      n += index.size();
    }
    return ROOT_COUNTED ? n : n - 1;
  }
}
