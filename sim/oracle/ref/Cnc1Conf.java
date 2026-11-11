import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.FileReader;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.util.Random;

/**
 * C&C 1's peripheral, ref's side (WORKER16; the C++ side is tests/test_cnc1_periph.cpp): ref's Cnc1Peripheral alone,
 * driven by a seeded script as C&C 1's core and slot (PeripheralSlot) and the chip (Chip, CcCores) drive it with
 * Config.ccCores; one line per observation. Compiled into ref's default package (D5: nothing in sim/ref changes).
 *
 * <pre>
 *   java -cp REF:. Cnc1Conf gen SEED CYCLES    a script on stdout
 *   java -cp REF:. Cnc1Conf run SCRIPT         ref's observations on stdout
 * </pre>
 *
 * Script: "# cnc1conf v1", "# seed S cycles N", then one line per cycle t (from 1): {@code t [R:k] [E:id,l] [U:l]
 * [S:w,v] [L:w]}: a tile's READY pulse at its edge t (Chip: readyPulse(k, t)); a unit's error line as it stands at the
 * end of t (id 0-63 a tile, 65, 66 C&C 2 and 3: errorLine); C&C 1's own ERRORS at the end of t (CcCores.endCycle:
 * unitErrors); C&C 1's core's store of window word w at t; a load of word w sampling at t (words 32-45; 5 EVENTS
 * consuming, 6 EVENTS_PEEK, as the slot reads them: value(t), the consume applied at the end of t under a mask of 0x3F).
 *
 * D33 (C&C 1's stores one cycle later in the framework): the driver applies stores of words 40-45 (the watchdog, LED, the
 * UART) at t + 1, so the peripheral's own work starts as the framework's does; READY_EXPECT and PULSE_GO act at t, as in
 * ref (the framework reproduces their every effect exactly). The generator keeps the cases D33 changes out of the
 * comparison: no PULSE_GO while a watchdog is set; D29's case is avoided as the references avoid it (a tile pulses READY
 * again only after a PULSE_GO has cleared its previous pulse: ref keeps one timestamp per tile, the framework the spec's
 * sticky bit); a load samples at least 2 cycles after the last store (as a core's
 * can: regLoadLatency 2); a UART byte only every 600 cycles once the divisor is 1 (the TX FIFO never fills).
 *
 * Observations: {@code t L:w=v} (hex), {@code t GO} (goNet called at t), and at the end {@code END toast T at A gos G
 * serial HEX}.
 */
final class Cnc1Conf {
    public static void main(String[] a) throws IOException {
        switch (a[0]) {
            case "gen" -> gen(Long.parseLong(a[1]), Integer.parseInt(a[2]));
            case "run" -> run(a[1]);
            default -> throw new IllegalArgumentException(a[0]);
        }
    }

    static final int[] LOADS = { 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 5, 6 };

