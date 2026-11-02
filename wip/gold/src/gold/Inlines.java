package gold;

/**
 * Jolt's inlines (virtual.cpp:988-1062, the inlines/*.inc sequences), written as readable loops. Each is checked row
 * for row against jolt-cpp's .inc emit lists, read as data, by InlineEmitTest on every build.
 *
 * Not yet written: an inline emits its jolt-cpp length of rows that refuse to execute, so the bytecode's shape
 * (and every jump target) is already right.
 */
final class Inlines {
  private Inlines() {}

  /** An inline whose expansion isn't written yet: executing it is an error. */
  static final int NOT_WRITTEN = -2;

  static void expand(Expander e, int op, int rd, int rs1, int rs2) {
    if (Secp.forOpcode(op) != Secp.Operation.NONE) {
      Secp256k1Inline.expand(e, op, rd, rs1, rs2);
      return;
    }
    int n = Expander.numCycles(op);
    for (int i = 0; i < n; i++) e.emit(NOT_WRITTEN, 0, 0, 0, true, true, true);
  }
}
