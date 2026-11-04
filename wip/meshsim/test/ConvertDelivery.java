import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Converts a v1.25 replay delivery (67-word state records) to v1.26 (DECISIONS-v1.26 items 1, 2, 8; WORKER19/NOTES.md),
 * without touching the streams. The source is only read; the converted delivery is written beside it:
 * DST/PROGRAM/ holds the converted records (every window_NNNNN.state*.bin and end.bin) and symbolic links to every other
 * source file (the streams, rows.bin, diag.bin, the tables), and DST/PROGRAM.manifest.json is the source manifest with
 * the converted files' sizes and SHA-256.
 *
 * <p>Per record: v1.25's word 0 (the template bitmap) moves to word 67; words 1-66 (state word, stream word, image)
 * become words 0-65; word 66 (files) is 0. The state word's [114:112] (the compression flags) move into the stream
 * word and are cleared; [115] (the store log) is kept; [116] (output rd_prev) is set. The stream word's bit lengths
 * become each stream's 128-bit words (ceil(bits / 128), checked against the stream file the flags name) and its values
 * out, taken from the window's raw record (state.bin): code bits / 32, log bits / 64, which a compressed form of the same
 * stream shares. end.bin (an end state, no streams) gets a zero stream word.
 *
 * <p>Usage: java ConvertDelivery SRC DST PROGRAM [--manifest M]  (M: default SRC/PROGRAM.manifest.json, else
 * SRC/manifest.json). Prints a summary and the new manifest's SHA-256.
 */
public final class ConvertDelivery {
    static final int V125_WORDS = 67;

    public static void main(String[] a) throws Exception {
        Path src = Path.of(a[0]).toAbsolutePath(), dst = Path.of(a[1]).toAbsolutePath();
        String prog = a[2];
        Path mf = null;
        for (int i = 3; i < a.length; i++) if (a[i].equals("--manifest")) mf = Path.of(a[++i]);
        if (mf == null) mf = Files.exists(src.resolve(prog + ".manifest.json")) ? src.resolve(prog + ".manifest.json") : src.resolve("manifest.json");
        long t0 = System.nanoTime();
        Stats st = convert(src, dst, prog, mf);
        System.out.printf("ConvertDelivery %s: %d windows, %d records converted, %d links; %s%n", prog, st.windows, st.records, st.links,
                st.errors.isEmpty() ? "OK" : st.errors.size() + " ERRORS");
        for (String e : st.errors.subList(0, Math.min(20, st.errors.size()))) System.out.println("  " + e);
        System.out.printf("values out: code %d, read log %d, write log %d; words raw %d, as picked %d (%.1f s)%n",
                st.vals[0], st.vals[1], st.vals[2], st.rawWords, st.pickWords, (System.nanoTime() - t0) / 1e9);
        System.out.println("manifest " + st.manifest + " sha256 " + ReplayTables.sha256(Files.readAllBytes(st.manifest)));
        System.exit(st.errors.isEmpty() ? 0 : 1);
    }

    static final class Stats { int windows, records, links; long rawWords, pickWords; final long[] vals = new long[3]; final List<String> errors = new ArrayList<>(); Path manifest; }

