/// @file
/// Property tests of ToolMapper and SpoolJoin with random operation sequences.
#include "fakes.hpp"

#include <module/prusa/tool_mapper.hpp>
#include <module/prusa/spool_join.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <map>
#include <random>
#include <set>

namespace {

constexpr int N = 8;

GcodeToolIndex G(int i) { return GcodeToolIndex::from_raw(i); }
VirtualToolIndex V(int i) { return VirtualToolIndex::from_raw(i); }

std::optional<int> mapped(const ToolMapper &m, int g, bool ignore_enabled = true) {
    return match(
        m.to_virtual(G(g), ignore_enabled),
        [](VirtualToolIndex v) -> std::optional<int> { return v.to_raw(); },
        [](ToolNotMapped) -> std::optional<int> { return std::nullopt; });
}

std::optional<int> gcode_of(const ToolMapper &m, int v) {
    return match(
        m.to_gcode(V(v)),
        [](GcodeToolIndex g) -> std::optional<int> { return g.to_raw(); },
        [](ToolNotMapped) -> std::optional<int> { return std::nullopt; });
}

/// Each virtual tool has at most one gcode tool, and to_gcode() is the inverse of to_virtual()
void check_mapper_invariants(const ToolMapper &m) {
    std::map<int, int> owner;
    for (int g = 0; g < N; ++g) {
        if (auto v = mapped(m, g)) {
            INFO("gcode " << g << " -> virtual " << *v);
            CHECK(!owner.contains(*v));
            owner[*v] = g;
        }
    }
    for (int v = 0; v < N; ++v) {
        INFO("virtual " << v);
        const auto it = owner.find(v);
        CHECK(gcode_of(m, v) == (it == owner.end() ? std::nullopt : std::optional<int>(it->second)));
    }
}

using Edges = std::set<std::pair<int, int>>;

Edges edges(const SpoolJoin &j) {
    Edges e;
    for (uint8_t i = 0; i < j.get_num_joins(); ++i) {
        const auto c = j.get_join_nr(i);
        e.insert({ c.spool_1, c.spool_2 });
    }
    return e;
}

/// The joins form disjoint chains: valid spools, out-degree and in-degree at most 1, no cycle.
void check_join_invariants(const SpoolJoin &j) {
    const Edges e = edges(j);
    CHECK(e.size() == j.get_num_joins()); // no duplicate join
    std::map<int, int> next, prev;
    for (auto [a, b] : e) {
        INFO("join " << a << " -> " << b);
        CHECK(a < N);
        CHECK(b < N);
        CHECK(a != b);
        CHECK(!next.contains(a));
        CHECK(!prev.contains(b));
        next[a] = b;
        prev[b] = a;
    }
    for (int s = 0; s < N; ++s) {
        // Follow the chain: it must end in at most N steps (no cycle), at get_last_spool_2_from_chain()
        int cur = s;
        int steps = 0;
        while (next.contains(cur) && steps <= N) {
            cur = next[cur];
            ++steps;
        }
        INFO("spool " << s);
        CHECK(steps <= N);
        CHECK(j.get_last_spool_2_from_chain(V(s)).to_raw() == cur);

        cur = s;
        steps = 0;
        while (prev.contains(cur) && steps <= N) {
            cur = prev[cur];
            ++steps;
        }
        CHECK(j.get_first_spool_1_from_chain(V(s)).to_raw() == cur);

        const auto s2 = j.get_spool_2(V(s));
        CHECK(s2.has_value() == next.contains(s));
        if (s2) {
            CHECK(s2->to_raw() == next[s]);
        }
    }
}

} // namespace

TEST_CASE("tool_mapper: random operations keep the mapping one to one", "[property]") {
    const auto seed = GENERATE(range(1u, 51u));
    CAPTURE(seed);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> tool(0, N - 1), op(0, 9);

    fake_tools.enabled.set();
    ToolMapper m;
    m.reset();
    for (int step = 0; step < 100; ++step) {
        const int o = op(rng);
        const int g = tool(rng), v = tool(rng);
        CAPTURE(step, o, g, v);
        if (o < 6) {
            const bool expected = fake_tools.enabled.test(v);
            CHECK(m.set_mapping(G(g), V(v)) == expected);
            if (expected) {
                CHECK(mapped(m, g) == v);
            }
        } else if (o == 6) {
            m.set_unassigned(G(g));
            CHECK(!mapped(m, g));
        } else if (o == 7) {
            fake_tools.enabled.flip(v);
        } else if (o == 8) {
            m.set_enable(!m.is_enabled());
        } else if (step % 25 == 0) {
            m.set_all_unassigned();
        }
        check_mapper_invariants(m);
    }

    // Power panic round trip
    ToolMapper::serialized_state_t state;
    m.serialize(state);
    ToolMapper restored;
    restored.deserialize(state);
    CHECK(restored.is_enabled() == m.is_enabled());
    for (int g = 0; g < N; ++g) {
        CHECK(mapped(restored, g) == mapped(m, g));
    }
    fake_tools.enabled.set();
}

