/// @file
/// Fakes for the ToolMapper and SpoolJoin host tests: enabled tools and a record of the toolchanges.
#pragma once
#include <bitset>
#include <optional>
#include <variant>
#include <vector>
#include <tool_index.hpp>

struct FakeTools {
    std::bitset<8> enabled = std::bitset<8>().set();
    std::vector<std::variant<VirtualToolIndex, PhysicalToolIndex, NoTool>> tool_changes;
    bool tool_change_result = true;
};
extern FakeTools fake_tools;
