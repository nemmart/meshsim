// slotconf: a SlotConf script (tests/ref/SlotConf.java) replayed on two units::SlotCore; the observations on stdout.
#include "slotconf.h"
#include "portconf.h"
int main(int argc, char** a) {
    if (argc < 2) { std::fprintf(stderr, "usage: slotconf SCRIPT\n"); return 2; } bool ok; std::string s = portconf::read_command(std::string("cat ") + a[1], &ok); std::string o = slotconf::run(s); fwrite(o.data(), 1, o.size(), stdout); }
