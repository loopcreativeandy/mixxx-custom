#pragma once

#include <QHash>
#include <QString>

namespace mixxx {

/// User-editable layout overrides (andy-custom), loaded from
/// <settingsPath>/andys_layout.ini. Lets minimum widget sizes - the things
/// that stop a splitter handle from being dragged any further - be changed
/// without recompiling. Applied while the skin is built, so a skin reload
/// (or restart) is needed after editing, unlike andys_spectrum.ini.
struct LayoutConfig {
    /// Minimum width/height in px, keyed by skin <ObjectName>. A value of 0
    /// means "no minimum" (drag as far as you like, children may clip).
    QHash<QString, int> minWidth;
    QHash<QString, int> minHeight;

    /// Multiplier on the library-font height that forms the hard floor for
    /// the track table row height. 1.0 = upstream behavior (a row can never
    /// be shorter than the text); lower values allow tighter rows.
    double rowHeightMinFactor;

    /// Thread-safe cached snapshot; re-parses the file when its modification
    /// time changes, throttled to ~2 s.
    static LayoutConfig current();
};

} // namespace mixxx
