#pragma once
// Test stub: HAS_FORK_DIAGNOSTICS off, as on the MINI (ProjectOptions.cmake). The test target puts
// this directory before the generated option headers.

#define HAS_FORK_DIAGNOSTICS() 0

#ifdef __cplusplus
namespace option {
inline constexpr bool has_fork_diagnostics = false;
}
#endif
