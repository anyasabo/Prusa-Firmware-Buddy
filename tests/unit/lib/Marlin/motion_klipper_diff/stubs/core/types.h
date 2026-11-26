#pragma once
// Stand-in for core/types.h: the real file needs the full Conditionals chain. The vector type is
// the real core/vector.hpp.
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "../inc/MarlinConfigPre.h"
#include "vector.hpp"

#define XYZE_N       4
#define NUM_AXES     3
#define LOGICAL_AXES 4

enum AxisEnum : uint8_t {
    X_AXIS = 0,
    Y_AXIS = 1,
    Z_AXIS = 2,
    E0_AXIS = 3,
    E_AXIS = E0_AXIS,
    A_AXIS = X_AXIS,
    B_AXIS = Y_AXIS,
    C_AXIS = Z_AXIS,
    ALL_AXES_ENUM = 0xFE,
    NO_AXIS_ENUM = 0xFF,
};

struct LogicalPosTag {};
struct NativePosTag {};
using MachinePosTag = NativePosTag;

template <typename T, typename Tag = NativePosTag>
using XYval = Vector<T, Tag, 2>;
template <typename T, typename Tag = NativePosTag>
using XYZval = Vector<T, Tag, 3>;
template <typename T, typename Tag = NativePosTag>
using XYZEval = Vector<T, Tag, 4>;

using xyze_int8_t = XYZEval<int8_t>;
using xyze_long_t = XYZEval<int32_t>;
using xyze_float_t = XYZEval<float>;
using xyze_double_t = XYZEval<double>;
using xyze_pos_t = xyze_float_t;
using MachinePosXYZE = XYZEval<float, MachinePosTag>;

#define LOOP_XY(VAR)   LOOP_S_LE_N(VAR, X_AXIS, Y_AXIS)
#define LOOP_XYZ(VAR)  LOOP_S_LE_N(VAR, X_AXIS, Z_AXIS)
#define LOOP_XYZE(VAR) LOOP_S_LE_N(VAR, X_AXIS, E_AXIS)
