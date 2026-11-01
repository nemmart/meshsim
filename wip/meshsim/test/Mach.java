import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.Map;
import java.util.function.BooleanSupplier;

/**
 * Test harness for M4 onward: a machine as specified (Config.machine(): every tile halted at reset, controllers on)
 * with C&C booting program images built by sw/build.sh, and helpers to read results back.
 */
final class Mach {
    static final Path SW = Path.of("sw/build");
    final Chip c;
    final Config cfg;
    long bootStart, runAt = -1;

    Mach(Config cfg) { this.cfg = cfg; c = new Chip(cfg); }
    static Config cfg(long shuffle) { Config k = Config.machine(); k.shuffleSeed = shuffle; return k; }

    // ---------------------------------------------------------------- images
    static final class Image {
        final String name; final int[] code, data; final Map<String, Integer> sym = new HashMap<>();
        Image(String name) {
            this.name = name;
            try {
                code = words(SW.resolve(name + ".code.bin"));
                data = words(SW.resolve(name + ".data.bin"));
                for (String l : Files.readAllLines(SW.resolve(name + ".sym"))) {
                    String[] f = l.trim().split("\\s+");
                    if (f.length == 3) sym.put(f[2], (int) Long.parseLong(f[0], 16));
                }
            } catch (IOException e) { throw new RuntimeException("image " + name + " (run sw/build.sh): " + e.getMessage()); }
        }
        int sym(String s) { Integer v = sym.get(s); if (v == null) throw new IllegalArgumentException(name + ": no symbol " + s); return v; }
    }
    static int[] words(Path p) throws IOException {
        byte[] b = Files.readAllBytes(p);
        int[] w = new int[(b.length + 3) / 4];
        for (int i = 0; i < b.length; i++) w[i >> 2] |= (b[i] & 0xFF) << (8 * (i & 3));
        return w;
    }

