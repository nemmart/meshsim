/** Tiny test harness: plain Java, no dependencies. */
final class T {
    static int passed, failed;
    static String current = "";

    static void group(String name) { current = name; }
    /** A part of a suite waiting for a later phase: reported, neither passed nor failed. */
    static void pending(String what, String until) { System.out.println("PENDING (" + until + "): " + what); }

    static void check(boolean ok, String what) {
        if (ok) passed++;
        else { failed++; System.out.println("FAIL [" + current + "] " + what); }
    }
    static void eq(long expect, long got, String what) {
        check(expect == got, what + ": expected " + expect + " got " + got);
    }
    static void eqo(Object expect, Object got, String what) {
        check(java.util.Objects.equals(expect, got), what + ": expected " + expect + " got " + got);
    }
    static void throwsISE(Runnable r, String what) {
        try { r.run(); failed++; System.out.println("FAIL [" + current + "] " + what + ": no exception"); }
        catch (IllegalStateException e) { passed++; }
    }
    static int done(String suite) {
        System.out.println(suite + ": " + passed + " passed, " + failed + " failed");
        return failed == 0 ? 0 : 1;
    }
}
