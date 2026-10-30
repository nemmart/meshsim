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
    static int[] loader() { try { return words(SW.resolve("loader.bin")); } catch (IOException e) { throw new RuntimeException(e); } }

    // ---------------------------------------------------------------- boot (6.2)
    /** C&C's boot (PHASE-2 INTERIM, until phase 4's boot ROM): with every tile halted, broadcast the loader and the image
     *  by uncounted mode-0 writes, the marker (CMD_JUMP to the image's entry), wait for all 64 READY, raise RUN. */
    Image boot(String name) {
        Image im = new Image(name);
        bootStart = c.cycle;
        c.cc.loadWords(LinkWord.BROADCAST, Registers.LOADER_BASE, loader())
            .loadWords(LinkWord.BROADCAST, Registers.PMEM_BASE, im.code)
            .loadWords(LinkWord.BROADCAST, im.sym("_data_start"), im.data)
            .command(LinkWord.BROADCAST, Registers.CMD_JUMP << 24, im.sym("_start"))
            .runWhenReady()
            .call(() -> runAt = c.cc.lastRunAt);
        return im;
    }

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
