#pragma once
#include <bsod/bsod.h>
enum class ErrCode {
    ERR_MECHANICAL_TOOLCHANGER,
    ERR_MECHANICAL_UNEXPECTED_TOOL,
};
extern "C" [[noreturn]] void fatal_error(const char *error, const char *module);
[[noreturn]] void fatal_error(const ErrCode error_code, ...);
