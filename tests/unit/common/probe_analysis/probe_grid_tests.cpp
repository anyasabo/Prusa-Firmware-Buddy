#include <catch2/catch_test_macros.hpp>

#include <set>
#include <utility>
#include <vector>
#include "probe_grid.hpp"

using namespace buddy::probe;
using Point = std::pair<int, int>;

// The CORE One L and CORE One: 27 x 27 mesh, border 1, a probe each 3 indices (9 x 9 major points).
static constexpr GridLayout core_one_l { 27, 27, 1, 3 };

static std::vector<Point> order(const GridLayout &g) {
    std::vector<Point> out;
    for_each_major_grid_point(g, [&](int x, int y) {
        out.emplace_back(x, y);
        return true;
    });
    return out;
}

TEST_CASE("major grid points: each one once, inside the border", "[probe_grid]") {
    const auto pts = order(core_one_l);
    REQUIRE(pts.size() == 81);
    const std::set<Point> unique(pts.begin(), pts.end());
    CHECK(unique.size() == 81);
    for (auto [x, y] : pts) {
        CHECK((x - 1) % 3 == 0);
        CHECK((y - 1) % 3 == 0);
        CHECK(x >= 1);
        CHECK(x <= 25);
        CHECK(y >= 1);
        CHECK(y <= 25);
    }
}

TEST_CASE("major grid points: serpentine from the back row", "[probe_grid]") {
    const auto pts = order(core_one_l);
    // Row y = 25 (row index 8, even) goes from high X to low X.
    CHECK(pts.front() == Point { 25, 25 });
    CHECK(pts[8] == Point { 1, 25 });
    // Row y = 22 (row index 7, odd) goes back from low X to high X.
    CHECK(pts[9] == Point { 1, 22 });
    CHECK(pts[17] == Point { 25, 22 });
    // The last row is y = 1 (row index 0, even): high X to low X.
    CHECK(pts.back() == Point { 1, 1 });
    // Each step moves to a neighbour major point: one step in X, or one row down at a row end.
    for (size_t i = 1; i < pts.size(); i++) {
        const int dx = std::abs(pts[i].first - pts[i - 1].first);
        const int dy = pts[i - 1].second - pts[i].second;
        CHECK(((dx == 3 && dy == 0) || (dx == 0 && dy == 3)));
    }
}

TEST_CASE("major grid points: the visitor can stop the walk", "[probe_grid]") {
    int calls = 0;
    for_each_major_grid_point(core_one_l, [&](int, int) {
        return ++calls < 5;
    });
    CHECK(calls == 5);
}

TEST_CASE("uneven bed threshold", "[probe_grid]") {
    CHECK_FALSE(mesh_is_uneven(-1.0f, 0.5f, 2.0f));
    CHECK(mesh_is_uneven(-1.5f, 0.5f, 2.0f));
    // 2026-11-21 on a CORE One L: -2.349 to 0.061 triggered the Z calibration prompt.
    CHECK(mesh_is_uneven(-2.348607f, 0.060622f, 2.0f));
}
