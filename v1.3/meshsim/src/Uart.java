/**
 * The tile's UART (spec 4.20, 8.13), as on every compute tile: unwired. The registers decode but are no-ops: UART_TX
 * writes are dropped and never stall, UART_DIVISOR and UART_STATUS writes are ignored, UART_RX reads valid = 0,
 * UART_STATUS reads empty FIFOs and no errors, and UART_DIVISOR reads its reset value (owner, 2026-10-27). meshsim
 * prints every TX byte to the tile's console ({@link #console}), wired or not. The shifter and its divisor timing are not
 * modelled (no tile is wired: DEVIATIONS.md).
 */
final class Uart {
    /** round(f_ref / 57,600) at f_ref = 25 MHz (4.20: f_ref is an Open parameter). */
    static final int RESET_DIVISOR = 434;
    final Tile tile;
    final StringBuilder console = new StringBuilder();
    long txBytes;
    /** Where completed console lines go (null: kept in {@link #console} only). */
    java.util.function.Consumer<String> sink;
    private int lineStart;
    Uart(Tile tile) { this.tile = tile; }
    void tx(long t, int v) {
        txBytes++;
        char c = (char) (v & 0xFF);
        console.append(c);
        if (c == '\n' && sink != null) { sink.accept("tile " + tile.idx + ": " + console.substring(lineStart, console.length() - 1)); lineStart = console.length(); }
    }
    int rx(long t) { return 0; }
    int status() { return 0; }
    int divisor() { return RESET_DIVISOR; }
    void writeStatus(int v) {}
    void writeDivisor(int v) {}
    boolean rxNotEmpty() { return false; }
    boolean txNotFull() { return true; }
}