    // ---------------------------------------------------------------- boot (6.2)
    /** C&C 1's boot of the tiles (6.2): every tile waits in the boot ROM (done pulsed); C&C 1 broadcasts the image by
     *  uncounted 128-bit mode-0 writes (code into program memory, data into data memory), then the first command
     *  (CMD_JUMP, slot 1 the entry); "command landed" raises READY; at all 64 READY, GO; the ROM jumps. */
    Image boot(String name) {
        Image im = new Image(name);
        bootStart = c.cycle;
        c.cc.loadWords(LinkWord.BROADCAST, Registers.PMEM_BASE, im.code)
            .loadWords(LinkWord.BROADCAST, im.sym("_data_start"), im.data)
            .command(LinkWord.BROADCAST, Registers.CMD_JUMP << 24, im.sym("_start"))
            .runWhenReady()
            .call(() -> runAt = c.cc.lastRunAt);
        return im;
    }
    // ---------------------------------------------------------------- boot with C&C 1-3 as cores (WORKER14, Config.ccCores)
    static Config coresCfg(long shuffle) { Config k = cfg(shuffle); k.ccCores = true; return k; }
    /** C&C 1's commands from the harness (sw/cc/cc1.c, W14-04). */
    static final int CC_PHASE = 0x50, CC_GO = 0x47, CC_EXPECT = 0x45, CC_EXIT = 0x58, CC_WATCHDOG = 0x57, CC_UART = 0x55;
    /** Chunk addresses in C&C 1's big SRAM where the harness stages the images C&C 1 sends (W14-03). */
    static final int STAGE_CHUNK = 0x10000;
    Image cc1, cc23;
    /** The outside world's boot (6.2, W14-02, W14-03): the images C&C 1 sends into its big SRAM (back door: staged by the
     *  host before the boot), its boot descriptor into its image's data, then C&C 1's image and first command through
     *  the host port. C&C 1's firmware boots C&C 2 and C&C 3, waits for their "started", then boots the tiles with
     *  `tile` (READY, GO). Returns the tile image. */
    Image bootCores(String tile) {
        if (c.cores == null) throw new IllegalStateException("bootCores needs Config.ccCores");
        Image t = new Image(tile); cc1 = new Image("cc1"); cc23 = new Image("cc23");
        bootStart = c.cycle;
        BigSRAM s = c.cores.unit(0).sram;
        int[] at = { STAGE_CHUNK };
        java.util.function.Function<int[], int[]> stage = w -> {           // {chunk, packets}: 128-bit rows, zero-padded
            int ch = at[0], pk = (w.length + 3) / 4;
            for (int i = 0; i < 4 * pk; i++) s.poke(ch + i, i < w.length ? w[i] : 0);
            at[0] += 4 * Math.max(pk, 1); return new int[] { ch, pk };
        };
        int[] desc = new int[1 + 3 * 6 + 1];
        desc[0] = 0xB007DE5C;
        int[] cp = stage.apply(cc23.code), cd = stage.apply(cc23.data), tp = stage.apply(t.code), td = stage.apply(t.data);
        for (int u = 0; u < 3; u++) {
            Image im = u < 2 ? cc23 : t; int[] p = u < 2 ? cp : tp, d = u < 2 ? cd : td;
            int cmd = at[0]; at[0] += 4;
            int[] e = { p[0], p[1], d[0], d[1], cmd, im.sym("_start") };
            System.arraycopy(e, 0, desc, 1 + 6 * u, 6);
        }
        desc[19] = cfg.goLatency;
        int[] data = cc1.data.clone(), base = { (cc1.sym("boot_desc") - cc1.sym("_data_start")) / 4 };
        System.arraycopy(desc, 0, data, base[0], desc.length);
        c.cores.hostLoad(0, Registers.PMEM_BASE, cc1.code);
        c.cores.hostLoad(0, cc1.sym("_data_start"), data);
        c.cores.hostCommand(0, Registers.CMD_JUMP << 24, cc1.sym("_start"));
        return t;
    }
    /** C&C 1's mailbox word i (sw/cc/cc1.c: [0] commands done, [1] last all-READY, [2] last GO, [3] boot state, [4]-[5]
     *  the "started" requestors, [6] results answered, [7] other requests). */
    int mbox(int i) { return c.cores.unit(0).core.peekLocal(cc1.sym("mbox") + 4 * i); }
    /** C&C 1's error-path summary word i (sw/cc/cc1.c's toast_info). */
    int toastInfo(int i) { return c.cores.unit(0).core.peekLocal(cc1.sym("toast_info") + 4 * i); }
    /** A command for C&C 1 (W14-04), landed in its command block through the host port. */
    void cc1Command(int type, int arg, int... args) { c.cores.hostCommand(0, type << 24 | arg, args); }
    /** A phase through C&C 1: the tile command word and up to 6 arguments to every tile, then all READY, then GO. */
    void phase(int word, int... args) { int[] a = new int[args.length + 1]; a[0] = word; System.arraycopy(args, 0, a, 1, args.length); cc1Command(CC_PHASE, args.length, a); }
    /** The tiles are running their image (C&C 1's boot GO has gone). */
    boolean coresBooted() { return cc1 != null && mbox(3) == 2; }

    // ---------------------------------------------------------------- running
    boolean run(BooleanSupplier cond, long max) { return c.runUntil(cond, max); }
    boolean allStopped() { for (Tile p : c.tiles) if (p.core.halt == Controller.RUN) return false; return true; }
    /** Every tile's program has ended through SIM_EXIT (8.15). */
    boolean allExited() { for (Tile p : c.tiles) if (p.core.halt != Controller.HALT_EXIT) return false; return true; }
    int exitValue(int tile) { return c.tiles[tile].ctl.simExitValue; }
    boolean allInWfi() { for (Tile p : c.tiles) if (!p.core.inWfi) return false; return true; }
    int word(int tile, int byteAddr) { return c.tiles[tile].core.peekLocal(byteAddr); }
    int word(int tile, Image im, String sym, int i) { return word(tile, im.sym(sym) + 4 * i); }
    String haltSummary() {
        StringBuilder sb = new StringBuilder();
        for (Tile p : c.tiles) if (p.core.halt != Controller.HALT_EXIT) { sb.append(p.core.haltReport()).append(" pc=0x").append(Integer.toHexString(p.core.pc)).append(p.core.inWfi ? " (WFI)" : "").append("; "); if (sb.length() > 400) break; }
        return sb.toString();
    }
}
