#include "library/stemoriginal.h"

#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <algorithm>
#include <cmath>
#include <vector>

#include "track/beats.h"
#include "track/cue.h"
#include "track/track.h"
#include "util/logger.h"

namespace {

const mixxx::Logger kLogger("StemOriginal");

const QStringList kStemFileSuffixes = {
        QStringLiteral(".stem.mp4"),
        QStringLiteral(".stem.m4a")};

/// Sub-version stamped on every beat grid this import hands to a stem track.
///
/// It has to be non-empty. AnalyzerBeats::shouldAnalyze() treats a grid with an
/// *empty* sub version whose first beat sits at frame 0 as "this came from the
/// metadata BPM tag, don't trust it" and re-analyzes the track on every single
/// load. Most of Andy's originals carry exactly such a grid, so copying one
/// onto a stem track - which is what this file does when the two grids cannot
/// be aligned - used to hand the stem that same permanent re-analysis. A grid
/// that was deliberately imported is authoritative, so mark it as imported.
const QString kStemImportSubVersion = QStringLiteral("stem_original_import");

/// File name without its (last) extension, e.g. "Artist - Title.mp3" ->
/// "Artist - Title".
QString fileBaseName(const QString& fileName) {
    const int dotIndex = fileName.lastIndexOf(QChar('.'));
    if (dotIndex <= 0) {
        return fileName;
    }
    return fileName.left(dotIndex);
}

/// Fall back to a sample rate that makes the frame positions of both tracks
/// directly comparable when one of them is not known (yet).
void resolveSampleRates(
        mixxx::audio::SampleRate* pSourceRate,
        mixxx::audio::SampleRate* pTargetRate) {
    if (!pSourceRate->isValid()) {
        *pSourceRate = *pTargetRate;
    }
    if (!pTargetRate->isValid()) {
        *pTargetRate = *pSourceRate;
    }
    if (!pSourceRate->isValid()) {
        // Neither track knows its sample rate. Any valid rate will do, as long
        // as it is the same on both sides: the conversion is then a no-op and
        // frame positions are copied verbatim.
        *pSourceRate = mixxx::audio::SampleRate(44100);
        *pTargetRate = *pSourceRate;
    }
}

/// The analyzer only ever confuses a tempo with a small whole multiple of it
/// (usually double or half), so anything up to 4x is accepted as "the same
/// tempo" for the purpose of comparing two grids of the same recording.
constexpr int kMaxTempoMultiple = 4;
/// Relative tolerance when comparing the two beat lengths.
constexpr double kTempoTolerance = 0.03;

double medianOf(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    const size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    return values[middle];
}

} // anonymous namespace

