#pragma once

#include <QColor>

class QString;

namespace mixxx {

/// User-editable colors for stem tracks (andy-custom), loaded from
/// <settingsPath>/andys_stem_colors.ini so they can be changed without
/// recompiling. The file is created with commented defaults by initialize().
/// Edits are picked up while Mixxx runs: stem waveform/mixer colors on the
/// next track (re)load, the library row tint within ~2 seconds.
struct StemColorConfig {
    QColor drums;
    QColor bass;
    QColor other;
    QColor vocals;
    QColor rowTint; // carries its alpha

    /// Point the config at the resolved settings directory and create the
    /// template file there if it is missing. Must be called once at startup,
    /// after the settings path is final.
    ///
    /// Until this has run, current() returns the built-in defaults and touches
    /// no files at all. Same rationale as SpectrumConfig::initialize():
    /// current() is reached from StemInfoImporter in the test suite
    /// (stemtest.cpp), and the old resolve-and-create-on-demand behavior wrote
    /// andys_stem_colors.ini into the user's real settings directory from the
    /// test binary.
    static void initialize(const QString& settingsPath);

    /// Thread-safe cached snapshot; re-parses the file when its modification
    /// time changes, checked at most every ~2 s so paint-path callers stay
    /// cheap. Never creates or writes the file — initialize() owns creation.
    static StemColorConfig current();
};

} // namespace mixxx
