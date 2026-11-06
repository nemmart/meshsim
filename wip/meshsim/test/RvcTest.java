/**
 * The Tracer's RVC decompressor ({@link Rvc}, spec 4.21.4 step 2) against gold's (wip/gold, Decoder.uncompress, a16z's
 * rules with D12) on all 2^16 16-bit encodings, and the code-stream word's rules: bit 0 cleared as the compressed flag,
 * 0 (an invalid expansion) for the encodings with no 32-bit form and the reserved forms (c.unimp among them).
 * Gold is found on the class path, or in build/gold (run_tests.sh compiles it there); without it the comparison is skipped.
 */
public final class RvcTest {
    public static void main(String[] args) throws Exception {
        T.group("Rvc against gold");
        java.lang.reflect.Method g = null;
        try { g = Class.forName("gold.Decoder").getMethod("uncompress", int.class); }
        catch (ClassNotFoundException e) {
            // run_tests.sh's $J has build/ only: gold's classes from build/gold (run_tests.sh compiles them there)
            java.nio.file.Path gp = java.nio.file.Path.of("build", "gold");
            if (java.nio.file.Files.isDirectory(gp)) {
                ClassLoader cl = new java.net.URLClassLoader(new java.net.URL[] { gp.toUri().toURL() }, RvcTest.class.getClassLoader());
                g = Class.forName("gold.Decoder", true, cl).getMethod("uncompress", int.class);
            } else T.pending("Rvc against gold, every 16-bit encoding", "gold's classes in build/gold");
        }
        int same = 0, compressedForms = 0, invalid = 0;
        for (int h = 0; h < 65536; h++) {
            if ((h & 3) == 3) continue;                       // a 32-bit instruction's first half
            compressedForms++;
            int mine = Rvc.uncompress(h);
            if (g != null) { int gold = (int) g.invoke(null, h); if (mine == gold) same++; else T.check(false, String.format("0x%04x: Rvc 0x%08x, gold 0x%08x", h, mine, gold)); }
            int cw = Rvc.codeWord(h);
            if (mine == 0xffffffff || mine == 0) { invalid++; T.check(cw == 0, String.format("0x%04x: no 32-bit form gives code word 0", h)); }
            else T.check(cw == (mine & ~1) && (cw & 3) == 2, String.format("0x%04x: the code word is the expansion with bit 0 cleared", h));
        }
        if (g != null) T.eq(compressedForms, same, "every compressed encoding equals gold's expansion");
        T.eq(0, Rvc.codeWord(0x0000), "c.unimp (0x0000) has no 32-bit form: an invalid expansion");
        T.eq(0x00101012, Rvc.codeWord(0x0002 | 0 << 7 | 1 << 2) , "C.SLLI x0, 1 (a HINT) is SLLI x0, x0, 1 (D12), bit 0 cleared");
        T.eq(0x0000006E, Rvc.codeWord(0xA001), "C.J 0 is JAL x0, 0 (the program's end), bit 0 cleared");
        T.eq(0x00100072, Rvc.codeWord(0x9002), "C.EBREAK is EBREAK, bit 0 cleared");
        System.out.println("compressed encodings " + compressedForms + ", with no valid form " + invalid);
        System.exit(T.done("RvcTest"));
    }
}
