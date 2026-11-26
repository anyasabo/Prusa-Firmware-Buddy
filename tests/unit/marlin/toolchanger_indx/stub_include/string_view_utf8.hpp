#pragma once

#include <cstddef>

struct string_view_utf8 {};

struct StringViewUtf8ParamBase {};

template <size_t>
struct StringViewUtf8Parameters : public StringViewUtf8ParamBase {};
