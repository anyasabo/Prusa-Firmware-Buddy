#pragma once
#include <tool_index.hpp>
#include <mapi/parking.hpp>
namespace pause {
struct Settings {
    void SetExtruder(VirtualToolIndex) {}
    void SetRetractLength(float) {}
    void SetParkPoint(const mapi::ParkingPosition &) {}
};
} // namespace pause
class Pause {
public:
    enum class LoadType { unload_spool_join };
    static Pause &Instance();
    bool perform(LoadType, const pause::Settings &);
};
