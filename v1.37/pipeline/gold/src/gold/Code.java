package gold;

/** Where a Machine finds its rows: the expanded program (Bytecode), or test equipment's own source (the ACT4 runner). */
public interface Code {
  /** The row at a bytecode index. */
  VInstr at(int index);

  /** The first row of the expansion of the instruction at pc. */
  int indexOf(long pc);

  /** Why the instruction at pc was removed (a standard UNIMP Jolt can't prove), or null. */
  String removedAt(long pc);
}
