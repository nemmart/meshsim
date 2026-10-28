import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Random;

/**
 * The digit all-to-all as a group run on the 16-group schedule (schedule-4.md), driven from Java (the controller is
 * M4): data sets as v3.10's AllToAllTest (uniform: Data(seed 7, n), records sorted by destination, (d << 58 | s << 52 | k))
 * or a stream matrix (design_v2/python/schedule-4-model/mats, copied to data/). Placement: send buffer at chunk 0
 * (area 0), receive buffer at 0x40000 (area 2).
 */
final class A2A {
    static final int[][] G4 = { { 1, 29, 35, 63 }, { 2, 30, 34, 62 }, { 3, 31, 33, 61 }, { 5, 23, 41, 59 }, { 6, 22, 42, 58 },
            { 7, 21, 43, 57 }, { 8, 28, 36, 56 }, { 9, 27, 37, 55 }, { 10, 26, 38, 54 }, { 11, 25, 39, 53 }, { 12, 24, 40, 52 },
            { 13, 15, 49, 51 }, { 14, 18, 46, 50 }, { 16, 20, 44, 48 }, { 17, 19, 45, 47 }, { 0, 4, 32, 60 } };
    static final int SRC = 0, INBOX = 0x40000;
    final int[][] cnt = new int[64][64];
    final long[][] rec = new long[64][];
    final int[] R = new int[16];
    final int[][] dstoff = new int[64][64];

    /** v3.10 AllToAllTest.Data(seed, n, ragged = false): the same generator, so the same records. */
    A2A(long seed, int n) {
        Random r = new Random(seed);
        for (int s = 0; s < 64; s++) {
            int np = n; int[] dst = new int[np]; int[] w = new int[64]; int wsum = 0;
            for (int d = 0; d < 64; d++) { w[d] = 1; wsum += w[d]; }
            for (int k = 0; k < np; k++) { int x = r.nextInt(wsum), d = 0; while (x >= w[d]) { x -= w[d]; d++; } dst[k] = d; cnt[s][d]++; }
            long[] recs = new long[np]; int[] pos = new int[64];
            for (int d = 1; d < 64; d++) pos[d] = pos[d - 1] + cnt[s][d - 1];
            for (int k = 0; k < np; k++) recs[pos[dst[k]]++] = ((long) dst[k] << 58) | ((long) s << 52) | k;
            rec[s] = recs;
        }
        finish();
    }
    /** A stream matrix: 64 lines of 64 counts (s -> d). */
    A2A(Path matrix) throws java.io.IOException {
        List<String> lines = Files.readAllLines(matrix);
        for (int s = 0; s < 64; s++) {
            String[] f = lines.get(s).trim().split("\\s+");
            int np = 0; for (int d = 0; d < 64; d++) { cnt[s][d] = Integer.parseInt(f[d]); np += cnt[s][d]; }
            long[] recs = new long[np]; int i = 0;
            for (int d = 0; d < 64; d++) for (int k = 0; k < cnt[s][d]; k++, i++) recs[i] = ((long) d << 58) | ((long) s << 52) | i;
            rec[s] = recs;
        }
        finish();
    }
    private void finish() {
        for (int g = 0; g < 16; g++) for (int s = 0; s < 64; s++) for (int k : G4[g]) R[g] = Math.max(R[g], cnt[s][(s + k) & 63]);
        for (int d = 0; d < 64; d++) for (int s = 1; s < 64; s++) dstoff[s][d] = dstoff[s - 1][d] + cnt[s - 1][d];
    }
    int expected(int d) { int e = 0; for (int s = 0; s < 64; s++) e += cnt[s][d]; return e; }
    long bound(int gap) { long b = 0; for (int r : R) b += 4L * r + gap; return b; }
    int first(int s, int d) { int f = 0; for (int x = 0; x < d; x++) f += cnt[s][x]; return f; }

    /** Load the send buffers, the stream and group tables, PTR and the counters on every tile, and DMA_CONFIG. */
    void load(Chip c, int gap) {
        for (Tile p : c.tiles) {
            int s = p.idx;
            for (int k = 0; k < rec[s].length; k++) p.sram.poke64(SRC + 2 * k, rec[s][k]);
            for (int g = 0; g < 16; g++) {
                for (int i = 0; i < 4; i++) {
                    int off = G4[g][i], d = (s + off) & 63;
                    p.setStream(4 * g + i, Fields.streamSrc(SRC + 2 * first(s, d)), Fields.streamDest(0, off), Fields.streamCount(cnt[s][d], LinkWord.M_SRC, Fields.SIZE_64, 0));
                }
                p.setGroup(g, Fields.groupLo(R[g]), Fields.groupHi(4 * g, 4, gap));
            }
            for (int src = 0; src < 64; src++) p.setPtr(src, INBOX + 2 * dstoff[src][s]);
            p.dir.remaining[TrafficDirector.SRAM] = expected(s);
            p.setMasterConfig(Fields.masterConfig(Fields.RUN_GROUPS, 0, 16, 0, 0, 1));
        }
    }

    /** Records that did not land where they should (0 = correct). */
    int verify(Chip c) {
        int bad = 0;
        for (int d = 0; d < 64; d++) {
            Tile p = c.tiles[d];
            for (int s = 0; s < 64; s++) {
                int base = INBOX + 2 * dstoff[s][d], f = first(s, d);
                for (int k = 0; k < cnt[s][d]; k++) if (p.sram.peek64(base + 2 * k) != rec[s][f + k]) bad++;
            }
        }
        return bad;
    }

    /** Start every tile in one cycle (+ its start skew), run until every tile's arrivals are in memory. Returns the
     *  cycles from the start store to the last tile's wake (v1.0: the lead-in, 4 ts_engine_latency + 2, included). */
    static long run(Chip c, long max) {
        long t0 = c.cycle;
        boolean[] started = new boolean[64];
        long end = -1;
        for (long i = 0; i < max; i++) {
            for (Tile p : c.tiles) if (!started[p.idx] && c.cycle >= t0 + c.startSkew[p.idx]) { p.pulse(Fields.PULSE_START); started[p.idx] = true; }
            c.step();
            boolean all = true;
            for (Tile p : c.tiles) if (!(p.ag.done && p.dir.remaining[TrafficDirector.SRAM] == 0 && p.memIdle(c.cycle))) { all = false; break; }
            if (all) { end = c.cycle; break; }
        }
        c.scanOverflows();
        return end < 0 ? -1 : end - t0;
    }
}
