package gold;

/** Locates the first difference between two segment files by frame (for test reports). */
final class FrameDiff {
  private FrameDiff() {}

  static int u32(byte[] b, int at) {
    return (b[at] & 0xff) | (b[at + 1] & 0xff) << 8 | (b[at + 2] & 0xff) << 16 | (b[at + 3] & 0xff) << 24;
  }

  /** null if identical; else where they first differ: byte offset, frame number and type, segment, offset in data. */
  static String firstDifference(byte[] mine, byte[] ref) {
    int n = Math.min(mine.length, ref.length), at = 0;
    while (at < n && mine[at] == ref[at]) at++;
    if (at == n && mine.length == ref.length) return null;
    int pos = 0, frame = 0;
    while (pos + 8 <= ref.length) {
      int words = u32(ref, pos), type = u32(ref, pos + 4);
      if (words < 2) break;
      if (at < pos + 4L * words) {
        int segment = frame == 0 ? -1 : (frame - 1) / 3;
        return String.format("byte %d (mine %d bytes, reference %d): frame %d (type %d, %d words%s), data byte %d",
            at, mine.length, ref.length, frame, type, words, segment >= 0 ? ", segment " + segment : "", at - pos - 8);
      }
      pos += 4 * words;
      frame++;
    }
    return String.format("byte %d (mine %d bytes, reference %d), past the reference's frames", at, mine.length, ref.length);
  }
}
