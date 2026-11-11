import java.io.DataOutputStream;
import java.nio.file.Path;
import java.security.DigestOutputStream;
import java.security.MessageDigest;
import java.util.List;
import java.util.TreeSet;

/**
 * WORKER17 (v1.35, the chain; owner's Q1): `ref`'s memory at every segment's start, as TracerT2 and M3Seg carry it by
 * T1's path (the Tracer model and the ideal controller, on WORKER26's initial image and map): per segment, the ownership
 * set from the update list, then the start is recorded, then the cache cold, the record from frame 2, 64 windows, the
 * dirty lines written back (M3Seg's loop exactly; its snapshot is this start).
 *
 * Per segment one line:
 *   start S nextFree N pages P scan HEX sha256 HEX
 * scan is StateFpM3.filesScan (the fingerprint's tr.files sum); sha256 is over M3Inputs.files' stream uncompressed
 * ("M3FILES1", nextFree, the page count, every page in address order: the byte-for-byte form of W13's snapshot).
 * After each carried segment, T1's end check: the Tracer's end state against the next segment's frame 2.
 *   sram=S,S,..   also the landings before the boot of those segments, as TracerT2.chip and M3Seg make them (64
 *                 windows, raw): "sram S sha256 HEX" over M3Inputs.sram's stream uncompressed (needs the run directory:
 *                 hops.txt, sw/bootrom.hex).
 *   dump=S,S,..  dir=DIR  W13's snapshot form (DIR/NAME.SSS.files.gz) of those segments' starts.
 *
 * Usage: java -cp build T1Carry NAME --w26 DIR [--segments a-b] [sram=..] [dump=.. dir=DIR]
 */
public final class T1Carry {
    static String hex(byte[] b) { StringBuilder s = new StringBuilder(); for (byte x : b) s.append(String.format("%02x", x)); return s.toString(); }

    public static void main(String[] a) throws Exception {
        String name = a[0]; Path w26 = null; int s0 = 0, s1 = Integer.MAX_VALUE;
        TreeSet<Integer> sramAt = new TreeSet<>(), dumpAt = new TreeSet<>(); Path dir = null;
        for (int i = 1; i < a.length; i++) {
            if (a[i].equals("--w26")) w26 = Path.of(a[++i]);
            else if (a[i].equals("--segments")) { String[] r = a[++i].split("-"); s0 = Integer.parseInt(r[0]); s1 = Integer.parseInt(r[r.length - 1]); }
            else if (a[i].startsWith("sram=")) for (String x : a[i].substring(5).split(",")) sramAt.add(Integer.parseInt(x));
            else if (a[i].startsWith("dump=")) for (String x : a[i].substring(5).split(",")) dumpAt.add(Integer.parseInt(x));
            else if (a[i].startsWith("dir=")) dir = Path.of(a[i].substring(4));
            else throw new IllegalArgumentException(a[i]);
        }
        if (!dumpAt.isEmpty() && dir == null) throw new IllegalArgumentException("dump= needs dir=");
        long t0 = System.nanoTime();
        TracerRig g = new TracerRig();
        TracerT1.W26 in = TracerT1.loadW26(w26, g, name);
        g.image = g.copy = in.img; g.lowest = in.lowest; g.log2K = in.log2K;
        g.ms.mapRootForStats = g.map.root();
        g.loadTables();
        List<TracerT1.Seg> segs = TracerT1.readSegments(w26.resolve("segments.seg"), in.lowest);
        System.out.printf("%s: %d segments in segments.seg; image root %d depth %d, map root %d depth %d%n", name, segs.size(),
                in.img.root(), in.img.depth(), g.map.root(), g.map.depth());
        int bad = 0;
        for (int s = 0; s <= s1 && s < segs.size(); s++) {
            TracerT1.Seg sg = segs.get(s);
            TreeSet<Long> written = new TreeSet<>(); for (long[] u : sg.updates) written.add(u[0] / 512);
            if (in.written != null && !new TreeSet<>(in.written.get(s)).equals(written)) { System.out.printf("segment %d: seg/NNN.pages differs from the update list's pages%n", s); bad++; }
            int created = g.fs.setOwnership(in.img, written);
            if (s >= s0) {
                MessageDigest md = MessageDigest.getInstance("SHA-256");
                try (DataOutputStream d = new DataOutputStream(new java.io.BufferedOutputStream(new DigestOutputStream(java.io.OutputStream.nullOutputStream(), md), 1 << 16))) { M3Inputs.files(g.fs, d); }
                System.out.printf("start %d nextFree %d pages %d scan %x sha256 %s  (owned %d, created %d, updates %d, advice %d)%n", s, StateFp.l(g.fs, "nextFree"),
                        g.fs.pagesAllocated(), StateFpM3.filesScan(g.fs), hex(md.digest()), written.size(), created, sg.updates.size(), sg.advice.length);
                if (dumpAt.contains(s)) M3Inputs.files(g.fs, dir.resolve(String.format("%s.%03d.files.gz", name, s)));
                if (sramAt.contains(s)) System.out.printf("sram %d sha256 %s%n", s, sram(g, in, sg, s + 1 < segs.size() ? segs.get(s + 1) : null));
                System.out.flush();
            }
            if (s == s1 || s + 1 == segs.size()) break;
            g.ms.cold();
            long[] regs = new long[128]; System.arraycopy(sg.state, 3, regs, 0, 128);
            g.loadRecord(sg.state[0], (int) sg.state[1], (int) sg.state[2], regs, sg.advice);
            for (int j = 0; j < 64; j++) { Tracer.WindowOut o = g.window(65536, 40L * 65536 + 2_000_000); if (o == null || g.tr.stopped()) throw new IllegalStateException("T1 carry: segment " + s + " stopped: " + g.tr.errorLog); }
            g.tr.writeBackDirtyUntimed();
            long[] nx = segs.get(s + 1).state;
            boolean ok = g.tr.rec[0] == nx[0] && (int) g.tr.rec[1] == (int) nx[1] && ((g.tr.rec[1] >>> 32) & 0xFFFF) == nx[2];
            for (int r = 1; r < 128 && ok; r++) ok = g.tr.rec[4 + r] == nx[3 + r];
            if (!ok) { System.out.printf("segment %d: the end state differs from frame 2 of segment %d%n", s, s + 1); bad++; }
        }
        System.out.printf("%s: T1Carry %s, %.1f s%n", name, bad == 0 ? "OK" : "FAIL (" + bad + ")", (System.nanoTime() - t0) / 1e9);
        System.exit(bad == 0 ? 0 : 1);
    }

