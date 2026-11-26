#pragma once
#include <cstdint>
namespace nozzle_cleaner {
enum class Sequence : uint16_t { enter_cleaner,
    purge_clean,
    clean,
    exit_cleaner };
bool load_and_execute(Sequence seq);
} // namespace nozzle_cleaner
