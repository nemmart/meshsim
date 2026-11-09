import java.io.DataOutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import java.util.TreeMap;

/**
 * WORKER13 (sim/tests/AUDIT-M3.md section 7): the inputs the framework's T2 harness loads, as `ref` built them (read
 * only, between cycles):
 *  - the memory at the segment's start (`FileStore`, the snapshot T1's carry left): "M3FILES1", nextFree, the page count,
 *    then every page in address order: its address (128 B units, int) and its 512 longs; little-endian, gzipped;
 *  - the harness's big-SRAM landings before the boot (C&C 3's tracer_layout.h, every tile's replay_layout.h): "M3SRAM01",
 *    then per unit 0-66 with a non-zero word: the unit, the count, then (chunk, value) pairs in chunk order; ints, little-
 *    endian, gzipped. Units 64-66 are C&C 1-3.
 */
final class M3Inputs {
    static DataOutputStream out(Path p) throws Exception { return new DataOutputStream(new java.io.BufferedOutputStream(new java.util.zip.GZIPOutputStream(Files.newOutputStream(p), 1 << 16))); }
    static void i(DataOutputStream d, int v) throws Exception { d.writeInt(Integer.reverseBytes(v)); }
    static void l(DataOutputStream d, long v) throws Exception { d.writeLong(Long.reverseBytes(v)); }

    @SuppressWarnings("unchecked")
    static void files(FileStore fs, Path p) throws Exception {
        Map<Integer, long[]> pages = new TreeMap<>((Map<Integer, long[]>) StateFp.o(fs, "pages"));
        try (DataOutputStream d = out(p)) {
            d.writeBytes("M3FILES1"); i(d, (int) StateFp.l(fs, "nextFree")); i(d, pages.size());
            for (var e : pages.entrySet()) { i(d, e.getKey()); for (long x : e.getValue()) l(d, x); }
        }
    }
    static void sram(Chip ch, Path p) throws Exception {
        try (DataOutputStream d = out(p)) {
            d.writeBytes("M3SRAM01");
            for (int u = 0; u < 67; u++) {
                BigSRAM s = u < 64 ? ch.tiles[u].sram : ch.cores.unit(u - 64).sram;
                int[][] data = (int[][]) StateFp.o(s, "data");
                int n = 0; for (int[] a : data) if (a != null) for (int v : a) if (v != 0) n++;
                if (n == 0) continue;
                i(d, u); i(d, n);
                for (int a = 0; a < data.length; a++) if (data[a] != null) for (int k = 0; k < data[a].length; k++) if (data[a][k] != 0) { i(d, (a << 17) | k); i(d, data[a][k]); }
            }
        }
    }
}
