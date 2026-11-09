/// @file
/// Compile-time check of the text length of metric_record_custom() and metric_record_custom_at_time().
///
/// A custom metric value must fit in metric_point_t::value_custom. A longer text is cut, and the
/// point then carries only the error "value too long". Usually this occurs only when a counter
/// becomes large, long after the change was tested. Thus, in C++, metric_record_custom() and
/// metric_record_custom_at_time() are macros. They find the longest text that the format can give
/// for the types of the arguments, and the build fails if it does not fit. The macros then call
/// the C function with the same literal format, so -Wformat checks the types as before.
///
/// The longest text uses the largest value of each type. For example %u gives 10 characters with
/// an unsigned int and 3 with a uint8_t, and %d gives 11 with an int. Thus:
/// - %s needs a precision (%.7s).
/// - %f counts as at most 9 integer digits (float_integer_digits). This is an assumption, not a
///   bound: a larger value gives a longer text. %e and %g are bounded.
/// - '*' as width or precision is not supported.
/// If a format cannot fit, put the fields in two or more metrics.
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace metric_custom_format {

/// Characters of a custom value, without the terminating zero.
inline constexpr size_t max_length = sizeof(metric_point_t::value_custom) - 1;

/// The integer digits that %f counts for: the check assumes |value| < 1e9. A double can have 309
/// integer digits, but a metric with such values is not useful. %e and %g have no such assumption.
inline constexpr size_t float_integer_digits = 9;

struct ArgInfo {
    bool is_floating;
    bool is_double; ///< double or long double: a larger exponent than float
    bool is_integral;
    bool is_signed;
    size_t size;
};

template <class T>
consteval ArgInfo arg_info() {
    using D = std::remove_cvref_t<T>;
    constexpr bool integral = std::is_integral_v<D> || std::is_enum_v<D>;
    bool is_signed = false;
    if constexpr (std::is_enum_v<D>) {
        is_signed = std::is_signed_v<std::underlying_type_t<D>>;
    } else {
        is_signed = std::is_signed_v<D>;
    }
    return { std::is_floating_point_v<D>, std::is_floating_point_v<D> && sizeof(D) > sizeof(float), integral, is_signed, sizeof(D) };
}

/// Bytes of the value that an integer conversion can show. An argument narrower than the
/// conversion keeps its range, except a signed value with an unsigned conversion: -1 with %u
/// gives 4294967295.
consteval size_t shown_bytes(const ArgInfo &info, size_t conversion_bytes, bool unsigned_conversion) {
    if (!info.is_integral || info.size >= conversion_bytes || (info.is_signed && unsigned_conversion)) {
        return conversion_bytes;
    }
    return info.size;
}

template <class... Args>
struct TypeList {};

/// Not constexpr: a call in a constant evaluation stops the build, and the compiler shows the
/// line of the call with its message. The firmware builds with -fno-exceptions, so no throw.
inline void format_error(const char *) {}

/// Not defined: only for decltype. Gives the types of the arguments as the variadic call sees them.
template <class... Args>
TypeList<std::decay_t<Args>...> types_of(Args &&...);

/// Decimal digits of the largest unsigned value of an integer of this many bytes.
consteval size_t unsigned_digits(size_t bytes) {
    size_t digits = 0;
    switch (bytes) { // the digits of 2^(8 * bytes) - 1
    case 1:
        digits = 3;
        break;
    case 2:
        digits = 5;
        break;
    case 4:
        digits = 10;
        break;
    case 8:
        digits = 20;
        break;
    default:
        format_error("metric_record_custom: unsupported integer size");
    }
    return digits;
}

/// Characters of the largest text that the format can give with these argument types.
/// It calls format_error() (thus a compile error) for a format that it cannot bound.
template <class... Args>
consteval size_t worst_case_length(const char *fmt) {
    constexpr ArgInfo args[] = { arg_info<Args>()..., ArgInfo {} };
    constexpr size_t arg_count = sizeof...(Args);
    size_t length = 0;
    size_t arg = 0;
    for (const char *p = fmt; *p;) {
        if (*p != '%') {
            ++length;
            ++p;
            continue;
        }
        ++p;
        if (*p == '%') {
            ++length;
            ++p;
            continue;
        }
        bool sign = false; // '+' or ' ': a sign also for positive values
        bool alternate = false; // '#'
        for (;; ++p) {
            if (*p == '+' || *p == ' ') {
                sign = true;
            } else if (*p == '#') {
                alternate = true;
            } else if (*p != '-' && *p != '0') {
                break;
            }
        }
        size_t width = 0;
        if (*p == '*') {
            format_error("metric_record_custom: '*' width is not supported");
        }
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + static_cast<size_t>(*p++ - '0');
        }
        bool has_precision = false;
        size_t precision = 0;
        if (*p == '.') {
            ++p;
            has_precision = true;
            if (*p == '*') {
                format_error("metric_record_custom: '*' precision is not supported");
            }
            while (*p >= '0' && *p <= '9') {
                precision = precision * 10 + static_cast<size_t>(*p++ - '0');
            }
        }
        size_t int_bytes = sizeof(int);
        bool long_double = false;
        if (p[0] == 'h' && p[1] == 'h') {
            int_bytes = 1;
            p += 2;
        } else if (p[0] == 'l' && p[1] == 'l') {
            int_bytes = sizeof(long long);
            p += 2;
        } else if (*p == 'h') {
            int_bytes = sizeof(short);
            ++p;
        } else if (*p == 'l') {
            int_bytes = sizeof(long);
            ++p;
        } else if (*p == 'z') {
            int_bytes = sizeof(size_t);
            ++p;
        } else if (*p == 'j') {
            int_bytes = sizeof(intmax_t);
            ++p;
        } else if (*p == 't') {
            int_bytes = sizeof(ptrdiff_t);
            ++p;
        } else if (*p == 'L') {
            long_double = true;
            ++p;
        }
        const char conversion = *p++;
        if (arg >= arg_count) {
            format_error("metric_record_custom: fewer arguments than conversions");
        }
        const ArgInfo info = args[arg++];
        size_t field = 0;
        switch (conversion) {
        case 'd':
        case 'i': {
            const size_t bytes = shown_bytes(info, int_bytes, false);
            // The most negative value has a '-'. An unsigned argument narrower than int has none.
            const bool minus = !info.is_integral || info.is_signed || bytes >= int_bytes;
            field = unsigned_digits(bytes) + ((minus || sign) ? 1 : 0);
            break;
        }
        case 'u':
            field = unsigned_digits(shown_bytes(info, int_bytes, true)) + (sign ? 1 : 0);
            break;
        case 'x':
        case 'X':
            field = 2 * shown_bytes(info, int_bytes, true) + (alternate ? 2 : 0);
            break;
        case 'o':
            field = (8 * shown_bytes(info, int_bytes, true) + 2) / 3 + (alternate ? 1 : 0);
            break;
        case 'c':
            field = 1;
            break;
        case 'p':
            field = 2 + 2 * sizeof(void *);
            break;
        case 's':
            if (!has_precision) {
                format_error("metric_record_custom: %s needs a precision, for example %.7s");
            }
            field = precision;
            break;
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G': {
            if (!info.is_floating) {
                format_error("metric_record_custom: a floating conversion needs a float or double argument");
            }
            const bool is_double = info.is_double || long_double;
            const size_t prec = has_precision ? precision : 6;
            // The text of inf and nan is shorter than these.
            if (conversion == 'f' || conversion == 'F') {
                field = 1 + float_integer_digits + ((prec > 0 || alternate) ? 1 + prec : 0);
            } else if (conversion == 'e' || conversion == 'E') {
                field = 1 + 1 + ((prec > 0 || alternate) ? 1 + prec : 0) + 2 + (is_double ? 3 : 2);
            } else {
                // %g: at most max(prec, 1) significant digits, a '.', and an exponent.
                field = 1 + (prec > 0 ? prec : 1) + 1 + 2 + (is_double ? 3 : 2);
            }
            break;
        }
        default:
            format_error("metric_record_custom: unsupported conversion");
        }
        length += field > width ? field : width;
    }
    if (arg != arg_count) {
        format_error("metric_record_custom: more arguments than conversions");
    }
    return length;
}

template <class List>
struct Checked;

/// A consteval constructor: thus each use with a literal format is a constant expression, and a
/// format that can give too long a text is a compile error.
template <class... Args>
struct Checked<TypeList<Args...>> {
    consteval Checked(const char *fmt) {
        if (worst_case_length<Args...>(fmt) > max_length) {
            format_error("metric_record_custom: the value can be longer than metric_point_t::value_custom; split the metric");
        }
    }
};

} // namespace metric_custom_format

// The macros check the format, then call the C functions of the same names. A function-like macro
// does not expand its own name again, and does not expand a name that is not followed by '('.
#define metric_record_custom(metric, fmt, ...)                                                                                        \
    ((void)metric_custom_format::Checked<decltype(metric_custom_format::types_of(__VA_ARGS__))>(fmt), \
        metric_record_custom(metric, fmt __VA_OPT__(, ) __VA_ARGS__))
#define metric_record_custom_at_time(metric, timestamp, fmt, ...)                                                                     \
    ((void)metric_custom_format::Checked<decltype(metric_custom_format::types_of(__VA_ARGS__))>(fmt), \
        metric_record_custom_at_time(metric, timestamp, fmt __VA_OPT__(, ) __VA_ARGS__))
