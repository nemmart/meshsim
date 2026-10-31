import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.HexFormat;
import java.util.List;
import java.util.Map;

/**
 * A replayer's tables as delivered (REPLAY-INPUT-REQUIREMENTS.md section 2, v1.25): the decode table, the ROM, the
 * controller-fed template words and their index, keyed by the <b>normalized template-index key</b> (wip/tables/README.md,
 * DECISIONS-v1.25 s5). v1.25's tables are universal (one set per jolt-cpp version, wip/tables/replayer/), so a delivery
 * may carry them per program or leave them to the harness.
 */
final class ReplayTables {
    final short[] table = new short[2048];
    final long[] romLo = new long[512], romHi = new long[512];
    /** The index, in template-number order (sorted by key): key, word offset, length, rd0, class. */
    int[] key = new int[0], offset = new int[0], length = new int[0], rd0 = new int[0], cls = new int[0];
    /** Every template's words, by template number: {lo, hi} per word. */
    final List<long[]> words = new ArrayList<>();
    byte[] decodeBytes, romBytes, templateBytes, indexBytes;
    Path dir;

    int count() { return key.length; }

    /**
     * key(w), the normalized template-index key: every bit of the presented code-stream word w except register fields.
     * Class 1, an inline (w[6:0] = 0001011): rd, rs1 and rs2 cleared. Class 2, CSRRW/CSRRS (w[6:0] = 1110011, funct3 1
     * or 2): rd and rs1 renamed by first appearance (x0 stays 0; a non-zero rd becomes 1; rs1 becomes 0 if x0, 1 if
     * equal to a non-zero rd or if rd is x0, else 2). Any other word is its own key.
     */
    static int key(int w) {
        int op = (w | 1) & 0x7F, f3 = (w >>> 12) & 7;
        if (op == 0x0B) return w & ~(31 << 7 | 31 << 15 | 31 << 20);
        if (op == 0x73 && (f3 == 1 || f3 == 2)) {
            int rd = (w >>> 7) & 31, rs1 = (w >>> 15) & 31;
            int nrd = rd == 0 ? 0 : 1;
            int nrs1 = rs1 == 0 ? 0 : (rd == 0 || rs1 == rd) ? 1 : 2;
            return w & ~(31 << 7 | 31 << 15) | nrd << 7 | nrs1 << 15;
        }
        return w;
    }

    /** The template number serving presented word w, or -1 (the index is sorted by key: a binary search). */
    int lookup(int w) {
        int k = key(w), lo = 0, hi = key.length - 1;
        while (lo <= hi) {
            int m = (lo + hi) >>> 1, c = Integer.compareUnsigned(key[m], k);
            if (c == 0) return m;
            if (c < 0) lo = m + 1; else hi = m - 1;
        }
        return -1;
    }

    /** The universal tables' place in the repository, from wip/meshsim (the harness's working directory). */
    static Path defaultDir() {
        String p = System.getProperty("replay.tables");
        return Path.of(p != null ? p : "../tables/replayer");
    }

    /** Load decode_table.bin, rom.bin, templates.bin and template_index.bin from dir. */
    static ReplayTables load(Path dir) throws IOException {
        ReplayTables t = new ReplayTables();
        t.dir = dir;
        t.decodeBytes = Files.readAllBytes(dir.resolve("decode_table.bin"));
        t.romBytes = Files.readAllBytes(dir.resolve("rom.bin"));
        t.templateBytes = Files.readAllBytes(dir.resolve("templates.bin"));
        t.indexBytes = Files.readAllBytes(dir.resolve("template_index.bin"));
        if (t.decodeBytes.length != 4096 || t.romBytes.length != 8192 || t.indexBytes.length % 16 != 0 || t.templateBytes.length % 16 != 0)
            throw new IOException(dir + ": table sizes " + t.decodeBytes.length + ", " + t.romBytes.length + ", " + t.indexBytes.length + ", " + t.templateBytes.length);
        ByteBuffer d = le(t.decodeBytes), r = le(t.romBytes), w = le(t.templateBytes), x = le(t.indexBytes);
        for (int e = 0; e < 2048; e++) t.table[e] = d.getShort(2 * e);
        for (int j = 0; j < 512; j++) { t.romLo[j] = r.getLong(16 * j); t.romHi[j] = r.getLong(16 * j + 8); }
        int n = t.indexBytes.length / 16;
        t.key = new int[n]; t.offset = new int[n]; t.length = new int[n]; t.rd0 = new int[n]; t.cls = new int[n];
        for (int k = 0; k < n; k++) {
            t.key[k] = x.getInt(16 * k); t.offset[k] = x.getInt(16 * k + 4); t.length[k] = x.getInt(16 * k + 8);
            t.rd0[k] = x.get(16 * k + 12) & 0xFF; t.cls[k] = x.get(16 * k + 13) & 0xFF;
            if (k > 0 && Integer.compareUnsigned(t.key[k - 1], t.key[k]) >= 0) throw new IOException(dir + ": index not sorted by key at " + k);
            long[] tw = new long[2 * t.length[k]];
            for (int i = 0; i < t.length[k]; i++) { tw[2 * i] = w.getLong(16 * (t.offset[k] + i)); tw[2 * i + 1] = w.getLong(16 * (t.offset[k] + i) + 8); }
            if (t.length[k] > 0 && ((tw[0] >>> 32) & 0xFFF) != t.length[k] - 1) throw new IOException(dir + ": template " + k + "'s length is not its first seq_remaining + 1");
            t.words.add(tw);
        }
        return t;
    }

    /** Check every table file against a manifest.json's {"files": {"replayer/x.bin": {"sha256": ...}}} (wip/tables). */
    static List<String> checkManifest(Path dir, Path manifest, String prefix) throws Exception {
        List<String> bad = new ArrayList<>();
        Map<String, Object> m = ReplayDelivery.Json.object(Files.readString(manifest));
        Map<String, Object> files = ReplayDelivery.Json.object(m.get("files"));
        for (String f : new String[] { "decode_table.bin", "rom.bin", "templates.bin", "template_index.bin" }) {
            Object e = files.get(prefix + f);
            if (e == null) { bad.add(f + ": not in " + manifest); continue; }
            String want = (String) ReplayDelivery.Json.object(e).get("sha256"), got = sha256(Files.readAllBytes(dir.resolve(f)));
            if (!want.equals(got)) bad.add(f + ": sha256 " + got + ", manifest " + want);
        }
        return bad;
    }

    static String sha256(byte[] b) throws Exception { return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(b)); }
    static ByteBuffer le(byte[] b) { return ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN); }
}
