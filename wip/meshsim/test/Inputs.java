import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * The inputs of a sort run and their SHA-256 hashes. DEV 100 was two kit versions giving
 * different results: the laptop ran an older tile image than the container. Every SortTest log now carries one line
 *   inputs: meshsim.jar=<sha> image/loader.bin=<sha> ... hops.txt=<sha> keys-65536/<set>.keys=<sha>
 * naming every file the run reads by its kit-relative name, and the kit ships HASHES (sha256sum format, the same names)
 * as the reference set. SortSummary flags runs whose hashes differ from it; run.sh checks the kit itself at the start.
 *
 * Usage (the kit; plain Java, no sha256sum needed):
 *   java Inputs hash FILE ...           print "sha  FILE" lines (assemble.sh makes HASHES with it)
 *   java Inputs check [HASHES]          every file in HASHES that exists here must match; exit 1 if one differs
 */
final class Inputs {
    /** The tile image's files, in a fixed order (the kit's image/ directory). v1.2 (WORKER8): one image, no overlays; the
     *  interim loader is meshsim's (sw/build/loader.bin, PHASE-2 INTERIM), hashed as image/loader.bin. */
    static final String[] IMAGE = { "sort.code.bin", "sort.data.bin", "sort.sym" };

    static String sha256(Path p) throws IOException {
        try (InputStream in = Files.newInputStream(p)) {
            MessageDigest d = MessageDigest.getInstance("SHA-256");
            byte[] buf = new byte[1 << 16];
            for (int r; (r = in.read(buf)) > 0; ) d.update(buf, 0, r);
            StringBuilder s = new StringBuilder();
            for (byte b : d.digest()) s.append(String.format("%02x", b & 0xFF));
            return s.toString();
        } catch (NoSuchAlgorithmException e) { throw new IllegalStateException(e); }
    }

    /** The jar this class was loaded from, or null when running from a classes directory. */
    static Path jar() {
        try {
            Path p = Path.of(Inputs.class.getProtectionDomain().getCodeSource().getLocation().toURI());
            return Files.isRegularFile(p) && p.toString().endsWith(".jar") ? p : null;
        } catch (Exception e) { return null; }
    }

    /** name -> hash for a run: the jar, the image files present in build, hops.txt, and the key file (null: built-in keys).
     *  Names are kit-relative: meshsim.jar, image/<file>, hops.txt, <key dir>/<file>. A missing file hashes as "missing". */
    static Map<String, String> of(Path build, Path keys) {
        Map<String, String> m = new LinkedHashMap<>();
        Path j = jar();
        m.put("meshsim.jar", j == null ? "(classes)" : hash(j));
        m.put("image/loader.bin", hash(SortTest.loaderPath()));
        for (String f : IMAGE) m.put("image/" + f, hash(build.resolve(f)));
        m.put("hops.txt", hash(Path.of("hops.txt")));
        if (keys != null) {
            Path dir = keys.toAbsolutePath().normalize().getParent();
            m.put((dir == null ? "" : dir.getFileName() + "/") + keys.getFileName(), hash(keys));
        }
        return m;
    }
    private static String hash(Path p) {
        try { return Files.isRegularFile(p) ? sha256(p) : "missing"; } catch (IOException e) { return "unreadable"; }
    }

    /** The log line. */
    static String line(Map<String, String> m) {
        StringBuilder s = new StringBuilder("inputs:");
        for (var e : m.entrySet()) s.append(' ').append(e.getKey()).append('=').append(e.getValue());
        return s.toString();
    }

    /** Parses a log's inputs line back into name -> hash (null: not an inputs line). */
    static Map<String, String> parse(String l) {
        l = l.strip();
        if (!l.startsWith("inputs:")) return null;
        Map<String, String> m = new LinkedHashMap<>();
        for (String t : l.substring(7).trim().split("\\s+")) {
            int i = t.lastIndexOf('=');
            if (i > 0) m.put(t.substring(0, i), t.substring(i + 1));
        }
        return m;
    }

    /** Reads a HASHES file (sha256sum format: "sha  name" or "sha *name"; # comments) into name -> hash. */
    static Map<String, String> readHashes(Path f) throws IOException {
        Map<String, String> m = new LinkedHashMap<>();
        for (String l : Files.readAllLines(f)) {
            l = l.strip();
            if (l.isEmpty() || l.startsWith("#")) continue;
            String[] c = l.split("\\s+", 2);
            if (c.length == 2) m.put(c[1].startsWith("*") ? c[1].substring(1) : c[1], c[0].toLowerCase());
        }
        return m;
    }

    /** Compares a run's hashes with the reference: one entry per differing file, "name: run <sha12> ref <sha12>". Names
     *  the reference does not list (key sets at another n) are not compared. */
    static List<String> differences(Map<String, String> run, Map<String, String> ref) {
        List<String> d = new ArrayList<>();
        for (var e : run.entrySet()) {
            String r = ref.get(e.getKey());
            if (r != null && !r.equals(e.getValue())) d.add(e.getKey() + ": " + abbrev(e.getValue()) + " (reference " + abbrev(r) + ")");
        }
        return d;
    }
    static String abbrev(String h) { return h.length() == 64 ? h.substring(0, 12) + "..." : h; }

    public static void main(String[] a) throws IOException {
        if (a.length >= 1 && a[0].equals("hash")) {
            for (int i = 1; i < a.length; i++) System.out.println(sha256(Path.of(a[i])) + "  " + a[i]);
            return;
        }
        if (a.length >= 1 && a[0].equals("check")) {
            Path hf = Path.of(a.length > 1 ? a[1] : "HASHES");
            if (!Files.exists(hf)) { System.out.println("== inputs: no " + hf + ", not checked"); return; }
            int n = 0; List<String> bad = new ArrayList<>();
            for (var e : readHashes(hf).entrySet()) {
                Path p = Path.of(e.getKey());
                if (!Files.isRegularFile(p)) continue;          // key sets are made only when a run needs them
                n++;
                String h = sha256(p);
                if (!h.equals(e.getValue())) bad.add(e.getKey() + ": " + abbrev(h) + " (reference " + abbrev(e.getValue()) + ")");
            }
            if (bad.isEmpty()) System.out.println("== inputs: " + n + " files match " + hf);
            else {
                System.out.println("== WARNING: " + bad.size() + " of " + n + " files differ from " + hf + " (results will not be comparable):");
                for (String b : bad) System.out.println("     " + b);
                System.exit(1);
            }
            return;
        }
        System.err.println("usage: java Inputs hash FILE ... | java Inputs check [HASHES]");
        System.exit(2);
    }
}
