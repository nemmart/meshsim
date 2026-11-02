/**
 * meshsim's stop on undefined behavior (DECISIONS-v1.25 section 8a, b; v1.25). A rule a program must keep that the
 * hardware doesn't check is UB: the hardware does nothing special and the outcome is unspecified, so meshsim stops
 * the run at once with an error naming the access, instead of modelling an outcome. Thrown from inside a cycle; it
 * propagates out of {@link Chip#runUntil} to whoever runs the chip. Tests that provoke UB on purpose catch it.
 * <p>v1.25's UB: misalignment (a mode-0 packet misaligned to its size, at the director; a write through a misaligned
 * PTR; a misaligned read by the address generator, the GATHER engine or the read port; the controller's misaligned
 * loads and stores; the replayer's misaligned part-0 writes), and gather buffers not 128-bit aligned (section 8c).
 */
final class UndefinedBehavior extends RuntimeException {
    final long cycle;
    final int tile;
    final String unit;

    UndefinedBehavior(long cycle, int tile, String unit, String what) {
        super("undefined behavior at cycle " + cycle + ", tile " + tile + ", " + unit + ": " + what
                + " (DECISIONS-v1.25 s8: meshsim stops; the hardware does nothing special)");
        this.cycle = cycle; this.tile = tile; this.unit = unit;
    }
}
