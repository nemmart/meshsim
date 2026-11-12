import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

/**
 * The replayer's error lists, ref's side (WORKER25, P7b): each committed ReplayConf script (tests/replayer/*.script,
 * unzipped) replayed on ref's Replayer exactly as `ReplayConf run` replays it, then every list the harnesses print
 * (Replayer.violations, invalids, overruns, underflows, leftovers, badWrites) with its size and every entry, as
 * List.toString writes it. The C++ side (tests/test_replay_lists.cpp) prints the same from the C++ replayer's lists.
 * Compiled into ref's default package with ReplayConf (D5).
 *
 *   java -cp REF:. ReplayLists NAME SCRIPT [NAME SCRIPT ...]
 */
public final class ReplayLists {
    public static void main(String[] a) throws Exception {
        StringBuilder out = new StringBuilder();
        for (int k = 0; k + 1 < a.length; k += 2) {
            ReplayConf.Rec rc = new ReplayConf.Rec(new Replayer(0));
            for (String line : Files.readAllLines(Path.of(a[k + 1]))) {
                if (line.isEmpty() || line.startsWith("#")) continue;
                String[] f = line.split(" ");
                long t = Long.parseLong(f[0]);
                for (int i = 1; i < f.length; i++) {
                    String o = f[i];
                    switch (o.charAt(0)) {
                        case 'A' -> { String[] g = o.split(":"); rc.accept(t, Integer.parseInt(g[1], 16), Long.parseUnsignedLong(g[2], 16), Long.parseUnsignedLong(g[3], 16)); }
                        case 'R' -> rc.run(t);
                        case 'C' -> rc.config(t, (int) Long.parseLong(o.substring(2), 16));
                        case 'Y' -> rc.read(t);
                        case 'K' -> rc.consume(t, Integer.parseInt(o.substring(2), 16));
                        case 'S' -> rc.step(t);
                        default -> throw new IllegalArgumentException(o);
                    }
                }
            }
            Replayer r = rc.r;
            out.append(a[k]).append('\n');
            list(out, "violations", r.violations); list(out, "invalid", r.invalids); list(out, "overruns", r.overruns);
            list(out, "underflows", r.underflows); list(out, "leftover", r.leftovers); list(out, "bad writes", r.badWrites);
            out.append("clean ").append(r.clean()).append('\n');
        }
        System.out.print(out);
    }
    static void list(StringBuilder b, String name, List<String> l) { b.append(name).append(' ').append(l.size()).append(' ').append(l).append('\n'); }
}
