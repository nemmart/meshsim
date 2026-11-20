// seed.h: a test's file system from a .seed file (seed.cpp).
#pragma once

#include <string>
#include <vector>

void seed_run(const char* file);              // powers on (a format), then the file's lines; nullptr: an empty FS
extern std::vector<std::string> g_seed_open;  // the paths the seed left open
extern bool g_seed_writer;                    // one of them to write
extern std::string g_seed_known_bug;          // set: check() skipped, this printed