TEST_CASE("tool_mapper: set_mapping takes the virtual tool from another gcode tool") {
    // This is the GUI behaviour (one gcode tool per virtual tool). The Connect start_print handler
    // (src/connect/marlin_printer.cpp handle_tool_mapping) calls set_mapping() for each gcode tool
    // and does not check for duplicates: a request that maps T0 and T1 to tool 0 leaves T0 unmapped.
    fake_tools.enabled.set();
    ToolMapper m;
    m.set_all_unassigned();
    m.set_enable(true);
    CHECK(m.set_mapping(G(0), V(0)));
    CHECK(m.set_mapping(G(1), V(0)));
    CHECK(!mapped(m, 0));
    CHECK(mapped(m, 1) == 0);
}

TEST_CASE("spool_join: random operations keep disjoint chains", "[property]") {
    const auto seed = GENERATE(range(1u, 101u));
    CAPTURE(seed);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> tool(0, N - 1), op(0, 9);

    fake_tools.enabled.set();
    SpoolJoin j;
    for (int step = 0; step < 100; ++step) {
        const int o = op(rng);
        const int a = tool(rng), b = tool(rng);
        CAPTURE(step, o, a, b);
        if (o < 6) {
            // Oracle for add_join(): the rules of the doc comment, written from the chain model
            const int end_a = j.get_last_spool_2_from_chain(V(a)).to_raw();
            const bool b_has_prev = j.get_first_spool_1_from_chain(V(b)).to_raw() != b;
            const bool same_chain = j.get_first_spool_1_from_chain(V(a)) == j.get_first_spool_1_from_chain(V(b));
            const bool expected = j.get_num_joins() < N && fake_tools.enabled.test(a) && fake_tools.enabled.test(b) && a != b && !b_has_prev && !same_chain && end_a != b;
            const auto first_a = j.get_first_spool_1_from_chain(V(a));
            const bool ok = j.add_join(V(a), V(b));
            CHECK(ok == expected);
            if (ok) {
                // The chain of b continues at the end of the chain of a
                CHECK(j.get_spool_2(V(end_a)) == std::optional(V(b)));
                CHECK(j.get_first_spool_1_from_chain(V(b)) == first_a);
            }
        } else if (o == 6) {
            const bool had = j.get_spool_2(V(a)).has_value() || j.get_first_spool_1_from_chain(V(a)).to_raw() != a;
            CHECK(j.reroute_joins_containing(V(a)) == had);
            CHECK(!j.get_spool_2(V(a)).has_value());
            CHECK(j.get_first_spool_1_from_chain(V(a)).to_raw() == a);
        } else if (o == 7) {
            j.remove_join_chain_containing(V(a));
            CHECK(!j.get_spool_2(V(a)).has_value());
            CHECK(j.get_first_spool_1_from_chain(V(a)).to_raw() == a);
        } else if (o == 8 && step % 30 == 0) {
            j.reset();
            CHECK(j.get_num_joins() == 0);
        } else if (o == 9) {
            fake_tools.enabled.flip(a);
        }
        check_join_invariants(j);
    }

    // Power panic round trip (deserialize() skips joins of disabled tools)
    fake_tools.enabled.set();
    SpoolJoin::serialized_state_t state;
    j.serialize(state);
    SpoolJoin restored;
    restored.deserialize(state);
    CHECK(edges(restored) == edges(j));
}

TEST_CASE("spool_join: do_join moves the gcode tool to the next spool") {
    fake_tools.enabled.set();
    fake_tools.tool_changes.clear();
    tool_mapper.reset();
    tool_mapper.set_all_unassigned();
    tool_mapper.set_enable(true);
    spool_join.reset();

    REQUIRE(tool_mapper.set_mapping(G(0), V(2)));
    REQUIRE(spool_join.add_join(V(2), V(5)));

    CHECK(spool_join.do_join(V(2)));
    CHECK(mapped(tool_mapper, 0) == 5);
    CHECK(!gcode_of(tool_mapper, 2));
    REQUIRE(fake_tools.tool_changes.size() == 1);
    CHECK(std::get<VirtualToolIndex>(fake_tools.tool_changes[0]) == V(5));
    check_mapper_invariants(tool_mapper);
}

// KNOWN BUG (fails, tagged [!shouldfail]): see the comments in the test.
TEST_CASE("spool_join: a join target that is also mapped to another gcode tool", "[!shouldfail][known_bug]") {
    // Sequence of handle_tool_mapping() in src/connect/marlin_printer.cpp for the Connect request
    // tool_mapping = [[0, 1], [1]]: T0 -> tool 0 with a spool join to tool 1, and T1 -> tool 1.
    // The GUI (frame_tool_mapping.cpp) disconnects the virtual tool before it adds a join; Connect does not.
    fake_tools.enabled.set();
    fake_tools.tool_changes.clear();
    tool_mapper.set_all_unassigned();
    tool_mapper.set_enable(true);
    spool_join.reset();
    REQUIRE(tool_mapper.set_mapping(G(0), V(0)));
    REQUIRE(spool_join.add_join(V(0), V(1)));
    REQUIRE(tool_mapper.set_mapping(G(1), V(1)));

    // The configuration is already inconsistent: tool 1 is a join target and the tool of T1
    CHECK(!gcode_of(tool_mapper, 1));

    // Tool 0 runs out: T0 continues on tool 1. set_mapping() takes tool 1 from T1, and a later T1 in the
    // gcode has no tool.
    CHECK(spool_join.do_join(V(0)));
    CHECK(mapped(tool_mapper, 0) == 1);
    CHECK(mapped(tool_mapper, 1) == 1);
}