    /** The landings before the boot, as TracerT2.chip and M3Seg.chip make them (64 windows, raw); the stream's sha256. */
    static String sram(TracerRig g, TracerT1.W26 in, TracerT1.Seg sg, TracerT1.Seg next) throws Exception {
        int windows = 64, rc = 0;
        Config k = Mach.coresCfg(-1); k.cc3Tracer = true;
        Mach m = new Mach(k);
        ReplayTables tb = g.tables;
        byte[] tables = new byte[tb.decodeBytes.length + tb.romBytes.length];
        System.arraycopy(tb.decodeBytes, 0, tables, 0, tb.decodeBytes.length); System.arraycopy(tb.romBytes, 0, tables, tb.decodeBytes.length, tb.romBytes.length);
        BigSRAM s3 = m.c.cores.unit(2).sram;
        TracerT2.putBytes(s3, TracerT2.C_TABLES, tables); TracerT2.putBytes(s3, TracerT2.C_INDEX, tb.indexBytes); TracerT2.putBytes(s3, TracerT2.C_TEMPLATES, tb.templateBytes);
        long[] regs = new long[128]; System.arraycopy(sg.state, 3, regs, 0, 128);
        long[] rec = new long[2 * 68];
        rec[0] = sg.state[0]; rec[1] = (sg.state[1] & 0xFFFFFFFFL) | (sg.state[2] & 0xFFFF) << 32;
        rec[3] = (long) sg.advice.length << 38;
        System.arraycopy(regs, 0, rec, 4, 128);
        long[] fw = Tracer.filesWord(in.img.root(), in.img.depth(), g.map.root(), g.map.depth(), in.log2K, in.lowest);
        rec[132] = fw[0]; rec[133] = fw[1];
        TracerT2.put(s3, TracerT2.C_RECORD, rec);
        long[] adv = java.util.Arrays.copyOf(sg.advice, sg.advice.length + (sg.advice.length & 1));
        TracerT2.put(s3, TracerT2.C_ADVICE, adv);
        if (next != null) {
            long[] ex = new long[2 * 66];
            ex[0] = next.state[0]; ex[1] = (next.state[1] & 0xFFFFFFFFL) | (next.state[2] & 0xFFFF) << 32;
            System.arraycopy(next.state, 3, ex, 4, 128);
            TracerT2.put(s3, TracerT2.C_EXPECT, ex);
        }
        int[] h = new int[32];
        h[0] = TracerT2.TL_MAGIC; h[1] = TracerT2.TL_VERSION; h[2] = TracerT2.C_TABLES; h[3] = TracerT2.C_INDEX; h[4] = tb.indexBytes.length / 16; h[5] = TracerT2.C_TEMPLATES; h[6] = TracerT2.C_RECORD;
        h[7] = TracerT2.C_OWN; h[8] = next != null && windows == 64 ? TracerT2.C_EXPECT : 0; h[9] = TracerT2.C_ADVICE; h[10] = sg.advice.length; h[11] = 65536 | rc;
        h[12] = g.mapMul; h[13] = g.mapStep; h[14] = g.mapK; h[15] = windows;
        h[16] = StageB.RECORD; h[17] = StageB.CODE; h[18] = StageB.RLOG; h[19] = StageB.WLOG; h[20] = 0;
        for (int i = 0; i < 32; i++) s3.poke(TracerT2.C_HDR + i, h[i]);
        for (int t = 0; t < 64; t++) {
            Tile p = m.c.tiles[t];
            StageB.put(p, StageB.TABLES, tables); StageB.put(p, StageB.INDEX, tb.indexBytes); StageB.put(p, StageB.TEMPLATES, tb.templateBytes);
            int[] th = new int[16];
            th[0] = 0x594C5052; th[1] = 5; th[2] = StageB.TABLES; th[3] = StageB.INDEX; th[4] = tb.indexBytes.length / 16; th[5] = StageB.TEMPLATES; th[6] = StageB.RECORD;
            th[7] = StageB.CODE; th[8] = -1; th[9] = StageB.RLOG; th[10] = -1; th[11] = StageB.WLOG; th[12] = -1; th[13] = t < windows ? 1 : 0;
            th[14] = Replayer.runConfig(65536, true, true);
            th[15] = t >= windows ? 1 : 2;
            for (int i = 0; i < 16; i++) p.sram.poke(StageB.HDR + i, th[i]);
        }
        MessageDigest md = MessageDigest.getInstance("SHA-256");
        try (DataOutputStream d = new DataOutputStream(new java.io.BufferedOutputStream(new DigestOutputStream(java.io.OutputStream.nullOutputStream(), md), 1 << 16))) { M3Inputs.sram(m.c, d); }
        return hex(md.digest());
    }
}
