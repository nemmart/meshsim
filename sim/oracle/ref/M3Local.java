import java.io.DataInputStream;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;

/**
 * ref's T2 chip from W13's two dumps instead of T1 and the deliveries (WORKER16): M3Seg.chip's machine (Config.ccCores,
 * cc3Tracer, Config's defaults), the Tracer's memory from M3Inputs.files, the harness's big-SRAM landings from
 * M3Inputs.sram, Mach.bootCores, CC_SEGMENT; run to point T (M3Seg's label: c.cycle - 1) and dump units word by word
 * (StateFpM3.dump), the C++ side's localisation target (the t2 tool's --dump-at T --fp-dump UNIT). D5: nothing in
 * sim/ref changes; compiled with ref's classes, its test classes (Mach) and StateFp, StateFpM3.
 *
 *   java -cp CLASSES M3Local FILES.gz SRAM.gz IMAGE_DIR T UNIT...     (run where sw/build holds cc1 and cc23)
 */
final class M3Local {
    static int i(DataInputStream d) throws Exception { return Integer.reverseBytes(d.readInt()); }
    static long l(DataInputStream d) throws Exception { return Long.reverseBytes(d.readLong()); }
    static DataInputStream in(Path p) throws Exception {
        return new DataInputStream(new java.io.BufferedInputStream(new java.util.zip.GZIPInputStream(Files.newInputStream(p), 1 << 16)));
    }
    @SuppressWarnings("unchecked")
    static FileStore files(Path p) throws Exception {
        try (DataInputStream d = in(p)) {
            byte[] m = new byte[8]; d.readFully(m);
            if (!new String(m).equals("M3FILES1")) throw new IllegalStateException("not M3FILES1");
            int next = i(d), n = i(d);
            FileStore fs = new FileStore(FileStore.PAGE_UNITS);
            HashMap<Integer, long[]> pages = (HashMap<Integer, long[]>) StateFp.o(fs, "pages");
            for (int k = 0; k < n; k++) { int a = i(d); long[] pg = new long[512]; for (int w = 0; w < 512; w++) pg[w] = l(d); pages.put(a, pg); }
            java.lang.reflect.Field f = FileStore.class.getDeclaredField("nextFree"); f.setAccessible(true); f.setInt(fs, next);
            return fs;
        }
    }
    static void sram(Chip ch, Path p) throws Exception {
        try (DataInputStream d = in(p)) {
            byte[] m = new byte[8]; d.readFully(m);
            if (!new String(m).equals("M3SRAM01")) throw new IllegalStateException("not M3SRAM01");
            while (true) {
                int u; try { u = i(d); } catch (java.io.EOFException e) { break; }
                int n = i(d);
                BigSRAM s = u < 64 ? ch.tiles[u].sram : ch.cores.unit(u - 64).sram;
                for (int k = 0; k < n; k++) { int c = i(d), v = i(d); s.poke(c, v); }
            }
        }
    }
    public static void main(String[] a) throws Exception {
        Config k = Mach.coresCfg(-1); k.cc3Tracer = true;
        Mach m = new Mach(k);
        Tracer tr = m.c.cores.tracer;
        MemorySystem ms = new MemorySystem(files(Path.of(a[0])));
        tr.mem = ms; tr.onStateWrite = ms::cold;
        sram(m.c, Path.of(a[1]));
        Path d = Path.of(a[2]);
        m.bootCores(new Mach.Image(d, "replay_ctl"), new Mach.Image(d, "tracer_ctl"));
        if (!m.run(m::coresBooted, 2_000_000)) throw new IllegalStateException("the boot didn't finish");
        m.cc1Command(0x54, 2, 1 << 24, 0x53 << 24);
        long t = Long.parseLong(a[3]);
        m.run(() -> m.c.cycle - 1 >= t, Long.MAX_VALUE / 4);
        PrintWriter w = new PrintWriter(System.out);
        for (int i = 4; i < a.length; i++) StateFpM3.dump(w, m.c, a[i], m.c.cycle - 1);
        w.flush();
    }
}
