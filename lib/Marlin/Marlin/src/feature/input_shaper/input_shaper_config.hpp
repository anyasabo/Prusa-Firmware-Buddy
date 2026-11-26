#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <printers.h>
#include <utility_extensions.hpp>
#include <utils/enum_array.hpp>

enum AxisEnum : uint8_t; // FWD declaration to avoid Marlin dependency in tests

namespace input_shaper {

enum class Type : uint8_t {
    // DO NOT CHANGE VALUES IN THIS ENUM WITHOUT CHANGING EEPROM CODE!
    // We no longer recommend ZV, EI_2HUMP & EI_3HUMP filter types with our automatic fitting

    first = 0,
    zv = first,
    zvd,
    mzv,
    ei,
    ei_2hump,
    ei_3hump,
    null,
    last = null,
    cnt
};

static constexpr EnumArray<Type, const char *, std::to_underlying(Type::cnt)> filter_names {
    { Type::zv, "ZV" },
    { Type::zvd, "ZVD" },
    { Type::mzv, "MZV" },
    { Type::ei, "EI" },
    { Type::ei_2hump, "EI_2HUMP" },
    { Type::ei_3hump, "EI_3HUMP" },
    { Type::null, "Null" }

};

static constexpr EnumArray<Type, const char *, std::to_underlying(Type::cnt)> filter_short_names {
    { Type::zv, "ZV" },
    { Type::zvd, "ZVD" },
    { Type::mzv, "MZV" },
    { Type::ei, "EI" },
    { Type::ei_2hump, "EI2" },
    { Type::ei_3hump, "EI3" },
    { Type::null, "NUL" }
};

/// Ordered and filtered filter list, for UI purposes
static constexpr std::array filter_list {
    Type::zvd,
    Type::mzv,
    Type::ei,
    Type::null,
};

static constexpr uint8_t low_freq_limit_hz = 35;
static constexpr uint8_t high_freq_limit_hz = 70;

const char *to_string(Type type);
const char *to_short_string(Type type);

static inline constexpr Type operator+(const Type lhs, const std::underlying_type<Type>::type rhs) {
    auto l_value = static_cast<std::underlying_type<Type>::type>(lhs);
    return Type(l_value + rhs);
}

inline Type &operator++(Type &type) {
    type = type + 1;
    return type;
}

struct __attribute__((packed)) AxisConfig {
    // DO NOT CHANGE LAYOUT OF THIS CLASS WITHOUT CHANGING EEPROM CODE!

    Type type;
    char _padding[3] {};
    float frequency; // Hz
    float damping_ratio;
    float vibration_reduction;

    friend auto operator<=>(const AxisConfig &, const AxisConfig &) = default;
};

struct __attribute__((packed)) WeightAdjustConfig {
    // DO NOT CHANGE LAYOUT OF THIS CLASS WITHOUT CHANGING EEPROM CODE!

    float frequency_delta; // Hz
    float mass_limit; // g

