// Fake watchdog for the host tests. The real header needs the STM32 HAL.
#pragma once

namespace hal {

class MultiWatchdog {
public:
    void kick([[maybe_unused]] bool hardware = true) {}
};

} // namespace hal
