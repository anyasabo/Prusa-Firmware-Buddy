#!/usr/bin/env python3
"""Error bounds of the Buddy pressure advance sampling, used by PaErrorBound (compare.cpp).

Buddy samples the PA position every h = smooth_time / 40, filters the samples with a 41-tap
Bartlett window (create_normalized_bartlett_window_filter()) and interpolates linearly between
the filter outputs. Klipper integrates with a continuous triangle window (pa_range_integrate()).
This script compares the two for a unit position jump and a unit speed jump (kink), over all
phases of the jump between two samples. Run: python3 pa_bounds.py (standard library only).
"""

N, MID = 41, 20
W = [(1 - abs(i - MID) / MID) / MID for i in range(N)]  # sum is 1
HST = float(MID)  # half smooth time in sample periods (h = 1)
PHASES, POINTS = 40, 20


def tri_cdf(t):  # integral of the triangle window (area 1) up to t
    if t <= -HST:
        return 0.0
    if t >= HST:
        return 1.0
    return (t + HST)**2 / (
        2 * HST * HST) if t < 0 else 1 - (HST - t)**2 / (2 * HST * HST)


def tri_ramp(t):  # integral of tri_cdf up to t: the smoothed ramp max(0, t)
    if t <= -HST:
        return 0.0
    if t >= HST:
        return t
    if t < 0:
        return (t + HST)**3 / (6 * HST * HST)
    return t + (HST - t)**3 / (6 * HST * HST)


def worst(f, exact):
    err = 0.0
    for ph in range(PHASES):
        t0 = ph / PHASES + 1e-9
        out = lambda c: sum(W[i] * f(c - MID + i - t0)
                            for i in range(N))  # centred on sample c
        for c in range(-MID - 2, MID + 3):
            a, b = out(c), out(c + 1)
            for k in range(POINTS):
                fr = k / POINTS
                err = max(err, abs(a + (b - a) * fr - exact(c + fr - t0)))
    return err


jump = worst(lambda t: 1.0 if t >= 0 else 0.0, tri_cdf)
kink = worst(lambda t: max(0.0, t), tri_ramp)
st = 2 * HST
print(f"position jump J: largest error {jump:.5f} J (bound J/40 = {1/40:.5f})")
print(
    f"speed jump dq:   largest error {kink / st:.6f} st*dq (bound st*dq/4800 = {1/4800:.6f})"
)
assert jump <= 1 / 40 + 1e-9 and kink / st <= 1 / 4800 + 1e-9
