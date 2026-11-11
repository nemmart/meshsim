/**
 * GoldRvc (WORKER21): gold's RVC decompressor (gold.Decoder.uncompress, a16z's rules with D12) on every compressed
 * 16-bit encoding (h & 3 != 3), one line each: "hhhh xxxxxxxx" (the encoding, the 32-bit expansion as gold returns it:
 * ffffffff where it can't be expanded, 0 for the reserved forms). RvcTest's reference for the C++ suite
 * (tests/suite_rvc.cpp), which compares rvc::uncompress with it, as v1.3's RvcTest compares Rvc with gold.
 * Usage: java -cp <gold's classes> GoldRvc > rvc-gold.txt
 */
public final class GoldRvc {
    public static void main(String[] args) {
        StringBuilder sb = new StringBuilder();
        for (int h = 0; h < 65536; h++) {
            if ((h & 3) == 3) continue;
            sb.append(String.format("%04x %08x%n", h, gold.Decoder.uncompress(h)));
        }
        System.out.print(sb);
    }
}
