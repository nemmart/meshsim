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
  /** Symbols from the .symtab section(s), by name (absent when the ELF has no section headers). */
  public final java.util.Map<String, Long> symbols = new java.util.HashMap<>();
  /** Section headers (sh_type, sh_flags, sh_addr, sh_offset, sh_size). */
  public record Section(long type, long flags, long addr, long offset, long size) {
    public boolean nobits() { return type == 8; }                              // SHT_NOBITS
    /** The object crate's SectionKind::Text for ELF: SHT_PROGBITS with SHF_EXECINSTR. */
    public boolean text() { return type == 1 && (flags & 4) != 0; }
  }
  public final List<Section> sections = new ArrayList<>();

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
    // section headers: SHT_SYMTAB (2) entries name their string table through sh_link (ELF64 Elf64_Shdr, Elf64_Sym)
    long shoff = b.getLong(40);
    int shentsize = b.getShort(58) & 0xffff, shnum = b.getShort(60) & 0xffff;
    if (shoff != 0 && shentsize == 64 && shoff + (long) shnum * 64 <= bytes.length) {
      for (int i = 0; i < shnum; i++) {
        int sh = (int) (shoff + 64L * i);
        sections.add(new Section(b.getInt(sh + 4) & 0xffffffffL, b.getLong(sh + 8), b.getLong(sh + 16), b.getLong(sh + 24),
            b.getLong(sh + 32)));
      }
      for (int i = 0; i < shnum; i++) {
        int sh = (int) (shoff + 64L * i);
        if (b.getInt(sh + 4) != 2) continue;
        long off = b.getLong(sh + 24), size = b.getLong(sh + 32), entsize = b.getLong(sh + 56);
        int link = b.getInt(sh + 40);
        long strOff = b.getLong((int) (shoff + 64L * link) + 24);
        for (long e = 0; entsize == 24 && e + 24 <= size; e += 24) {
          int at = (int) (off + e);
          int name = b.getInt(at);
          if (name == 0) continue;
          int start = (int) (strOff + name), end = start;
          while (end < bytes.length && bytes[end] != 0) end++;
          symbols.putIfAbsent(new String(bytes, start, end - start, java.nio.charset.StandardCharsets.US_ASCII), b.getLong(at + 8));
        }
      }
    }
  }
}
