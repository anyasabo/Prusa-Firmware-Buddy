#pragma once
#include <bsod/bsod.h>
extern "C" [[noreturn]] void fatal_error(const char *error, const char *module);
