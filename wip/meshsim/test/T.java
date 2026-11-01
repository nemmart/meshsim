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
    /** v1.25 (DECISIONS-v1.25 s8b): r must stop with UndefinedBehavior whose message names all of {@code names}. */
    static UndefinedBehavior ub(Runnable r, String what, String... names) {
        try { r.run(); failed++; System.out.println("FAIL [" + current + "] " + what + ": ran on, no undefined-behavior stop"); return null; }
        catch (UndefinedBehavior e) {
            for (String n : names) if (!e.getMessage().contains(n)) { failed++; System.out.println("FAIL [" + current + "] " + what + ": the stop doesn't name '" + n + "': " + e.getMessage()); return e; }
            passed++; return e;
        }
    }
    static int done(String suite) {
        System.out.println(suite + ": " + passed + " passed, " + failed + " failed");
        return failed == 0 ? 0 : 1;
    }
}
