package gold;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * A minimal ELF64 reader, by hand: the header and the program headers, which are all the loader uses
 * (jolt-cpp loader.cpp:25-190 reads the same through libelf).
 */
public final class Elf {
  public record Segment(long type, long flags, long offset, long vaddr, long fileSize, long memSize, long align) {
    public boolean executable() { return (flags & 1) != 0; }   // PF_X
  }

  public static final int PT_LOAD = 1;
  public static final int EM_RISCV = 243;
  public static final int ET_EXEC = 2;

  public final byte[] bytes;
  public final int machine, type;
  public final long entry;
  public final List<Segment> segments = new ArrayList<>();

  public Elf(Path path) throws IOException {
    bytes = Files.readAllBytes(path);
    ByteBuffer b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
    if (bytes.length < 64 || b.getInt(0) != 0x464c457f) throw new IOException(path + ": not an ELF file");
    if (bytes[4] != 2) throw new IOException(path + ": not ELF64");
    if (bytes[5] != 1) throw new IOException(path + ": not little-endian");     // EI_DATA = ELFDATA2LSB
    type = b.getShort(16) & 0xffff;
    machine = b.getShort(18) & 0xffff;
    entry = b.getLong(24);
    long phoff = b.getLong(32);
    int phentsize = b.getShort(54) & 0xffff;
    int phnum = b.getShort(56) & 0xffff;
    for (int i = 0; i < phnum; i++) {
      int o = (int) (phoff + (long) i * phentsize);
      segments.add(new Segment(b.getInt(o) & 0xffffffffL, b.getInt(o + 4) & 0xffffffffL, b.getLong(o + 8),
          b.getLong(o + 16), b.getLong(o + 32), b.getLong(o + 40), b.getLong(o + 48)));
    }
  }
}
