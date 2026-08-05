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

/// Which half of a stem/non-stem pair to look for.
enum class Counterpart {
    /// The ordinary track a stem file was generated from.
    Original,
    /// The stem file that was generated from an ordinary track.
    Stem,
};

/// Look up the other half of a stem/non-stem pair.
///
/// Matches on the file base name first (the reliable signal: the stem
/// extractor keeps the name), then falls back to an exact artist+title match.
/// The result is always of the requested kind, and never the track that was
/// passed in. If several candidates match, one that already has a beat grid
/// wins.
///
/// Returns an invalid TrackId if nothing was found, or if `track` is already
/// of the requested kind.
TrackId findCounterpartTrackId(
        const QSqlDatabase& database,
        const Track& track,
        Counterpart counterpart);

/// Look up the library track that a stem track was generated from.
/// Shorthand for `findCounterpartTrackId(..., Counterpart::Original)`.
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
