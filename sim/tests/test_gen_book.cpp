// test_gen_book.cpp (WORKER23; P5, D40): gen_book (v1.3's gen_book.py in C++) reproduces the committed fw/RECIPES.md
// byte for byte from RECIPES.tmpl.md and the recipes' sources; and book.h's region() on the cases regions.py's
// patterns decide (checked against the Python before it was removed). Run from sim/.
#include <unistd.h>

#include <cstdio>
#include <string>

#include "../fw/recipes/book.h"
#include "harness.h"

namespace {
std::string self_dir() {
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return ".";
    std::string p(buf, size_t(n));
    return p.substr(0, p.rfind('/'));
}
}  // namespace

TEST(gen_book_reproduces_RECIPES_md) {
    const std::string cmd = self_dir() + "/gen_book --check > /dev/null 2>&1";
    CHECK_EQ(std::system(cmd.c_str()), 0);
}

TEST(region_as_regions_py) {
    bool f = false;
    // the de-indent: the least indent of the lines with text; blank lines don't count, short ones become empty
    CHECK(book::region("x\n    /* RECIPE a */\n    int a;\n\n      b();\n    /* END */\n", "a", f) == "int a;\n\n  b();" && f);
    // trailing blank lines before END stay out (the first newline of the run ends the region)
    CHECK(book::region("/* RECIPE b */\nx\n\n\n/* END */", "b", f) == "x" && f);
    // the first marker whose END follows
    CHECK(book::region("/* RECIPE c */\n1\n/* END */ /* RECIPE c */\n2\n/* END */", "c", f) == "1" && f);
    // absent, and a name that is only a prefix of another
    book::region("/* RECIPE dd */\n1\n/* END */", "d", f); CHECK(!f);
    // the template's lines
    std::string n, fl;
    CHECK(book::tmpl_line("@@copy rcp_copy.c@@", n, fl) && n == "copy" && fl == "rcp_copy.c");
    CHECK(!book::tmpl_line("@@copy  rcp_copy.c@@", n, fl) && !book::tmpl_line("@@copy@@", n, fl) && !book::tmpl_line(" @@a b@@", n, fl));
}
