#pragma once
// Stand-in for src/common/tool_index.hpp: a printer with one tool (CORE One L without INDX).
#include <array>
#include <cstdint>
#include <optional>
#include <variant>

#include <utils/array_extensions.hpp>
#include <utils/storage/strong_index_array.hpp>
#include <utils/variant_utils.hpp>

struct NoTool {
    constexpr bool operator==(const NoTool &) const { return true; }
};

struct VirtualToolIndex {
    static constexpr uint8_t count = 1;
    uint8_t raw = 0;
    constexpr uint8_t to_raw() const { return raw; }
    static constexpr uint8_t to_raw_static(VirtualToolIndex t) { return t.raw; }
    static std::array<VirtualToolIndex, 1> all() { return { VirtualToolIndex {} }; }
};

struct PhysicalToolIndex {
    static constexpr uint8_t count = 1;
    uint8_t raw = 0;
    constexpr uint8_t to_raw() const { return raw; }
    std::variant<VirtualToolIndex, NoTool> currently_selected_virtual_tool() const { return VirtualToolIndex {}; }
    static std::optional<PhysicalToolIndex> currently_selected_opt() { return PhysicalToolIndex {}; }
};
