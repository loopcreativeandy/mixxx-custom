#pragma once

#include <QString>

#include "track/track_decl.h"
#include "track/trackid.h"

class QSqlDatabase;
class Track;

/// Andy's stem workflow (andy-custom): every stem file in the library is
/// generated from an ordinary track file that is also in the library, so the
/// two share artist/title and file base name and differ only by the
/// ".stem.<ext>" suffix. Mixxx treats them as two unrelated tracks, which means
/// hot cues, loops, beat grid and BPM have to be maintained twice.
///
/// These helpers find the non-stem "original" of a stem track and copy that
/// track's DJ state onto the stem track.
namespace mixxx {
namespace stemoriginal {

/// Cheap suffix check, mirrors StemInfoImporter's preferred stem extensions
/// (and the row tint in BaseTrackTableModel).
bool isStemFileLocation(const QString& location);

/// File name of a stem track with the ".stem.<ext>" suffix removed, e.g.
/// "/music/stems/Artist - Title.stem.m4a" -> "Artist - Title".
/// Returns an empty string if the location is not a stem file.
QString stemBaseName(const QString& location);

/// Look up the library track that a stem track was generated from.
///
/// Matches on the file base name first (the reliable signal: the stem
/// extractor keeps the name), then falls back to an exact artist+title match.
/// Stem files are never returned. If several candidates match, one that
/// already has a beat grid wins.
///
/// Returns an invalid TrackId if nothing was found.
TrackId findOriginalTrackId(
        const QSqlDatabase& database,
        const Track& stemTrack);

/// True if the track carries cue data that a copy would destroy, i.e. hot
/// cues, saved loops or jumps. Analyzer-generated cues (main cue, intro/outro,
/// N60dBSound) do not count - they are recreated on the next analysis.
bool hasUserCueData(const Track& track);

struct ImportResult {
    int cuesCopied = 0;
    bool beatsCopied = false;
    bool bpmCopied = false;
    bool keyCopied = false;
};

/// Copy cue points, beat grid/BPM, key and the descriptive metadata from
/// `source` onto `target`, replacing whatever `target` had. Sample rate
/// differences between the two files are compensated for.
ImportResult importFromOriginal(Track& target, const Track& source);

} // namespace stemoriginal
} // namespace mixxx
