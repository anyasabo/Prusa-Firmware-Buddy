#pragma once
#define LOG_COMPONENT_DEF(name, level) \
    struct LogComponentDummy_##name {}
#define LOG_COMPONENT_REF(name)     struct LogComponentDummy_##name
#define log_debug(component, ...)   ((void)0)
#define log_info(component, ...)    ((void)0)
#define log_warning(component, ...) ((void)0)
#define log_error(component, ...)   ((void)0)
