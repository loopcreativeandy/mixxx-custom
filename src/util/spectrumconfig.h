#pragma once

class QString;

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
    /// Speed a peak marker starts sliding with, in full scales per second.
    double peakFallSpeed;
    /// Acceleration added while a peak marker slides, in full scales per
    /// second squared. 0 = the linear slide the markers had before CP13.
    double peakFallGravity;
    /// Fade the topmost LED by its fractional level instead of snapping it
    /// on/off - makes the motion look continuous rather than stepped.
    bool smoothTopSegment;
    /// Repaint interval while bars are still moving, in ms (16 ~= 60 fps).
    int frameIntervalMs;
    /// Number of frequency bars, spread logarithmically over 40 Hz - 16 kHz.
    /// Unlike the motion knobs this one is read once at startup (the engine
    /// allocates its filter bank and the skin builds the widget from it), so
    /// changing it needs a Mixxx restart.
    int bands;
    /// Re-read this file while Mixxx runs so edits to the values above apply
    /// within ~2 s. Set to 0 once the motion is dialed in: the file is then
    /// read once at startup and never stat()ed again, so the paint path stops
    /// touching the filesystem entirely.
    ///
    /// Overridden by the "Spectrum Live Reload" tick box in the skin settings
    /// whenever a skin that has it is loaded — see setHotReloadOverride().
    /// This key is the fallback for skins without the tick box.
    bool hotReload;

    /// Hard limits for `bands`, shared by the engine and the widget.
    static constexpr int kMinBands = 4;
    static constexpr int kMaxBands = 64;

    /// Point the config at the resolved settings directory and create the
    /// template file there if it is missing. Must be called once at startup,
    /// after the settings path is final.
    ///
    /// Until this has run, current() returns the built-in defaults and touches
    /// no files at all. That matters: current() is called from EngineSpectrum's
    /// constructor, and in the test binary the settings path resolves to the
    /// AppData root (QStandardPaths needs an application name), so the old
    /// read-and-create-on-demand behavior wrote andys_spectrum.ini into the
    /// user's AppData and crashed the Windows ARM64 test job. See
    /// mixxx-build/STATUS.md, 2026-08-07.
    static void initialize(const QString& settingsPath);

    /// Live override for `hotReload`, driven by the "Spectrum Live Reload"
    /// tick box in the skin settings ([Skin],spectrum_hot_reload). Takes
    /// precedence over the file's hot_reload key for the rest of the session
    /// once a skin has set it.
    ///
    /// WSpectrumMeter owns that control and pushes changes in here.
    /// SpectrumConfig deliberately holds no ControlProxy of its own: a proxy
    /// has to be created and destroyed on the same thread, and this is a
    /// process-wide singleton reached from both the engine and the GUI.
    static void setHotReloadOverride(bool enabled);

    /// Thread-safe cached snapshot; re-parses the file when its modification
    /// time changes, throttled so paint-path callers stay cheap. Never creates
    /// or writes the file — initialize() owns creation.
    static SpectrumConfig current();
};

} // namespace mixxx
