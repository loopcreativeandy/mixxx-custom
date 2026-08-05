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
                    alignCueInfo(&cueInfo, *pSourceBeats, *pTargetOwnBeats);
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
        // so overwriting it with the original's would undo the correction.
    } else if (pSourceBeats) {
        mixxx::BeatsPointer pTargetBeats;
        if (sourceRate == targetRate) {
            pTargetBeats = pSourceBeats;
        } else {
            // Same tempo in wall clock time, different frames per second.
            const double frameRatio =
                    static_cast<double>(targetRate) / static_cast<double>(sourceRate);
            std::vector<mixxx::BeatMarker> markers;
            markers.reserve(pSourceBeats->getMarkers().size());
            for (const auto& marker : pSourceBeats->getMarkers()) {
                markers.emplace_back(
                        mixxx::audio::FramePos(
                                std::round(marker.position().value() * frameRatio)),
                        marker.beatsTillNextMarker());
            }
            pTargetBeats = mixxx::Beats::fromBeatMarkers(targetRate,
                    markers,
                    mixxx::audio::FramePos(
                            std::round(pSourceBeats->getLastMarkerPosition().value() *
                                    frameRatio)),
                    pSourceBeats->getLastMarkerBpm(),
                    pSourceBeats->getSubVersion());
        }
        result.beatsCopied = target.trySetBeats(pTargetBeats);
        result.bpmCopied = result.beatsCopied;
    } else {
        const double sourceBpm = source.getBpm();
        if (sourceBpm > 0) {
            result.bpmCopied = target.trySetBpm(sourceBpm);
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
