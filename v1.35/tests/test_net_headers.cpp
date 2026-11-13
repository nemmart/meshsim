// The network's headers: they compile, the unit types meet the framework's requirements, and the
// link word's layout is ref's (LinkWord.java).
#include <type_traits>

#include "harness.h"
#include "units/chip.h"
#include "units/cnc.h"
#include "units/inject.h"
#include "units/net.h"
#include "units/router.h"

using namespace units;

static_assert(sim::UnitType<Router> && sim::UnitType<Injection> && sim::UnitType<Cnc> && sim::UnitType<RowCollector>
              && sim::UnitType<Endpoint>);
static_assert(std::is_move_constructible_v<Router> && std::is_move_constructible_v<Cnc>);
static_assert(sim::HasDigest<Router> && sim::HasDigest<Injection> && sim::HasDigest<Cnc> && sim::HasDigest<RowCollector>
              && sim::HasDigest<Endpoint>);

TEST(linkword_layout_is_refs) {
    // LinkWord.hdr(src 5, dst 126, mode 0, counter 3, addr 0x00001234, size 2)
    uint64_t h = lw::hdr(5, lw::BROADCAST, lw::M_WRITE, 3, 0x1234, lw::SIZE_128);
    CHECK_EQ(h, 5ull | 126ull << 7 | 3ull << 17 | 2ull << 20 | 0x1234ull << 22);
    CHECK_EQ(lw::src(h), 5u);
    CHECK_EQ(lw::dst(h), 126u);
    CHECK_EQ(lw::counter(h), 3u);
    CHECK_EQ(lw::size(h), 2u);
    CHECK_EQ(lw::addr(h), 0x1234u);
    CHECK_EQ(lw::src(lw::with_src(h | lw::TAG_TOKEN, 9)), 9u);
    CHECK((lw::with_src(h | lw::TAG_TOKEN, 9) & lw::TAG_TOKEN) != 0);
    CHECK_EQ(lw::addr(lw::hdr(0, 0, 0, 0, 0xFFFFFFFFu, 0)), 0xFFFFFFFFu);
}

TEST(mesh_neighbours_are_chips) {
    CHECK_EQ(neighbour(7, Router::TO_RIGHT), 8u);
    CHECK_EQ(neighbour(0, Router::TO_LEFT), 63u);
    CHECK_EQ(neighbour(60, Router::TO_DOWN), 4u);
    CHECK_EQ(neighbour(3, Router::TO_UP), 59u);
    CHECK_EQ(arrival_port(Router::TO_RIGHT), unsigned(Router::FROM_LEFT));
    CHECK_EQ(arrival_port(Router::TO_UP), unsigned(Router::FROM_DOWN));
}
