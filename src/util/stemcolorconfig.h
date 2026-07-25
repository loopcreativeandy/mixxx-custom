#pragma once

#include <QColor>

namespace mixxx {

/// User-editable colors for stem tracks (andy-custom), loaded from
/// <settingsPath>/andys_stem_colors.ini so they can be changed without
/// recompiling. The file is created with commented defaults on first use.
/// Edits are picked up while Mixxx runs: stem waveform/mixer colors on the
/// next track (re)load, the library row tint within ~2 seconds.
struct StemColorConfig {
    QColor drums;
    QColor bass;
    QColor other;
    QColor vocals;
    QColor rowTint; // carries its alpha

    /// Thread-safe cached snapshot; re-parses the file when its modification
    /// time changes, checked at most every ~2 s so paint-path callers stay
    /// cheap.
    static StemColorConfig current();
};

} // namespace mixxx
