package gold;

/** The guest did something jolt-cpp aborts on (a failed assert row, UNIMP, a misaligned access, a bad address). */
public class EmulationError extends RuntimeException {
  private static final long serialVersionUID = 1L;

  public EmulationError(String message) { super(message); }
}
