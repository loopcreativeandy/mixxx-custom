#pragma once

#include <QString>
#include <optional>

#include "track/beats.h"
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

/// Length of one beat in seconds around `timeSeconds`, or 0.0 if `beats` has
/// no usable beat there.
double beatPeriodSecondsAt(const Beats& beats, double timeSeconds);

/// True if `sourceBeats` and `targetBeats` describe the same tempo around
/// `timeSeconds`, allowing for the analyzer's usual whole-number confusion
/// (one grid at double, triple or quadruple the other's tempo).
bool tempoIsCompatible(
        const Beats& sourceBeats,
        const Beats& targetBeats,
        double timeSeconds);

/// How much later the beat around `timeSeconds` occurs in `targetBeats` than
/// in `sourceBeats`, in seconds. This is the time offset between the audio of
/// the two files: a stem file is re-encoded from its original and picks up a
/// codec delay of some tens of milliseconds on the way, which shifts every
/// musical event - and with it the analyzed beat grid.
///
/// The result is the difference between the closest beat of each grid and is
/// therefore never larger than half a beat: the assumption is that the two
/// files are the same recording and drifted only slightly apart, not that they
/// are offset by a whole beat or more.
///
/// Returns nullopt if either grid has no beat near that position, or if a
/// sample rate is missing.
std::optional<double> beatGridTimeOffsetSeconds(
        const Beats& sourceBeats,
        const Beats& targetBeats,
        double timeSeconds);

struct ImportResult {
    int cuesCopied = 0;
    bool beatsCopied = false;
    bool bpmCopied = false;
    bool keyCopied = false;
    /// True if the target's own beat grid was used to correct the imported cue
    /// positions for the codec delay of the stem file. The target's grid is
    /// then kept instead of being overwritten with the source's.
    bool alignedToTargetGrid = false;
    /// True if the target has no beat grid of its own (or one at an unrelated
    /// tempo), so the cue positions could not be corrected and the source's
    /// grid was copied verbatim.
    bool alignmentUnavailable = false;
    /// Median time correction that was applied to the imported cue positions.
    /// Only meaningful if `alignedToTargetGrid` is true.
    double alignmentShiftMillis = 0.0;
    /// The BPM the target now runs at after adopting the source's tempo, or
    /// 0.0 if no tempo was adopted. Only set when the target kept its own beat
    /// grid, i.e. together with `alignedToTargetGrid`.
    double bpmAdopted = 0.0;
};

/// Copy cue points, beat grid/BPM, key and the descriptive metadata from
/// `source` onto `target`, replacing whatever `target` had. Sample rate
/// differences between the two files are compensated for.
///
/// If `target` has a beat grid of its own at a compatible tempo, that grid is
/// kept and the imported cue positions are corrected by the offset between the
/// two grids, which cancels out the stem file's codec delay. The grid's tempo
/// is still set to the source's exact BPM - hand-corrected tempo has to reach
/// the stem track - while its phase stays where the stem file's beats are.
ImportResult importFromOriginal(Track& target, const Track& source);

} // namespace stemoriginal
} // namespace mixxx
