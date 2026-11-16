// test_delivery_json (D42): tools/delivery/json.h, the JSON reader and writer convert_delivery and
// synthetic_delivery share, against Java's: Double.toString on every value of tests/delivery/java_doubles.txt (made by
// oracle/ref/JavaDoubles.java, JDK 21), and a manifest-shaped document read and written back as ConvertDelivery does.
#include <cstdio>
#include <cstring>
#include <string>

#include "../tools/delivery/json.h"
#include "harness.h"

namespace {

TEST(java_double_matches_javas_double_tostring) {
    FILE* f = fopen("tests/delivery/java_doubles.txt", "r");
    CHECK(f != nullptr);
    char line[256];
    int n = 0, bad = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned long long bits = 0; char want[128] = {0};
        if (sscanf(line, "%llx %127s", &bits, want) != 2) continue;
        double d; memcpy(&d, &bits, 8);
        const std::string got = json::java_double(d);
        if (got != want) { if (bad++ < 5) fprintf(stderr, "  %016llx: java %s, ours %s\n", bits, want, got.c_str()); }
        n++;
    }
    fclose(f);
    CHECK(n > 8000);
    CHECK_EQ(bad, 0);
}

TEST(json_round_trip_keeps_order_types_and_escapes) {
    const std::string in = "{\"b\": 1, \"a\": [1, 2.5, -3e-7, \"x\\u00e9\\n\", true, null], \"c\": {\"z\": {}, \"y\": []}, \"b\": 7}";
    std::string err;
    json::Ptr v = json::parse(in, err);
    CHECK(err.empty());
    const std::string want = "{\n  \"b\": 7,\n  \"a\": [1, 2.5, -3.0E-7, \"x\xc3\xa9\\n\", true, null],\n  \"c\": {\n    \"z\": {},\n    \"y\": []\n  }\n}";
    CHECK(json::write(v, "") == want);
}

}  // namespace
