#pragma once

namespace mixxx {

/// User-editable tuning for the spectrum analyzer (andy-custom), loaded from
/// <settingsPath>/andys_spectrum.ini so the motion can be dialed in without
/// recompiling. The file is created with commented defaults on first use and
/// re-read while Mixxx runs (checked at most every ~2 s).
struct SpectrumConfig {
    /// Speed the bar starts falling with, in full scales per second. Keeps
    /// the fall from easing in (that reads as "exponential").
    double fallInitialSpeed;
    /// Acceleration added while falling, in full scales per second squared.
    /// 0 = perfectly linear fall.
    double fallGravity;
    /// Fraction of the upward gap closed per frame, 0.0-1.0. 1.0 = instant
    /// attack; lower values take the jitter out of the rise.
    double attack;
    /// How long a peak marker sits still before it starts to slide, in ms.
    double peakHoldMs;
    /// Peak marker fall speed, in full scales per second.
    double peakFallSpeed;
    /// Fade the topmost LED by its fractional level instead of snapping it
    /// on/off - makes the motion look continuous rather than stepped.
    bool smoothTopSegment;
    /// Repaint interval while bars are still moving, in ms (16 ~= 60 fps).
    int frameIntervalMs;

    /// Thread-safe cached snapshot; re-parses the file when its modification
    /// time changes, throttled so paint-path callers stay cheap.
    static SpectrumConfig current();
};

} // namespace mixxx
