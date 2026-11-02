package gold;

/**
 * jolt-cpp's host I/O encoding (host_io_encoding.cpp at 821e1bd). Older guests call the host with a raw ECALL; newer
 * ones use VirtualHostIO and keep ECALL for traps. In LegacyRawEcall mode every ECALL is traced as VirtualHostIO.
 */
public final class HostIo {
  private HostIo() {}

  public enum Encoding { AUTO, STANDARD, LEGACY_RAW_ECALL }

  /**
   * Auto-detection (host_io_encoding.cpp:38-90): legacy iff the whole code decodes, has a raw ECALL, and has no
   * VirtualHostIO and no trap runtime (CSRRW, CSRRS, MRET). The scan stops at the first undecodable instruction, and
   * then the answer is Standard.
   */
  public static Encoding resolve(byte[] code, Encoding requested) {
    if (requested != Encoding.AUTO) return requested;
    int ecalls = 0, offset = 0;
    boolean hostIo = false, trapRuntime = false;
    while (offset < code.length) {
      Decoder.Decoded d = Decoder.decodeNext(code, offset);
      if (!d.instr().isValid()) return Encoding.STANDARD;
      switch (d.instr().opcode) {
        case Op.ECALL: ecalls++; break;
        case Op.VirtualHostIO: hostIo = true; break;
        case Op.CSRRW: case Op.CSRRS: case Op.MRET: trapRuntime = true; break;
        default: break;
      }
      offset += d.size();
    }
    return ecalls > 0 && !hostIo && !trapRuntime ? Encoding.LEGACY_RAW_ECALL : Encoding.STANDARD;
  }

  /** host_io_encoding.cpp:92-105. */
  public static Instruction apply(Instruction in, Encoding encoding) {
    if (encoding == Encoding.LEGACY_RAW_ECALL && in.opcode == Op.ECALL) return Instruction.noOperands(Op.VirtualHostIO);
    return in;
  }
}