    @SuppressWarnings("unchecked")
    static Stats convert(Path src, Path dst, String prog, Path mf) throws Exception {
        Stats st = new Stats();
        Map<String, Object> m = ReplayDelivery.Json.object(Files.readString(mf));
        Path sp = src.resolve(prog), dp = dst.resolve(prog);
        Files.createDirectories(dp);
        // every source file: a link, unless it is a record (converted below); the records grouped by window, from one listing
        Map<Integer, List<String>> recs = new java.util.HashMap<>();
        try (var ls = Files.list(sp)) {
            for (Path f : (Iterable<Path>) ls::iterator) {
                String n = f.getFileName().toString();
                if (isRecord(n)) { recs.computeIfAbsent(Integer.parseInt(n.substring(7, n.indexOf('.'))), k -> new ArrayList<>()).add(n); continue; }
                Path to = dp.resolve(n);
                Files.deleteIfExists(to);
                Files.createSymbolicLink(to, f.toAbsolutePath());
                st.links++;
            }
        }
        for (Object po : ReplayDelivery.Json.list(m.get("programs"))) {
            Map<String, Object> p = ReplayDelivery.Json.object(po);
            if (!prog.equals(p.get("name"))) continue;
            for (Object wo : ReplayDelivery.Json.list(p.get("window_entries"))) {
                Map<String, Object> we = (Map<String, Object>) wo;
                if (!we.containsKey("files")) continue;
                st.windows++;
                int index = ((Number) we.get("index")).intValue();
                try { convertWindow(sp, dp, index, recs.getOrDefault(index, List.of()), (Map<String, Object>) we.get("files"), st); }
                catch (RuntimeException e) { st.errors.add("window " + index + ": " + e.getMessage()); }
            }
        }
        m.put("converted", Map.of("by", "ConvertDelivery (meshsim WORKER20), v1.25 -> v1.26 records (DECISIONS-v1.26)",
                "from_manifest_sha256", ReplayTables.sha256(Files.readAllBytes(mf))));
        st.manifest = dst.resolve(prog + ".manifest.json");
        Files.writeString(st.manifest, json(m, "") + "\n");
        return st;
    }

    static boolean isRecord(String n) { return n.matches("window_\\d+\\.(state[-a-z]*|end)\\.bin"); }

    /** One window: the raw record gives the value counts; every record variant and end.bin are rewritten. */
    static void convertWindow(Path sp, Path dp, int index, List<String> names, Map<String, Object> files, Stats st) throws Exception {
        String base = String.format("window_%05d.", index);
        ByteBuffer raw = read(sp.resolve(base + "state.bin"));
        long[] rl = lengths(raw);
        if (flags(raw) != 0) throw new IllegalStateException("state.bin has compression flags set: not the raw record");
        if (rl[0] % 32 != 0 || rl[1] % 64 != 0 || rl[2] % 64 != 0) throw new IllegalStateException("raw bit lengths not whole values: " + java.util.Arrays.toString(rl));
        int[] vals = { (int) (rl[0] / 32), (int) (rl[1] / 64), (int) (rl[2] / 64) };
        for (int k = 0; k < 3; k++) st.vals[k] += vals[k];
        String[] sn = { "code", "rlog", "wlog" };
        if (!names.contains(base + "state.bin")) throw new IllegalStateException("no " + base + "state.bin");
        {
            for (String n : names) {
                Path f = sp.resolve(n);
                ByteBuffer r = read(f);
                boolean end = n.endsWith(".end.bin");
                long[] len = lengths(r);
                int fl = flags(r);
                Replayer.StreamSize[] sz = new Replayer.StreamSize[3];
                for (int k = 0; k < 3; k++) {
                    if (end) { sz[k] = new Replayer.StreamSize(0, 0, false); continue; }
                    boolean c = ((fl >> k) & 1) != 0;
                    int words = (int) ((len[k] + 127) / 128);
                    long fileBytes = Files.size(sp.resolve(base + ReplayDelivery.streamFile(sn[k], c)));
                    if (fileBytes != 16L * words) throw new IllegalStateException(n + ": " + sn[k] + (c ? "-lru" : "") + " is " + fileBytes + " bytes, its " + len[k] + " bits need " + words + " words");
                    if (!c && len[k] != rl[k]) throw new IllegalStateException(n + ": raw " + sn[k] + " length " + len[k] + ", state.bin's " + rl[k]);
                    sz[k] = new Replayer.StreamSize(words, vals[k], c);
                    if (n.endsWith(".state.bin")) st.rawWords += words;
                    if (n.endsWith(".state-pick.bin")) st.pickWords += words;
                }
                byte[] out = convertRecord(r, sz);
                Files.deleteIfExists(dp.resolve(n));
                Files.write(dp.resolve(n), out);
                st.records++;
                Object key = files.containsKey(n) ? n : n.substring(base.length());
                Object e = files.get(key);
                if (e == null) throw new IllegalStateException(n + ": not in the manifest");
                String sha = ReplayTables.sha256(out);
                if (e instanceof Map<?, ?>) {
                    Map<String, Object> ne = new LinkedHashMap<>();
                    for (Map.Entry<?, ?> x : ((Map<?, ?>) e).entrySet()) ne.put((String) x.getKey(), x.getValue());
                    ne.put("sha256", sha);
                    if (ne.containsKey("bytes")) ne.put("bytes", (long) out.length);
                    files.put((String) key, ne);
                } else files.put((String) key, sha);
            }
        }
    }

