#pragma once
#include <bitset>
#include <variant>
#include <tool_index.hpp>
#include <module/prusa/dock_position.hpp>
#include <module/prusa/tool_offset.hpp>

template <typename T>
struct FakeStoreItem {
    T value {};
    T get() const { return value; }
    void set(const T &v) { value = v; }
};

/// Fake of the config store items that the toolchanger uses.
struct FakeConfigStore {
    FakeStoreItem<bool> indx_last_picked_tool_valid;
    std::variant<PhysicalToolIndex, NoTool> indx_last_picked_tool { NoTool {} };
    FakeStoreItem<std::bitset<8>> indx_dock_calibrated_mask;
    FakeStoreItem<bool> stealth_mode;
    std::array<DockPosition, 8> dock_positions {};
    std::array<ToolOffset, 8> tool_offsets {};

    std::variant<PhysicalToolIndex, NoTool> get_indx_last_picked_tool() const { return indx_last_picked_tool; }
    void set_indx_last_picked_tool(std::variant<PhysicalToolIndex, NoTool> t) { indx_last_picked_tool = t; }
    DockPosition get_dock_position(PhysicalToolIndex t) const { return dock_positions[t.to_raw()]; }
    void set_dock_position(PhysicalToolIndex t, DockPosition p) { dock_positions[t.to_raw()] = p; }
    ToolOffset get_tool_offset(PhysicalToolIndex t) const { return tool_offsets[t.to_raw()]; }
    void set_tool_offset(PhysicalToolIndex t, ToolOffset o) { tool_offsets[t.to_raw()] = o; }
};
FakeConfigStore &config_store();
