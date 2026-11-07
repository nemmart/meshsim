import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

/** Offset table hops[k] = (b_k, a_k) (spec 4.6), supplied as configuration, never computed. */
final class Hops {
    final int[] b = new int[64], a = new int[64];
    private final boolean[] set = new boolean[64];

    static Hops load(Path file) throws IOException { return parse(Files.readString(file)); }

    static Hops parse(String text) {
        Hops h = new Hops();
        java.util.regex.Matcher m = java.util.regex.Pattern.compile("(\\d+)\\s*:\\s*\\[\\s*(-?\\d+)\\s*,\\s*(-?\\d+)\\s*\\]")
                .matcher(text.replaceAll("#[^\\n]*", ""));
        while (m.find()) {
            int k = Integer.parseInt(m.group(1));
            h.b[k] = Integer.parseInt(m.group(2)); h.a[k] = Integer.parseInt(m.group(3)); h.set[k] = true;
        }
        String bad = h.validate();
        if (bad != null) throw new IllegalArgumentException("invalid hops table: " + bad);
        return h;
    }

    String validate() {
        for (int k = 0; k < 64; k++) {
            if (!set[k]) return "hops[" + k + "] missing";
            int bk = b[k], ak = a[k];
            if (Math.abs(bk) > 4 || Math.abs(ak) > 4) return "hops[" + k + "] exceeds |4|";
            if (((8 * ak + bk - k) & 63) != 0) return "hops[" + k + "] does not satisfy k = 8a + b (mod 64)";
            int best = Integer.MAX_VALUE;
            for (int aa = -4; aa <= 4; aa++) for (int bb = -4; bb <= 4; bb++)
                if (((8 * aa + bb - k) & 63) == 0) best = Math.min(best, Math.abs(aa) + Math.abs(bb));
            if (Math.abs(ak) + Math.abs(bk) != best) return "hops[" + k + "] not minimal";
        }
        return null;
    }
}
