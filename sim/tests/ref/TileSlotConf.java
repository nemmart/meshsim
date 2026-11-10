import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

/**
 * PeripheralSlotTest's groups as conformance, ref's side (WORKER15; the C++ side is tests/test_tileslotconf.cpp).
 *
 *   java -cp REF:. TileSlotConf PROG       a .prog (tests/ref/tileslotconf_progs.py) run on ref's machine: its program as
 *                                          every tile's boot ROM (Controller.bootRom), Config.machine(), the test
 *                                          peripheral as the tile's peripheral 1 if "periph test"; the pokes between
 *                                          cycles; the injects with Net.at. Writes the observations on stdout.
 *
 * Observations: "dmem A V" for the dumped words; "dir T SRC ADDR LO HI" for each mode-0 packet the tile's director
 * accepted for a peripheral's region (0x04-0x06) or not (its log); "bad N" (packets dropped as bad); "acc LAND SRC OFF LO
 * HI" (the test peripheral's accepts); "dma T DST MODE CTR ADDR LO HI" (the slot's DMA packets).
 */
public final class TileSlotConf {
    public static void main(String[] a) throws Exception {
        int tile = -1, cycles = 0; boolean test = false;
        int[] rom = null;
        java.util.List<String[]> pokes = new java.util.ArrayList<>(), injects = new java.util.ArrayList<>();
        int dumpA = 0, dumpN = 0;
        for (String l : Files.readAllLines(Path.of(a[0]))) {
            if (l.isEmpty() || l.startsWith("#")) continue;
            String[] f = l.split(" ");
            switch (f[0]) {
                case "tile" -> tile = Integer.parseInt(f[1]);
                case "periph" -> test = f[1].equals("test");
                case "cycles" -> cycles = Integer.parseInt(f[1]);
                case "rom" -> { rom = new int[f.length - 1]; for (int i = 1; i < f.length; i++) rom[i - 1] = (int) Long.parseLong(f[i], 16); }
                case "poke" -> pokes.add(f);
                case "inject" -> injects.add(f);
                case "dump" -> { dumpA = Integer.parseInt(f[1], 16); dumpN = Integer.parseInt(f[2]); }
                default -> throw new IllegalArgumentException(l);
            }
        }
        Controller.bootRom = rom;
        final int tp = tile;
        TestPeripheral[] x = new TestPeripheral[1];
        Config k = Config.machine();
        if (test) k.testPeripheral = t -> t == tp ? (x[0] = new TestPeripheral()) : null;
        Net n = new Net(k);
        Tile p = n.c.tiles[tile];
        for (String[] f : injects)
            n.at(Integer.parseInt(f[2]), Long.parseLong(f[1]), tile, LinkWord.M_WRITE, 0, (int) Long.parseLong(f[3], 16), LinkWord.SIZE_128,
                 Long.parseUnsignedLong(f[4], 16), Long.parseUnsignedLong(f[5], 16));
        for (long c = 0; c < cycles; c++) {
            if (n.c.cycle != c) throw new IllegalStateException("the chip's cycle " + n.c.cycle + ", not " + c);
            for (String[] f : pokes) {
                if (Long.parseLong(f[1]) != c) continue;
                switch (f[2]) {
                    case "post" -> x[0].post(c);
                    case "credit" -> x[0].credit(c);
                    case "level1" -> x[0].level(true);
                    case "level0" -> x[0].level(false);
                    case "fault" -> x[0].fault();
                    default -> throw new IllegalArgumentException(f[2]);
                }
            }
            n.step();
        }
        StringBuilder o = new StringBuilder("# tileslotconf observations v1: " + Path.of(a[0]).getFileName() + "\n");
        for (int i = 0; i < dumpN; i++) o.append("dmem ").append(Integer.toHexString(dumpA + 4 * i)).append(' ').append(Integer.toHexString(p.cmem.peek(dumpA + 4 * i))).append('\n');
        for (long[] e : p.dir.log) {
            long h = e[1 + 1];
            if (LinkWord.mode(h) != LinkWord.M_WRITE) continue;
            int r = LinkWord.region(LinkWord.addr(h));
            if (r < 0x04 || r > 0x06) continue;
            o.append("dir ").append(e[0]).append(' ').append(LinkWord.src(h)).append(' ').append(Integer.toHexString(LinkWord.addr(h))).append(' ')
             .append(Long.toHexString(e[1])).append(' ').append(Long.toHexString(e[3])).append('\n');
        }
        o.append("bad ").append(p.dir.droppedBad).append('\n');
        if (x[0] != null)
            for (long[] e : x[0].accepts)    // {landT, src, offset, size, lo, hi}
                o.append("acc ").append(e[0]).append(' ').append(e[1]).append(' ').append(Long.toHexString(e[2])).append(' ')
                 .append(Long.toHexString(e[4])).append(' ').append(Long.toHexString(e[5])).append('\n');
        for (long[] d : p.slot.dmaOut)       // {t, dst, mode, counter, addr, size, lo, hi}
            o.append("dma ").append(d[0]).append(' ').append(d[1]).append(' ').append(d[2]).append(' ').append(d[3]).append(' ')
             .append(Long.toHexString(d[4])).append(' ').append(Long.toHexString(d[6])).append(' ').append(Long.toHexString(d[7])).append('\n');
        System.out.print(o);
    }
}