namespace mixxx {
namespace stemoriginal {

bool isStemFileLocation(const QString& location) {
    for (const auto& suffix : kStemFileSuffixes) {
        if (location.endsWith(suffix, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

QString stemBaseName(const QString& location) {
    if (!isStemFileLocation(location)) {
        return QString();
    }
    const QString fileName = QFileInfo(location).fileName();
    for (const auto& suffix : kStemFileSuffixes) {
        if (fileName.endsWith(suffix, Qt::CaseInsensitive)) {
            return fileName.left(fileName.length() - suffix.length());
        }
    }
    return QString();
}

TrackId findCounterpartTrackId(
        const QSqlDatabase& database,
        const Track& track,
        Counterpart counterpart) {
    const QString ownLocation = track.getLocation();
    const bool ownIsStem = isStemFileLocation(ownLocation);
    const bool wantStem = counterpart == Counterpart::Stem;
    if (ownIsStem == wantStem) {
        // Already the kind that was asked for, so there is nothing to find.
        return {};
    }

    // The base name is what the two files have in common: the stem extractor
    // keeps the name and only appends ".stem.<ext>".
    const QString baseName = ownIsStem
            ? stemBaseName(ownLocation)
            : fileBaseName(QFileInfo(ownLocation).fileName());
    if (baseName.isEmpty()) {
        return {};
    }
    // Looking for the stem file means looking for "<base>.stem.<ext>".
    const QString namePattern = wantStem
            ? baseName + QStringLiteral(".stem.%")
            : baseName + QStringLiteral(".%");

    // Candidates are ordered so that a track with a beat grid wins over one
    // without: Andy keeps backup copies of some files, and the copy he
    // actually works with is the analyzed one.
    const QString orderBy = QStringLiteral(
            " ORDER BY (library.beats IS NOT NULL) DESC, library.id ASC");

    // Pass 1: same file base name. The stem extractor keeps the name, so this
    // is the reliable signal. '_' and '%' in the base name make LIKE match too
    // much, which is harmless because every row is checked again below.
    {
        QSqlQuery query(database);
        query.prepare(QStringLiteral(
                              "SELECT library.id, track_locations.location "
                              "FROM library "
                              "INNER JOIN track_locations "
                              "ON library.location = track_locations.id "
                              "WHERE library.mixxx_deleted = 0 "
                              "AND track_locations.fs_deleted = 0 "
                              "AND track_locations.filename LIKE :namePattern") +
                orderBy);
        query.bindValue(":namePattern", namePattern);
        if (!query.exec()) {
            kLogger.warning() << "Failed to look up the counterpart of"
                              << ownLocation << query.lastError();
        } else {
            while (query.next()) {
                const QString location = query.value(1).toString();
                if (location == ownLocation ||
                        isStemFileLocation(location) != wantStem) {
                    continue;
                }
                const QString candidateBaseName = wantStem
                        ? stemBaseName(location)
                        : fileBaseName(QFileInfo(location).fileName());
                if (candidateBaseName.compare(baseName, Qt::CaseInsensitive) != 0) {
                    continue;
                }
                return TrackId(query.value(0));
            }
        }
    }

    // Pass 2: exact artist + title. Catches a file that was renamed after the
    // stems had been extracted.
    const QString artist = track.getArtist();
    const QString title = track.getTitle();
    if (artist.isEmpty() && title.isEmpty()) {
        return {};
    }
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
                          "SELECT library.id, track_locations.location "
                          "FROM library "
                          "INNER JOIN track_locations "
                          "ON library.location = track_locations.id "
                          "WHERE library.mixxx_deleted = 0 "
                          "AND track_locations.fs_deleted = 0 "
                          "AND library.artist = :artist COLLATE NOCASE "
                          "AND library.title = :title COLLATE NOCASE") +
            orderBy);
    query.bindValue(":artist", artist);
    query.bindValue(":title", title);
    if (!query.exec()) {
        kLogger.warning() << "Failed to look up the counterpart of"
                          << ownLocation << query.lastError();
        return {};
    }
    while (query.next()) {
        const QString location = query.value(1).toString();
        if (location == ownLocation || isStemFileLocation(location) != wantStem) {
            continue;
        }
        return TrackId(query.value(0));
    }
    return {};
}

TrackId findOriginalTrackId(
        const QSqlDatabase& database,
        const Track& stemTrack) {
    return findCounterpartTrackId(database, stemTrack, Counterpart::Original);
}

bool hasUserCueData(const Track& track) {
    const QList<CuePointer> cuePoints = track.getCuePoints();
    for (const auto& pCue : cuePoints) {
        if (!pCue) {
            continue;
        }
        switch (pCue->getType()) {
        case mixxx::CueType::HotCue:
        case mixxx::CueType::Loop:
        case mixxx::CueType::Jump:
            return true;
        default:
            break;
        }
    }
    return false;
}

bool isImplausibleAlignmentShift(double shiftMillis) {
    return shiftMillis < kMinPlausibleShiftMillis ||
            shiftMillis > kMaxPlausibleShiftMillis;
}

double beatPeriodSecondsAt(const Beats& beats, double timeSeconds) {
    const auto sampleRate = beats.getSampleRate();
    if (!sampleRate.isValid()) {
        return 0.0;
    }
    const double framesPerSecond = static_cast<double>(sampleRate);
    const auto beatPos = beats.findClosestBeat(
            audio::FramePos(timeSeconds * framesPerSecond));
    if (!beatPos.isValid()) {
        return 0.0;
    }
    // +1 frame so that a position that already is a beat does not return
    // itself.
    const auto nextBeatPos = beats.findNextBeat(audio::FramePos(beatPos.value() + 1));
    if (!nextBeatPos.isValid()) {
        return 0.0;
    }
    return (nextBeatPos.value() - beatPos.value()) / framesPerSecond;
}

bool tempoIsCompatible(
        const Beats& sourceBeats,
        const Beats& targetBeats,
        double timeSeconds) {
    const double sourcePeriod = beatPeriodSecondsAt(sourceBeats, timeSeconds);
    const double targetPeriod = beatPeriodSecondsAt(targetBeats, timeSeconds);
    if (sourcePeriod <= 0.0 || targetPeriod <= 0.0) {
        return false;
    }
    double ratio = sourcePeriod / targetPeriod;
    if (ratio < 1.0) {
        ratio = 1.0 / ratio;
    }
    const double multiple = std::round(ratio);
    if (multiple < 1.0 || multiple > kMaxTempoMultiple) {
        return false;
    }
    return std::abs(ratio - multiple) <= kTempoTolerance * multiple;
}

std::optional<double> beatGridTimeOffsetSeconds(
        const Beats& sourceBeats,
        const Beats& targetBeats,
        double timeSeconds) {
    const auto sourceRate = sourceBeats.getSampleRate();
    const auto targetRate = targetBeats.getSampleRate();
    if (!sourceRate.isValid() || !targetRate.isValid()) {
        return std::nullopt;
    }
    const double sourceFramesPerSecond = static_cast<double>(sourceRate);
    const double targetFramesPerSecond = static_cast<double>(targetRate);

    const auto sourceBeatPos = sourceBeats.findClosestBeat(
            audio::FramePos(timeSeconds * sourceFramesPerSecond));
    if (!sourceBeatPos.isValid()) {
        return std::nullopt;
    }
    const double sourceBeatSeconds = sourceBeatPos.value() / sourceFramesPerSecond;

    const auto targetBeatPos = targetBeats.findClosestBeat(
            audio::FramePos(sourceBeatSeconds * targetFramesPerSecond));
    if (!targetBeatPos.isValid()) {
        return std::nullopt;
    }
    const double targetBeatSeconds = targetBeatPos.value() / targetFramesPerSecond;

    return targetBeatSeconds - sourceBeatSeconds;
}

namespace {

/// Moves a cue by the offset between the two beat grids at its own position,
/// so that a cue that sat on a beat of the original sits on the matching beat
/// of the stem file. Cues that were deliberately placed off the grid keep
/// their distance to it.
///
/// Returns the applied correction in milliseconds, or nullopt if the grids
/// have nothing to say about that position.
std::optional<double> alignCueInfo(
        CueInfo* pCueInfo,
        const Beats& sourceBeats,
        const Beats& targetBeats) {
    const std::optional<double> startMillis = pCueInfo->getStartPositionMillis();
    const std::optional<double> endMillis = pCueInfo->getEndPositionMillis();
    // A saved loop without a start position is anchored by its end.
    const std::optional<double> anchorMillis = startMillis ? startMillis : endMillis;
    if (!anchorMillis) {
        return std::nullopt;
    }
    const std::optional<double> offsetSeconds =
            beatGridTimeOffsetSeconds(sourceBeats, targetBeats, *anchorMillis / 1000.0);
    if (!offsetSeconds) {
        return std::nullopt;
    }
    const double offsetMillis = *offsetSeconds * 1000.0;
    if (startMillis) {
        pCueInfo->setStartPositionMillis(*startMillis + offsetMillis);
    }
    if (endMillis) {
        // The loop length is a musical quantity and does not change, so both
        // ends move by the same amount.
        pCueInfo->setEndPositionMillis(*endMillis + offsetMillis);
    }
    return offsetMillis;
}

/// Builds a beat grid for the stem track that runs at the original track's
/// tempo but keeps the stem file's own beat positions.
///
/// Andy corrects the BPM of his originals by hand, and that correction has to
/// reach the stem track - but the stem's grid is what the imported cue
/// positions were just aligned to, so it cannot simply be replaced by the
/// original's. Instead the grid is re-laid at the original's exact BPM around
/// one beat of the stem's own grid, which is what editing the BPM in the
/// library does: the tempo number changes, the phase does not.
///
/// The anchor beat is picked through the grid offset rather than just taking
/// the stem's first beat, because the analyzer regularly reads a stem file at
/// double tempo. Half of the beats in such a grid sit between the original's
/// beats, and anchoring on one of those would put the whole re-tempoed grid on
/// the off-beat. The beat closest to a beat of the original is by construction
/// not one of them.
///
/// Returns nullopt if the grids have nothing to say about the start of the
/// track, or if the new grid could not be built.
BeatsPointer retempoedTargetBeats(
        const Beats& sourceBeats,
        const Beats& targetBeats,
        Bpm sourceBpm,
        audio::SampleRate targetRate) {
    const auto sourceRate = sourceBeats.getSampleRate();
    // The grid that is about to be replaced knows the rate it was built for.
    if (targetBeats.getSampleRate().isValid()) {
        targetRate = targetBeats.getSampleRate();
    }
    if (!sourceBpm.isValid() || !sourceRate.isValid() || !targetRate.isValid()) {
        return nullptr;
    }
    const auto sourceFirstBeat = sourceBeats.firstBeat();
    if (!sourceFirstBeat.isValid()) {
        return nullptr;
    }
    const double sourceFirstBeatSeconds =
            sourceFirstBeat.value() / static_cast<double>(sourceRate);
    const std::optional<double> offsetSeconds = beatGridTimeOffsetSeconds(
            sourceBeats, targetBeats, sourceFirstBeatSeconds);
    if (!offsetSeconds) {
        return nullptr;
    }

    const double targetFramesPerSecond = static_cast<double>(targetRate);
    double anchorFrames =
            (sourceFirstBeatSeconds + *offsetSeconds) * targetFramesPerSecond;
    // A negative anchor is a valid grid in Mixxx, but it is easier to reason
    // about one inside the track: the grid is constant, so any beat describes
    // it.
    const double beatLengthFrames = 60.0 * targetFramesPerSecond / sourceBpm.value();
    if (beatLengthFrames > 0.0) {
        while (anchorFrames < 0.0) {
            anchorFrames += beatLengthFrames;
        }
    } else if (anchorFrames < 0.0) {
        return nullptr;
    }

    return Beats::fromConstTempo(targetRate,
            audio::FramePos(std::round(anchorFrames)),
            sourceBpm,
            kStemImportSubVersion);
}

} // anonymous namespace

ImportResult importFromOriginal(Track& target, const Track& source) {
    ImportResult result;

    auto sourceRate = source.getSampleRate();
    auto targetRate = target.getSampleRate();
    resolveSampleRates(&sourceRate, &targetRate);

    const mixxx::BeatsPointer pSourceBeats = source.getBeats();
    const mixxx::BeatsPointer pTargetOwnBeats = target.getBeats();

    // A stem file is decoded from its original and comes back with a codec
    // delay in front of it - measured at 95-99 ms on Andy's library - so every
    // cue position of the original is that much too early for the stem file.
    // The delay is not knowable from the metadata, but it moves the analyzed
    // beat grid by exactly the same amount, so the stem's own grid is the
    // measuring stick: whatever it is offset by against the original's grid is
    // what the cues have to move by. That only works if the stem has been
    // analyzed and both grids agree on the tempo.
    const double anchorSeconds = std::max(0.0, source.getDuration() / 2.0);
    const bool alignToTargetGrid = pSourceBeats && pTargetOwnBeats &&
            tempoIsCompatible(*pSourceBeats, *pTargetOwnBeats, anchorSeconds);
    result.alignedToTargetGrid = alignToTargetGrid;
    result.alignmentUnavailable = !alignToTargetGrid;

    // The tempo the stem track ends up with is decided before the cues are
    // placed, because that grid is what they are placed against: Andy corrects
    // the BPM of his originals by hand, and a cue aligned to the stem's own
    // slightly different reading would sit a little beside the corrected grid -
    // by more and more of a millisecond the later in the track it is. With the
    // grid re-tempoed first, the correction collapses to the constant codec
    // delay it physically is.
    const BeatsPointer pRetempoedTargetBeats = alignToTargetGrid
            ? retempoedTargetBeats(*pSourceBeats,
                      *pTargetOwnBeats,
                      Bpm(source.getBpm()),
                      targetRate)
            : nullptr;
    const Beats* pAlignmentGrid = pRetempoedTargetBeats
            ? pRetempoedTargetBeats.get()
            : pTargetOwnBeats.get();

    // Cue points. The CueInfo round trip is time based, so it converts the
    // frame positions if the two files were encoded at different rates.
    const QList<CuePointer> sourceCues = source.getCuePoints();
    QList<CuePointer> targetCues;
    targetCues.reserve(sourceCues.size());
    std::vector<double> shiftsMillis;
    for (const auto& pCue : sourceCues) {
        if (!pCue) {
            continue;
        }
        mixxx::CueInfo cueInfo = pCue->getCueInfo(sourceRate);
        if (alignToTargetGrid) {
            const std::optional<double> shiftMillis =
                    alignCueInfo(&cueInfo, *pSourceBeats, *pAlignmentGrid);
            if (shiftMillis) {
                shiftsMillis.push_back(*shiftMillis);
            }
        }
        targetCues.append(CuePointer(new Cue(cueInfo, targetRate, true)));
    }
    target.setCuePoints(targetCues);
    result.cuesCopied = targetCues.size();
    result.alignmentShiftMillis = medianOf(std::move(shiftsMillis));

    // Beat grid. A locked BPM on the stem track would reject the new grid, so
    // unlock first and adopt the original's lock state afterwards.
    const bool sourceBpmLocked = source.isBpmLocked();
    target.setBpmLocked(false);
    if (alignToTargetGrid) {
        // The stem's own grid is what the cue positions were just aligned to,
        // so overwriting it with the original's would undo the correction. Only
        // the tempo number is taken over, on a grid that keeps the stem file's
        // own beat positions.
        if (pRetempoedTargetBeats) {
            const Bpm sourceBpm = Bpm(source.getBpm());
            if (Bpm(target.getBpm()) == sourceBpm) {
                // Nothing to change, but the tempi do match.
                result.bpmCopied = true;
            } else {
                result.bpmCopied = target.trySetBeats(pRetempoedTargetBeats);
            }
            if (result.bpmCopied) {
                result.bpmAdopted = sourceBpm.value();
            }
        }
    } else if (pSourceBeats) {
        // Same tempo in wall clock time, possibly a different frames per
        // second. The grid is rebuilt even when the rates match, because it
        // has to be re-stamped with kStemImportSubVersion - the original's own
        // sub version is typically empty, which would make the analyzer
        // re-analyze the stem track on every load.
        const double frameRatio = sourceRate == targetRate
                ? 1.0
                : static_cast<double>(targetRate) / static_cast<double>(sourceRate);
        std::vector<mixxx::BeatMarker> markers;
        markers.reserve(pSourceBeats->getMarkers().size());
        for (const auto& marker : pSourceBeats->getMarkers()) {
            markers.emplace_back(
                    mixxx::audio::FramePos(
                            std::round(marker.position().value() * frameRatio)),
                    marker.beatsTillNextMarker());
        }
        mixxx::BeatsPointer pTargetBeats = mixxx::Beats::fromBeatMarkers(targetRate,
                markers,
                mixxx::audio::FramePos(
                        std::round(pSourceBeats->getLastMarkerPosition().value() *
                                frameRatio)),
                pSourceBeats->getLastMarkerBpm(),
                kStemImportSubVersion);
        result.beatsCopied = target.trySetBeats(pTargetBeats);
        result.bpmCopied = result.beatsCopied;
    } else {
        // The original has no grid at all, only a tempo number. Track::trySetBpm
        // would build the grid with an *empty* sub version, and a constant grid
        // always has a beat within the first beat length of the start, so it
        // would regularly land on frame 0 - the shape the analyzer re-analyzes
        // on every load. Build the same grid it would, but stamped.
        const double sourceBpm = source.getBpm();
        if (sourceBpm > 0 && targetRate.isValid()) {
            auto anchor = target.getMainCuePosition();
            if (!anchor.isValid()) {
                anchor = mixxx::audio::kStartFramePos;
            }
            result.bpmCopied = target.trySetBeats(
                    mixxx::Beats::fromConstTempo(targetRate,
                            anchor,
                            mixxx::Bpm(sourceBpm),
                            kStemImportSubVersion));
        }
    }
    target.setBpmLocked(sourceBpmLocked);

    // Musical key
    const Keys sourceKeys = source.getKeys();
    if (sourceKeys.getGlobalKey() != mixxx::track::io::key::INVALID) {
        target.setKeys(sourceKeys);
        result.keyCopied = true;
    }

    // Descriptive metadata. The stem file is generated from the original, so
    // the original's tags are the ones Andy curates (his DJ tag codes live in
    // grouping and comment).
    target.setTitle(source.getTitle());
    target.setArtist(source.getArtist());
    target.setAlbum(source.getAlbum());
    target.setAlbumArtist(source.getAlbumArtist());
    target.updateGenre(source.getGenre());
    target.setComposer(source.getComposer());
    target.setGrouping(source.getGrouping());
    target.setComment(source.getComment());
    target.setYear(source.getYear());
    target.setTrackNumber(source.getTrackNumber());
    target.setTrackTotal(source.getTrackTotal());
    target.setColor(source.getColor());
    target.setRating(source.getRating());

    return result;
}

} // namespace stemoriginal
} // namespace mixxx
