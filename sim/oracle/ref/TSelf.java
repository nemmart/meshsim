/**
 * TSelf (WORKER20, P2): v1.3's suite harness T driven through every method, passing and failing, so its lines and
 * counts can be compared with tests/suite.h's (tests/test_suite.cpp runs the same sequence). oracle/ref/tself.sh.
 */
public final class TSelf {
    public static void main(String[] a) {
        T.check(true, "never printed");
        T.check(false, "a failure before any group");
        T.group("checks");
        T.check(1 + 1 == 2, "passes");
        T.check(false, "fails at " + 42 + " with a value " + (-7));
        T.group("eq");
        T.eq(5, 5, "equal");
        T.eq(5, 6, "unequal");
        T.eq(-1, 0xFFFFFFFFL, "a word against -1");
        T.eq(Long.MIN_VALUE, Long.MAX_VALUE, "the extremes");
        T.eq((int) 0x80000000, 0x80000000L, "an int widened");
        T.group("eqo");
        T.eqo("[0, 2, 3, 5, 8]", java.util.Arrays.toString(new long[] { 0, 2, 3, 5, 8 }), "a list, equal");
        T.eqo("[0, 1]", java.util.Arrays.toString(new long[] { }), "a list against the empty one");
        T.eqo("x", "y", "strings");
        T.eqo(null, null, "two nulls");
        T.eqo(null, "z", "null against a string");
        T.group("stops");
        T.throwsISE(() -> { throw new IllegalStateException("bad put"); }, "throws");
        T.throwsISE(() -> { }, "runs on");
        T.ub(() -> { throw new UndefinedBehavior(5, 3, "director", "a mode-0 packet at 0x4 not aligned to its size"); }, "names both", "director", "aligned");
        T.ub(() -> { }, "ub runs on", "x");
        T.ub(() -> { throw new UndefinedBehavior(9, 1, "read port", "a misaligned read"); }, "names the wrong unit", "read port", "gather", "nothing");
        T.pending("a part of this suite", "phase 2");
        T.group("");
        T.check(false, "the empty group");
        System.exit(T.done("TSelf"));
    }
}