    static ByteBuffer read(Path p) throws IOException {
        byte[] b = Files.readAllBytes(p);
        if (b.length != 16 * V125_WORDS) throw new IllegalStateException(p.getFileName() + ": " + b.length + " bytes, not a v1.25 record (1,072)");
        return ReplayDelivery.le(b);
    }
    /** v1.25's stream word (record word 2): code [23:0], read log [55:32], write log [87:64], in bits. */
    static long[] lengths(ByteBuffer r) {
        long lo = r.getLong(32), hi = r.getLong(40);
        return new long[] { lo & 0xFFFFFF, (lo >>> 32) & 0xFFFFFF, hi & 0xFFFFFF };
    }
    /** v1.25's state word [114:112], the compression flags (code, read log, write log). */
    static int flags(ByteBuffer r) { return (int) ((r.getLong(24) >>> 48) & 7); }

    /** v1.25 record -> v1.26 record. */
    static byte[] convertRecord(ByteBuffer r, Replayer.StreamSize[] sz) {
        long sHi = r.getLong(24);
        if ((sHi >>> 52) != 0) throw new IllegalStateException("state word [127:116] not 0: 0x" + Long.toHexString(sHi >>> 52));
        boolean store = ((sHi >>> 51) & 1) != 0;
        long hi = Replayer.stateHi((int) sHi, (int) (sHi >>> 32) & 0xFFFF, store, true);
        long[] regs = new long[128];
        for (int i = 0; i < 128; i++) regs[i] = r.getLong(48 + 8 * i);
        long[] rec = ReplayWindow.record(r.getLong(16), hi, Replayer.streamWord(sz[0], sz[1], sz[2]), regs, new long[2], new long[] { r.getLong(0), r.getLong(8) });
        rec[4] = regs[0];                                    // the image's register 0, as delivered
        ByteBuffer b = ReplayDelivery.le(new byte[8 * rec.length]);
        for (int i = 0; i < rec.length; i++) b.putLong(8 * i, rec[i]);
        return b.array();
    }

    // ================================================================ JSON out (objects keep their order)
    @SuppressWarnings("unchecked")
    static String json(Object o, String ind) {
        if (o instanceof Map<?, ?> m) {
            if (m.isEmpty()) return "{}";
            boolean flat = m.values().stream().noneMatch(v -> v instanceof Map || v instanceof List);
            StringBuilder s = new StringBuilder("{"); boolean f = true;
            for (Map.Entry<?, ?> e : m.entrySet()) {
                s.append(f ? "" : ",").append(flat ? (f ? "" : " ") : "\n" + ind + "  ");
                f = false;
                s.append(str((String) e.getKey())).append(": ").append(json(e.getValue(), ind + "  "));
            }
            return s.append(flat ? "" : "\n" + ind).append('}').toString();
        }
        if (o instanceof List<?> l) {
            if (l.isEmpty()) return "[]";
            boolean flat = l.stream().noneMatch(v -> v instanceof Map || v instanceof List);
            StringBuilder s = new StringBuilder("[");
            for (int i = 0; i < l.size(); i++) s.append(i > 0 ? "," : "").append(flat ? (i > 0 ? " " : "") : "\n" + ind + "  ").append(json(l.get(i), ind + "  "));
            return s.append(flat ? "" : "\n" + ind).append(']').toString();
        }
        if (o instanceof String s) return str(s);
        return String.valueOf(o);
    }
    static String str(String s) {
        StringBuilder b = new StringBuilder("\"");
        for (char c : s.toCharArray()) {
            switch (c) {
                case '"' -> b.append("\\\""); case '\\' -> b.append("\\\\"); case '\n' -> b.append("\\n"); case '\t' -> b.append("\\t"); case '\r' -> b.append("\\r");
                default -> { if (c < 0x20) b.append(String.format("\\u%04x", (int) c)); else b.append(c); }
            }
        }
        return b.append('"').toString();
    }
}
