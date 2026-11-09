#pragma once
// Test stub of src/common/metric.h: records every custom value as formatted,
// with the firmware's 48 byte buffer (47 characters + NUL).
#include <cstdint>
#include <string>
#include <vector>

enum metric_value_type_t { METRIC_VALUE_CUSTOM = 0x04 };
enum { METRIC_DISABLED = 0,
    METRIC_ENABLED = 1 };

struct metric_t {
    const char *name;
};

#define METRIC_DEF(var, name, type, min_interval_ms, enabled) static metric_t var = { name }

struct RecordedMetric {
    std::string name;
    std::string value;
    bool too_long;
};
extern std::vector<RecordedMetric> recorded_metrics;

void metric_record_custom(metric_t *metric, const char *fmt, ...);
