#pragma once
#define LOG_COMPONENT_DEF(name, severity) static_assert(true)
#define LOG_COMPONENT_REF(name)           static_assert(true)
#define log_warning(component, ...)       ((void)0)
#define log_info(component, ...)          ((void)0)
#define log_debug(component, ...)         ((void)0)
#define log_error(component, ...)         ((void)0)
