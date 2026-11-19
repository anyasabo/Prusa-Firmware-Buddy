#pragma once
// Host test stub: no CCM RAM on the host.
template <typename T>
inline bool can_be_used_by_dma(T *) { return true; }
