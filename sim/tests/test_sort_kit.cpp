// The sort kit's helpers (WORKER24, P6; tests/sort_kit.h) against Java's own outputs: String.format's %,d, %+,d and
// %+.2f (HALF_UP on the shortest digits; every expected value printed by JDK 21 itself), Path.of(...).toString(), Inputs' parser and
// HASHES reader. Then the tools on v1.3's kit run at -n 2000 (Quest 178, tests/sort/kit/n2000.tgz): sort_summary's
// SUMMARY.md and all.csv equal v1.3's SortSummary's, sort_compare's self-comparison equal v1.3's SortCompare's.
#include <fcntl.h>
#include <unistd.h>

#include "harness.h"
#include "sort_kit.h"

// the two tools' mains, linked into this test
#define SORT_KIT_NO_MAIN
#include "sort_compare_main.cpp"
#include "sort_summary_main.cpp"

using namespace sortkit;

TEST(grouping_as_java) {
    CHECK(grouped(0) == "0");
    CHECK(grouped(999) == "999");
    CHECK(grouped(1000) == "1,000");
    CHECK(grouped(1360082) == "1,360,082");
    CHECK(grouped(-1234567) == "-1,234,567");
    CHECK(grouped(0, true) == "+0");
    CHECK(grouped(42, true) == "+42");
    CHECK(grouped(-42, true) == "-42");
    CHECK(grouped(INT64_MIN) == "-9,223,372,036,854,775,808");
}

TEST(fixed_half_up_as_java) {
    // String.format("%+.2f", x) in JDK 21
    CHECK(fixed(0.0, 2, true) == "+0.00");
    CHECK(fixed(0.125, 2, true) == "+0.13");        // C's printf: +0.12
    CHECK(fixed(1.005, 2, true) == "+1.01");        // shortest digits 1.005, HALF_UP (C: +1.00)
    CHECK(fixed(2.675, 2, true) == "+2.68");        // (C: +2.67)
    CHECK(fixed(-0.155, 2, true) == "-0.16");
    CHECK(fixed(-0.001, 2, true) == "-0.00");
    CHECK(fixed(99.995, 2, true) == "+100.00");
    CHECK(fixed(0.7300000000000001, 2, true) == "+0.73");
    CHECK(fixed(100.0 * -7 / 250338, 2, true) == "-0.00");
    CHECK(fixed(100.0 * 36 / 5697, 2, true) == "+0.63");
    CHECK(fixed(12345.678, 2) == "12345.68");
    CHECK(fixed(1e-7, 2, true) == "+0.00");
    CHECK(fixed(5e-3, 2, true) == "+0.01");         // 0.005 HALF_UP
}

TEST(java_path) {
    CHECK(java_path("results") == "results");
    CHECK(java_path("results/") == "results");
    CHECK(java_path("a//b///") == "a/b");
    CHECK(java_path("./results") == "./results");
    CHECK(java_path("/") == "/");
    CHECK(resolve("results/", "all.csv") == "results/all.csv");
}

TEST(inputs_line_and_hashes) {
    Ordered m;
    CHECK(!parse_inputs("sort uniform", m));
    CHECK(parse_inputs("  inputs: meshsim.jar=(classes) sw/bootrom.hex=ab keys-2000/u.keys=cd  ", m));
    CHECK_EQ(m.v.size(), size_t(3));
    CHECK(m.v[0].first == "meshsim.jar" && m.v[0].second == "(classes)");
    CHECK(*m.get("keys-2000/u.keys") == "cd");
    const std::string h = std::string(64, 'A');
    const std::string path = "/tmp/test_sort_kit_hashes";
    CHECK(write_file(path, "# c\n\n" + h + "  hops.txt\n" + std::string(64, 'b') + " *image/x y.bin\nshort\n"));
    Ordered r = read_hashes(path);
    CHECK_EQ(r.v.size(), size_t(2));
    CHECK(*r.get("hops.txt") == std::string(64, 'a'));
    CHECK(r.contains("image/x y.bin"));
    Ordered run; run.put("hops.txt", std::string(64, 'c')); run.put("other", "z");
    std::vector<std::string> d = differences(run, r);
    CHECK_EQ(d.size(), size_t(1));
    CHECK(d[0] == "hops.txt: cccccccccccc... (reference aaaaaaaaaaaa...)");
    std::remove(path.c_str());
}

TEST(the_kit_tools_on_v13s_n2000_run) {
    char cwd[4096]; CHECK(getcwd(cwd, sizeof cwd) != nullptr);
    const std::string here = cwd, tgz = here + "/tests/sort/kit/n2000.tgz";
    CHECK(is_file(tgz));
    char tmpl[] = "/tmp/test_sort_kit_XXXXXX";
    const char* d = mkdtemp(tmpl); CHECK(d != nullptr);
    const std::string dir = d;
    CHECK_EQ(std::system(("tar xzf " + tgz + " -C " + dir).c_str()), 0);
    CHECK(chdir(dir.c_str()) == 0);
    std::rename("results/SUMMARY.md", "ref.md"); std::rename("results/all.csv", "ref.csv");
    char a0[] = "sort_summary", a1[] = "results";
    char* sargv[] = {a0, a1, nullptr};
    char c0[] = "sort_compare", c2[] = "self.md";
    char* cargv[] = {c0, a1, a1, c2, nullptr};
    std::fflush(stdout);                                    // the tools' stdout to /dev/null, then back
    const int saved = dup(1), null = open("/dev/null", O_WRONLY);
    dup2(null, 1); close(null);
    const int rs = sort_summary_main(2, sargv);
    const int rc = sort_compare_main(4, cargv);
    std::fflush(stdout); dup2(saved, 1); close(saved);
    const std::string got_md = read_file_all("results/SUMMARY.md"), want_md = read_file_all("ref.md");
    const std::string got_csv = read_file_all("results/all.csv"), want_csv = read_file_all("ref.csv");
    const std::string got_cmp = read_file_all("self.md"), want_cmp = read_file_all(here + "/tests/sort/kit/n2000-compare-self.out");
    CHECK(chdir(here.c_str()) == 0);
    CHECK_EQ(std::system(("rm -rf " + dir).c_str()), 0);
    CHECK_EQ(rs, 0); CHECK_EQ(rc, 0);
    CHECK(!want_md.empty() && got_md == want_md);
    CHECK(!want_csv.empty() && got_csv == want_csv);
    CHECK(!want_cmp.empty() && got_cmp == want_cmp);
}
