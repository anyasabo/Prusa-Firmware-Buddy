#pragma once
// The metrics are not recorded.
struct metric_t {};
#define METRIC_DEF(var, name, type, interval, enabled) static metric_t var
inline void metric_record_custom(metric_t *, const char *, ...) {}
inline void metric_record_custom_at_time(metric_t *, uint32_t, const char *, ...) {}
