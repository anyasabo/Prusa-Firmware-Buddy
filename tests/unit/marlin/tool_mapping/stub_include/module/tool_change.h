#pragma once
#include <tool_index.hpp>
#include <variant>
enum class tool_return_t { no_return,
    to_current,
    to_destination,
    dock_backoff };
enum class tool_change_lift_t { no_lift,
    mbl_only_lift,
    full_lift };
bool tool_change(const std::variant<VirtualToolIndex, PhysicalToolIndex, NoTool> new_tool, tool_return_t return_type = tool_return_t::to_current, tool_change_lift_t z_lift = tool_change_lift_t::full_lift, bool z_return = true);
