package gold;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;

/** Stage 1's secp256k1 advice: the words row_dump took from jolt-cpp's rows (u64 each, little-endian, row order). */
public final class AdviceFile implements Machine.SecpAdvice {
  private final ByteBuffer words;

  public AdviceFile(Path file) throws IOException {
    words = Files.exists(file) ? ByteBuffer.wrap(Files.readAllBytes(file)).order(ByteOrder.LITTLE_ENDIAN)
                               : ByteBuffer.allocate(0);
  }

  @Override public long next() {
    if (words.remaining() < 8) throw new EmulationError("secp advice exhausted");
    return words.getLong();
  }

  public long remainingWords() { return words.remaining() / 8; }
}
