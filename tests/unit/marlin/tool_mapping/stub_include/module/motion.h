#pragma once
#include <inc/MarlinConfig.h>
#include <core/types.h>
extern xyze_pos_t current_position;
extern xyz_pos_t hotend_currently_applied_offset;
template <size_t s>
[[nodiscard]] auto toLogical(const Vector<float, NativePosTag, s> &v) {
    return (v + hotend_currently_applied_offset).template to_tag<LogicalPosTag>();
}
template <size_t s>
[[nodiscard]] auto toNative(const Vector<float, LogicalPosTag, s> &v) {
    return v.template to_tag<NativePosTag>() - hotend_currently_applied_offset;
}
void do_blocking_move_to(const xyz_pos_t &raw, const feedRate_t &fr_mm_s = 0.0f);
void sync_e_position_to(float e);
