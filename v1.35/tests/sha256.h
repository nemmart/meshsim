// sha256.h: SHA-256 (FIPS 180-4), for the golden traces' image and data-memory digests (configuration regime).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace test {
std::string sha256_hex(const void* data, size_t n);
}