    friend auto operator<=>(const WeightAdjustConfig &, const WeightAdjustConfig &) = default;
};

struct Config {
    std::optional<AxisConfig> axis[3];
    std::optional<WeightAdjustConfig> weight_adjust_y;
};

// NOTE: On CoreXY the following apply to the logical movement axis, not the AB directions
inline constexpr AxisConfig axis_x_default {
    // DO NOT CHANGE DEFAULTS WITHOUT CHANGING EEPROM CODE!

#if PRINTER_IS_PRUSA_MINI()
    .type = Type::mzv,
    .frequency = 118.2f,
#elif PRINTER_IS_PRUSA_XL()
    .type = Type::mzv,
    .frequency = 35.8f,
#elif PRINTER_IS_PRUSA_COREONE()
    .type = Type::mzv,
    .frequency = 60.0,
#elif PRINTER_IS_PRUSA_COREONEL()
    .type = Type::mzv,
    .frequency = 56.0,
#elif PRINTER_IS_PRUSA_iX()
    .type = Type::mzv,
    .frequency = 53.0,
#else
    .type = Type::mzv,
    .frequency = 50.7f,
#endif
    .damping_ratio = 0.1f,
    .vibration_reduction = 20.0f,
};
inline constexpr AxisConfig axis_y_default {
    // DO NOT CHANGE DEFAULTS WITHOUT CHANGING EEPROM CODE!

#if PRINTER_IS_PRUSA_MINI()
    .type = Type::mzv,
    .frequency = 32.8f,
#elif PRINTER_IS_PRUSA_XL()
    .type = Type::mzv,
    .frequency = 35.4f,
#elif PRINTER_IS_PRUSA_COREONE()
    .type = Type::mzv,
    .frequency = 48.0,
#elif PRINTER_IS_PRUSA_COREONEL()
    .type = Type::mzv,
    .frequency = 42.0,
#elif PRINTER_IS_PRUSA_iX()
    .type = Type::mzv,
    .frequency = 42.0,
#else
    .type = Type::mzv,
    .frequency = 40.6f,
#endif
    .damping_ratio = 0.1f,
    .vibration_reduction = 20.0f,
};
inline constexpr AxisConfig axis_z_default {
    // Z axis does not default to IS and is not currently saved on eeprom.
    // Filter type/frequency is only set at runtime and can be freely changed.

    .type = Type::null,
    .frequency = 0.,
    .damping_ratio = 0.,
    // The EI shapers divide by the vibration reduction (v_tol = 1 / R). 0 gave NaN amplitudes
    // for "M593 Z T3 F40". 20 is the default of the X and Y axes and of Klipper.
    .vibration_reduction = 20.0f,
};

inline constexpr AxisConfig axis_defaults[3] = { axis_x_default, axis_y_default, axis_z_default };

inline constexpr bool weight_adjust_enabled_default = {
// DO NOT CHANGE DEFAULTS WITHOUT CHANGING EEPROM CODE!
#if PRINTER_IS_PRUSA_XL() || PRINTER_IS_PRUSA_iX() || PRINTER_IS_PRUSA_COREONE() || PRINTER_IS_PRUSA_COREONEL()
    false
#else
    true
#endif
};

inline constexpr WeightAdjustConfig weight_adjust_y_default {
    // DO NOT CHANGE DEFAULTS WITHOUT CHANGING EEPROM CODE!
#if PRINTER_IS_PRUSA_XL() || PRINTER_IS_PRUSA_iX() || PRINTER_IS_PRUSA_COREONE() || PRINTER_IS_PRUSA_COREONEL()
    .frequency_delta = 0,
#else
    .frequency_delta = -20.0f,
#endif
    .mass_limit = 800.0f,
};

Config &current_config();

void set_config_for_m74(const AxisEnum axis, const std::optional<AxisConfig> &axis_config);
void set_config_for_m74(const std::optional<WeightAdjustConfig> &next_config);
Config get_config_for_m74();

void init();
std::optional<AxisConfig> get_axis_config(const AxisEnum axis);
void set_axis_config(const AxisEnum axis, std::optional<AxisConfig> axis_config);
void set_axis_y_weight_adjust(std::optional<WeightAdjustConfig> wa_config);

constexpr float frequency_safe_min = 10.0;
#if PRINTER_IS_PRUSA_MINI()
constexpr float frequency_safe_max = 150.0;
#else
constexpr float frequency_safe_max = 100.0;
#endif

constexpr float clamp_frequency_to_safe_values(float frequency) {
    return std::clamp(frequency, frequency_safe_min, frequency_safe_max);
}

/// Largest damping ratio that M593 accepts. The shaper formulas divide by sqrt(1 - damping_ratio^2),
/// and input_shaper::get() calls bsod() at 1. Klipper uses the same limit for its classic shapers
/// (https://github.com/Klipper3d/klipper/commit/768b19e8be6458f43615423f48b74cf1e3ad1a9d).
/// The lower Klipper limits for EI (0.4, 0.3, 0.2) are for its optimized EI formulas, not for the
/// classic formulas here.
constexpr float damping_ratio_max = 0.99f;

/// Limits of the vibration reduction that M593 accepts. The EI shapers use v_tol = 1 / R, and their
/// formulas are valid only for v_tol <= 1. Below 1, 2HUMP EI gives NaN amplitudes. From
/// approximately 1e23, (1/R)^2 is 0 in float, and 2HUMP EI divides 0 by 0.
constexpr float vibration_reduction_min = 1.f;
constexpr float vibration_reduction_max = 1e6f;

/// Returns true if the shaper of type @p type uses the vibration reduction (the EI shapers).
constexpr bool uses_vibration_reduction(Type type) {
    return type == Type::ei || type == Type::ei_2hump || type == Type::ei_3hump;
}

/// Returns true if M593 accepts the damping ratio @p damping_ratio (parameter D).
constexpr bool is_valid_damping_ratio(float damping_ratio) {
    return damping_ratio >= 0.f && damping_ratio <= damping_ratio_max;
}

/// Returns true if M593 accepts the vibration reduction @p vibration_reduction (parameter R).
constexpr bool is_valid_vibration_reduction(float vibration_reduction) {
    return vibration_reduction >= vibration_reduction_min && vibration_reduction <= vibration_reduction_max;
}

/// Returns true if @p type is a value of the enum.
constexpr bool is_valid_type(Type type) {
    return std::to_underlying(type) <= std::to_underlying(Type::last);
}

/// Returns true if M593 can apply @p config, the merge of the given parameters and the previous
/// configuration of the axis.
constexpr bool is_valid_axis_config(const AxisConfig &config) {
    return is_valid_type(config.type)
        && is_valid_damping_ratio(config.damping_ratio)
        && (!uses_vibration_reduction(config.type) || is_valid_vibration_reduction(config.vibration_reduction));
}

} // namespace input_shaper
