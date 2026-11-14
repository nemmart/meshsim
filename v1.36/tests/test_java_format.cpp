// test_java_format.cpp: java_format.h against Java's outputs: the values (tests/test_sort_kit.cpp), the
// and the examples (tests/ctl/ctl.h, tests/stageb.h), and the shapes PhaseLog prints (%.0f, %.1f).
#include <cstdint>

#include "harness.h"
#include "java_format.h"

using jfmt::fixed;
using jfmt::grouped;

TEST(java_format_grouped) {
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

TEST(java_format_fixed) {
    CHECK(fixed(0.0, 2, true) == "+0.00");
    CHECK(fixed(0.125, 2, true) == "+0.13");        // C's printf: +0.12
    CHECK(fixed(1.005, 2, true) == "+1.01");        // the shortest digits 1.005, HALF_UP (C: +1.00)
    CHECK(fixed(2.675, 2, true) == "+2.68");        // (C: +2.67)
    CHECK(fixed(-0.155, 2, true) == "-0.16");
    CHECK(fixed(-0.001, 2, true) == "-0.00");
    CHECK(fixed(99.995, 2, true) == "+100.00");
    CHECK(fixed(0.7300000000000001, 2, true) == "+0.73");
    CHECK(fixed(100.0 * -7 / 250338, 2, true) == "-0.00");
    CHECK(fixed(100.0 * 36 / 5697, 2, true) == "+0.63");
    CHECK(fixed(12345.678, 2) == "12345.68");
    CHECK(fixed(129.25, 1) == "129.3");             // the example (C: 129.2)
    CHECK(fixed(0.0005, 3) == "0.001");             // the %.3f (C: 0.000 or 0.001 by the binary value)
    CHECK(fixed(2.5, 0) == "3");                    // PhaseLog's %.0f (C: 2)
    CHECK(fixed(0.5, 0) == "1");
    CHECK(fixed(1234.0 / 64.0, 0) == "19");         // 19.28125
    CHECK(fixed(1440.0 / 64.0, 1) == "22.5");
    CHECK(fixed(0.05, 1) == "0.1");                 // (C: 0.1 by the binary value 0.05000000000000000277)
    CHECK(fixed(0.25, 1) == "0.3");                 // (C: 0.2)
}
