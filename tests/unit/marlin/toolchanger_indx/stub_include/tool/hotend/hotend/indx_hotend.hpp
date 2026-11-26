/// Fake of src/common/tool/hotend/hotend/indx_hotend.hpp. It keeps the thermal-management
/// state and the invariant check of the real class (indx_hotend.cpp).
#pragma once
#include <tool_index.hpp>
#include <bsod/bsod.h>

class IndxHotend {
    friend class PrusaToolChanger;
    friend class PrusaToolChangerUtils;
    friend struct FakeWorld;

public:
    struct Tool {
        IndxHotend &hotend() { return *h; }
        IndxHotend *h;
    };
    static Tool indx_tool(PhysicalToolIndex tool);
    static void assert_thermally_managed_invariant(std::variant<PhysicalToolIndex, NoTool> expected_managed);
    bool is_thermally_managed() const { return is_thermally_managed_; }

    int start_count = 0;
    int stop_count = 0;

private:
    void start_heating() {
        assert_thermally_managed_invariant(NoTool {});
        is_thermally_managed_ = true;
        ++start_count;
    }
    void stop_heating() {
        is_thermally_managed_ = false;
        ++stop_count;
        assert_thermally_managed_invariant(NoTool {});
    }
    bool is_thermally_managed_ = false;
};