    static void gen(long seed, int cycles) {
        Random r = new Random(seed);
        StringBuilder sb = new StringBuilder();
        sb.append("# cnc1conf v1\n# seed ").append(seed).append(" cycles ").append(cycles).append('\n');
        boolean[] lvl = new boolean[128];
        boolean unit = false;
        long lastStore = -10, lastTx = -1000;
        boolean wdPhase = false;
        int burst = 0;
        long[] readyAt = new long[64]; java.util.Arrays.fill(readyAt, Long.MIN_VALUE); long clearedAt = Long.MIN_VALUE;
        for (int t = 1; t <= cycles; t++) {
            int pulse = -1;
            sb.append(t);
            if (t > cycles * 4 / 5) wdPhase = true;
            if (burst == 0 && r.nextInt(400) == 0) burst = 64 + r.nextInt(64);
            if (burst > 0) { burst--; if (r.nextInt(4) == 0) pulse = r.nextInt(64); }
            else if (r.nextInt(50) == 0) pulse = r.nextInt(64);
            if (pulse >= 0 && !(readyAt[pulse] != Long.MIN_VALUE && readyAt[pulse] >= clearedAt)) {
                readyAt[pulse] = t + 8; sb.append(" R:").append(pulse);
            }
            if (r.nextInt(150) == 0) {
                int id = r.nextInt(10) == 0 ? 65 + r.nextInt(2) : r.nextInt(64);
                lvl[id] = !lvl[id];
                sb.append(" E:").append(id).append(',').append(lvl[id] ? 1 : 0);
            }
            if (r.nextInt(3000) == 0) { unit = !unit; sb.append(" U:").append(unit ? 1 : 0); }
            String st = null;
            if (t == 1) st = "44,1";
            else if (t == 3) st = "37,ffffffff";
            else if (t == 5) st = "38,ffffffff";
            else if (r.nextInt(40) == 0) {
                int k = r.nextInt(100);
                if (k < 15) st = "37," + Integer.toHexString(r.nextInt(4) == 0 ? r.nextInt() : -1);
                else if (k < 30) st = "38," + Integer.toHexString(r.nextInt(4) == 0 ? r.nextInt() : -1);
                else if (k < 55) st = wdPhase ? null : "39," + (r.nextInt(5) == 0 ? "0" : "1");
                else if (k < 62) st = "41," + Integer.toHexString(r.nextInt(4));
                else if (k < 72) { if (t - lastTx > 600) { st = "42," + Integer.toHexString(r.nextInt(512)); lastTx = t; } }
                else if (k < 77) st = "45," + Integer.toHexString(r.nextInt(4) << 16);
                else if (k < 85) st = wdPhase ? "40," + Integer.toHexString(r.nextInt(3) == 0 ? 0 : 1 + r.nextInt(200)) : null;
                else if (k < 92) st = (32 + r.nextInt(5)) + ",1";            // read-only: 0
                else st = (46 + r.nextInt(80)) + ",5";                          // undefined: -1
            }
            if (st != null) { sb.append(" S:").append(st); lastStore = t; if (st.equals("39,1")) clearedAt = t + 1; }
            else if (t - lastStore >= 2 && r.nextInt(6) == 0) sb.append(" L:").append(LOADS[r.nextInt(LOADS.length)]);
            sb.append('\n');
        }
        System.out.print(sb);
    }

    static void run(String script) throws IOException {
        Config cfg = new Config();
        Cnc1Peripheral p = new Cnc1Peripheral(cfg);
        long[] goAt = { -1 };
        StringBuilder obs = new StringBuilder();
        p.goNet = t -> obs.append(t).append(" GO\n");
        int[] deferredW = null;                              // D33: a store of words 40-45, applied at t + 1
        try (BufferedReader in = new BufferedReader(new FileReader(script), 1 << 16);
             Writer out = new BufferedWriter(new OutputStreamWriter(System.out), 1 << 16)) {
            for (String line; (line = in.readLine()) != null; ) {
                if (line.startsWith("#")) continue;
                String[] f = line.split(" ");
                long t = Long.parseLong(f[0]);
                if (deferredW != null) { p.write(t, deferredW[0], deferredW[1]); deferredW = null; }
                int consume = -1; Integer unit = null;
                for (int i = 1; i < f.length; i++) {
                    char k = f[i].charAt(0);
                    String[] v = f[i].substring(2).split(",");
                    switch (k) {
                        case 'R' -> p.readyPulse(Integer.parseInt(v[0]), t);
                        case 'E' -> p.errorLine(t, Integer.parseInt(v[0]), v[1].equals("1"));
                        case 'U' -> unit = Integer.parseInt(v[0]);
                        case 'S' -> {
                            int w = Integer.parseInt(v[0]), x = (int) Long.parseLong(v[1], 16);
                            if (w >= 40 && w <= 45) deferredW = new int[] { w, x }; else p.write(t, w, x);
                        }
                        case 'L' -> {
                            int w = Integer.parseInt(v[0]);
                            int x;
                            if (w == 5 || w == 6) { x = p.ev.value(t); if (w == 5) consume = x & 0x3F; }
                            else { Integer y = p.word(t, w); x = y == null ? 0 : y; }
                            obs.append(t).append(" L:").append(w).append('=').append(Integer.toHexString(x)).append('\n');
                        }
                        default -> throw new IllegalArgumentException(f[i]);
                    }
                }
                p.runCycle(t);
                if (consume >= 0) p.ev.consume(consume);
                if (unit != null) p.unitErrors(t, unit == 1);
                if (obs.length() > 1 << 16) { out.write(obs.toString()); obs.setLength(0); }
            }
            StringBuilder hex = new StringBuilder();
            for (char ch : p.serialOut.toString().toCharArray()) hex.append(String.format("%02x", (int) ch));
            obs.append("END toast ").append(p.toast ? 1 : 0).append(" at ").append(p.toastAt).append(" gos ").append(p.gos)
               .append(" serial ").append(hex.length() == 0 ? "-" : hex).append('\n');
            out.write(obs.toString());
        }
    }
}
