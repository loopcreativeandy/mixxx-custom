#pragma once

#include <QString>

#include "track/cue.h"
#include "util/color/rgbcolor.h"

/// andy-custom CP142: anti-loops (IDEA-22).
///
/// An anti-loop is a saved loop (CueType::Loop in a hotcue slot) whose label
/// starts with kMarker. While it is armed (hotcue pressed, runtime only, off
/// after load) playback skips from its start to its end. Storing it as a plain
/// loop with a label marker keeps the database untouched: official Mixxx just
/// shows a normal saved loop named "⏭ ...".
namespace andyantiloop {

inline const QString kMarker = QStringLiteral("⏭"); // ⏭
inline constexpr mixxx::RgbColor kColor = mixxx::RgbColor(0xC50A08); // Mixxx red

inline bool labelHasMarker(const QString& label) {
    return label.startsWith(kMarker);
}

inline bool isAntiLoop(const Cue& cue) {
    return cue.getType() == mixxx::CueType::Loop && labelHasMarker(cue.getLabel());
}

/// The label without the marker (what the user edits).
inline QString plainLabel(const QString& label) {
    if (!labelHasMarker(label)) {
        return label;
    }
    return label.mid(kMarker.size()).trimmed();
}

inline QString markedLabel(const QString& plain) {
    return plain.isEmpty() ? kMarker : kMarker + QChar(' ') + plain;
}

/// Turns a loop cue into an anti-loop (marker + red) or back (marker removed;
/// the color stays, the user can change it).
inline void setAntiLoop(Cue* pCue, bool enable) {
    const QString plain = plainLabel(pCue->getLabel());
    if (enable) {
        pCue->setLabel(markedLabel(plain));
        pCue->setColor(kColor);
    } else {
        pCue->setLabel(plain);
    }
}

} // namespace andyantiloop
