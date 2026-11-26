#pragma once
#include <inc/MarlinConfigPre.h>
#include <core/types.h>
#include <tool_index.hpp>

typedef struct {
    float z_raise;
} toolchange_settings_t;
extern toolchange_settings_t toolchange_settings;

enum class tool_return_t {
    no_return,
    to_current,
    to_destination,
    dock_backoff,
};

enum class tool_change_lift_t {
    no_lift,
    mbl_only_lift,
    full_lift,
    _last_item = full_lift,
};
