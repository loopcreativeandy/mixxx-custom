#include "library/similarity/similarityindex.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "library/dao/directorydao.h"
#include "library/queryutil.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "util/logger.h"

namespace {

const mixxx::Logger kLogger("SimilarityIndex");

const QString kIndexFileName = QStringLiteral("similarity.sqlite");

// [Similarity] settings. There is no preferences page yet, so these are read from
// mixxx.cfg; every one of them has a working default.
const char* kConfigGroup = "[Similarity]";
const char* kConfigIndexPath = "index_path";
const char* kConfigResultCount = "result_count";
const char* kConfigMinScore = "min_score";
const char* kConfigMinResults = "min_results";
const char* kConfigMinDuration = "min_duration_seconds";
const char* kConfigMaxDuration = "max_duration_seconds";
const char* kConfigExcludedDirs = "excluded_directories";

constexpr int kDefaultResultCount = 50;
// Andy, 2026-09-23: the Similar view shows every track at or above this
// similarity, however many that is. Measured on his index (3754 tracks):
// median 86 hits per seed, 6 % of seeds get none, the busiest 643.
constexpr double kDefaultMinScore = 0.9;
// ...but never fewer than this many, so the view is not empty for a track
// with no close neighbours (Andy, same day).
constexpr int kDefaultMinResults = 10;
// A duration window is the only filter available without a dedicated tag, and it can
// only honestly catch the extremes: silence clips and spoken excerpts at the bottom,
// hour-long recordings at the top. Measured against a real library, anything narrower
// starts dropping short edits and long remixes that are perfectly good tracks.
constexpr double kDefaultMinDurationSeconds = 45.0;
constexpr double kDefaultMaxDurationSeconds = 900.0;
const QString kDefaultExcludedDirs = QStringLiteral("sets");

/// Re-reading the library for every right-click would be wasteful, keeping the mapping
/// forever would go stale as tracks are added. One second is imperceptible either way.
constexpr qint64 kMappingMaxAgeMs = 1000;

/// Longest trailing path segment sequence tried when matching, as a safety net in case
/// the index ever holds a very deep relative path.
constexpr int kMaxPathDepth = 8;

/// Two files are considered the same length within this tolerance. Decoders disagree
/// slightly on MP3 duration, so an exact comparison would be useless.
constexpr double kDurationToleranceSeconds = 2.0;

QString normalizePath(const QString& path) {
    // Replace separators explicitly rather than with QDir::fromNativeSeparators(), which
    // is a no-op on anything but Windows: the index is built on one platform and read on
    // another, so both spellings have to work everywhere.
    QString normalized = path;
    normalized.replace(QChar('\\'), QChar('/'));
    return normalized.toLower();
}

QStringList pathSegments(const QString& normalizedPath) {
    return normalizedPath.split(QChar('/'), Qt::SkipEmptyParts);
}

} // anonymous namespace

SimilarityIndex::SimilarityIndex(UserSettingsPointer pConfig)
        : m_pConfig(std::move(pConfig)),
          m_pTrackCollectionManager(nullptr),
          m_loadAttempted(false),
          m_loaded(false),
          m_useTestLibrary(false),
          m_dimension(0),
          m_maxPathDepth(1),
          m_mappingValid(false),
          m_libraryTrackCount(0) {
}

SimilarityIndex::~SimilarityIndex() = default;

int SimilarityIndex::resultCount() const {
    const int count = m_pConfig
            ? m_pConfig->getValue(ConfigKey(kConfigGroup, kConfigResultCount),
                      kDefaultResultCount)
            : kDefaultResultCount;
    return std::clamp(count, 1, 1000);
}

double SimilarityIndex::minScore() const {
    const double minScore = m_pConfig
            ? m_pConfig->getValue(ConfigKey(kConfigGroup, kConfigMinScore),
                      kDefaultMinScore)
            : kDefaultMinScore;
    // Explicit range test, not std::clamp: under -ffast-math clamp lets a NaN
    // through.
    if (minScore >= -1.0 && minScore <= 1.0) {
        return minScore;
    }
    return kDefaultMinScore;
}

int SimilarityIndex::minResults() const {
    const int minResults = m_pConfig
            ? m_pConfig->getValue(ConfigKey(kConfigGroup, kConfigMinResults),
                      kDefaultMinResults)
            : kDefaultMinResults;
    return std::clamp(minResults, 0, 1000);
}

void SimilarityIndex::invalidate() {
    m_loadAttempted = false;
    m_loaded = false;
    m_mappingValid = false;
}

// ---------------------------------------------------------------------- loading

QString SimilarityIndex::findIndexFile() const {
    if (m_pConfig) {
        const QString configured =
                m_pConfig->getValue(ConfigKey(kConfigGroup, kConfigIndexPath));
        if (!configured.isEmpty()) {
            return configured;
        }
    }
    // Otherwise: next to the music, in whichever library directory holds it.
    VERIFY_OR_DEBUG_ASSERT(m_pTrackCollectionManager) {
        return QString();
    }
    const QList<mixxx::FileInfo> dirs = m_pTrackCollectionManager->internalCollection()
                                                ->getDirectoryDAO()
                                                .loadAllDirectories();
    for (const auto& dirInfo : dirs) {
        const QString candidate = QDir(dirInfo.location()).filePath(kIndexFileName);
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    if (dirs.isEmpty()) {
        return QString();
    }
    // Nothing found: name the first directory so the message can say where to put it.
    return QDir(dirs.first().location()).filePath(kIndexFileName);
}

bool SimilarityIndex::loadVectors(const QString& filePath) {
    m_vectors.clear();
    m_relPaths.clear();
    m_indexDurations.clear();
    m_rowByRelPath.clear();
    m_rowsByBasename.clear();
    m_dimension = 0;
    m_maxPathDepth = 1;
    m_filePath = filePath;
    m_error.clear();
    m_builtAt.clear();
    m_sourceRoot.clear();

    if (filePath.isEmpty()) {
        m_error = QObject::tr("No music library directory is configured.");
        return false;
    }
    if (!QFileInfo::exists(filePath)) {
        m_error = QObject::tr("No similarity index found.");
        return false;
    }

    // Its own connection, read-only: the shared Mixxx database is never touched.
    const QString connectionName =
            QStringLiteral("similarity-") + QUuid::createUuid().toString();
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(filePath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!db.open()) {
            m_error = QObject::tr("The similarity index could not be opened.");
            kLogger.warning() << "Failed to open index:" << db.lastError().text();
        } else {
            QSqlQuery metaQuery(db);
            if (metaQuery.exec(QStringLiteral("SELECT key, value FROM meta"))) {
                while (metaQuery.next()) {
                    const QString key = metaQuery.value(0).toString();
                    const QString value = metaQuery.value(1).toString();
                    if (key == QLatin1String("dim")) {
                        m_dimension = value.toInt();
                    } else if (key == QLatin1String("built_at")) {
                        m_builtAt = value;
                    } else if (key == QLatin1String("library_root")) {
                        m_sourceRoot = value;
                    }
                }
            }
            if (m_dimension <= 0) {
                m_error = QObject::tr("The similarity index is unreadable.");
                kLogger.warning() << "Index has no usable vector size";
            } else {
                QSqlQuery query(db);
                query.setForwardOnly(true);
                if (!query.exec(QStringLiteral(
                            "SELECT rel_path, duration, vec FROM vectors"))) {
                    m_error = QObject::tr("The similarity index is unreadable.");
                    LOG_FAILED_QUERY(query);
                } else {
                    const int expectedBytes = m_dimension * static_cast<int>(sizeof(float));
                    int skipped = 0;
                    while (query.next()) {
                        const QByteArray blob = query.value(2).toByteArray();
                        if (blob.size() != expectedBytes) {
                            ++skipped;
                            continue;
                        }
                        const QString relPath = normalizePath(query.value(0).toString());
                        const int row = m_relPaths.size();
                        const float* pFloats =
                                reinterpret_cast<const float*>(blob.constData());
                        m_vectors.insert(m_vectors.end(), pFloats, pFloats + m_dimension);
                        m_relPaths.append(relPath);
                        m_indexDurations.push_back(
                                query.value(1).isNull() ? 0.0 : query.value(1).toDouble());
                        m_rowByRelPath.insert(relPath, row);
                        const QStringList segments = pathSegments(relPath);
                        if (!segments.isEmpty()) {
                            m_rowsByBasename.insert(segments.last(), row);
                            m_maxPathDepth = std::max(m_maxPathDepth, static_cast<int>(segments.size()));
                        }
                    }
                    if (skipped > 0) {
                        kLogger.warning()
                                << "Skipped" << skipped << "vectors of unexpected size";
                    }
                    ok = !m_relPaths.isEmpty();
                    if (!ok) {
                        m_error = QObject::tr("The similarity index is empty.");
                    }
                }
            }
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    m_maxPathDepth = std::min(m_maxPathDepth, kMaxPathDepth);
    return ok;
}

bool SimilarityIndex::ensureLoaded() {
    if (m_loadAttempted) {
        return m_loaded;
    }
    m_loadAttempted = true;
    m_loaded = loadVectors(findIndexFile());
    m_mappingValid = false;
    if (m_loaded) {
        kLogger.info() << "Loaded" << m_relPaths.size() << "vectors of size" << m_dimension;
    }
    return m_loaded;
}

bool SimilarityIndex::openForTesting(const QString& filePath) {
    m_loadAttempted = true;
    m_loaded = loadVectors(filePath);
    m_mappingValid = false;
    return m_loaded;
}

void SimilarityIndex::setLibraryTracksForTesting(const QList<LibraryTrack>& tracks) {
    m_useTestLibrary = true;
    m_testLibraryTracks = tracks;
    m_mappingValid = false;
}

// --------------------------------------------------------------------- matching

QList<SimilarityIndex::LibraryTrack> SimilarityIndex::readLibraryTracks() const {
    QList<LibraryTrack> tracks;
    VERIFY_OR_DEBUG_ASSERT(m_pTrackCollectionManager) {
        return tracks;
    }
    QSqlQuery query(m_pTrackCollectionManager->internalCollection()->database());
    query.setForwardOnly(true);
    query.prepare(QStringLiteral(
            "SELECT library.id, track_locations.location, library.duration "
            "FROM library INNER JOIN track_locations "
            "ON library.location = track_locations.id "
            "WHERE library.mixxx_deleted = 0 AND track_locations.fs_deleted = 0"));
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return tracks;
    }
    while (query.next()) {
        LibraryTrack track;
        track.trackId = TrackId(query.value(0));
        track.location = query.value(1).toString();
        track.durationSeconds = query.value(2).toDouble();
        if (track.trackId.isValid() && !track.location.isEmpty()) {
            tracks.append(track);
        }
    }
    return tracks;
}

int SimilarityIndex::rowForLocation(const QString& location) const {
    const QStringList segments = pathSegments(normalizePath(location));
    // Longest suffix first: the more path segments agree, the less room there is for a
    // same-named file somewhere else in the library to win.
    const int maxDepth = std::min(m_maxPathDepth, static_cast<int>(segments.size()));
    for (int depth = maxDepth; depth >= 1; --depth) {
        const QString candidate =
                segments.mid(segments.size() - depth).join(QChar('/'));
        const auto it = m_rowByRelPath.constFind(candidate);
        if (it != m_rowByRelPath.constEnd()) {
            return it.value();
        }
    }
    return -1;
}

int SimilarityIndex::rowForBasename(const QString& location, double durationSeconds) const {
    const QStringList segments = pathSegments(normalizePath(location));
    if (segments.isEmpty()) {
        return -1;
    }
    const QList<int> rows = m_rowsByBasename.values(segments.last());
    if (rows.size() == 1) {
        return rows.first();
    }
    if (rows.isEmpty() || durationSeconds <= 0.0) {
        return -1;
    }
    // Several files share this name. Duration is the only thing left to tell them
    // apart, and it has to be unambiguous or the match is a guess.
    int match = -1;
    for (int row : rows) {
        const double indexDuration = m_indexDurations[static_cast<size_t>(row)];
        if (indexDuration <= 0.0) {
            continue;
        }
        if (std::abs(indexDuration - durationSeconds) <= kDurationToleranceSeconds) {
            if (match >= 0) {
                return -1;
            }
            match = row;
        }
    }
    return match;
}

void SimilarityIndex::matchLibrary(const QList<LibraryTrack>& tracks) {
    m_rowByTrackId.clear();
    m_durationByTrackId.clear();
    m_excludedByTrackId.clear();
    m_trackIdsByRow.clear();
    m_trackIdsByRow.resize(m_relPaths.size());
    m_libraryTrackCount = tracks.size();

    QStringList excludedDirs;
    const QString configured = m_pConfig
            ? m_pConfig->getValue(
                      ConfigKey(kConfigGroup, kConfigExcludedDirs), kDefaultExcludedDirs)
            : kDefaultExcludedDirs;
    const QStringList configuredDirs = configured.split(QChar(','), Qt::SkipEmptyParts);
    for (const QString& dir : configuredDirs) {
        const QString trimmed = dir.trimmed().toLower();
        if (!trimmed.isEmpty()) {
            excludedDirs.append(trimmed);
        }
    }

    QList<const LibraryTrack*> unmatched;
    for (const LibraryTrack& track : tracks) {
        m_durationByTrackId.insert(track.trackId, track.durationSeconds);
        const QStringList segments = pathSegments(normalizePath(track.location));
        bool excluded = false;
        // The last segment is the file name, so stop before it.
        for (int i = 0; i + 1 < segments.size(); ++i) {
            if (excludedDirs.contains(segments.at(i))) {
                excluded = true;
                break;
            }
        }
        m_excludedByTrackId.insert(track.trackId, excluded);

        const int row = rowForLocation(track.location);
        if (row >= 0) {
            m_rowByTrackId.insert(track.trackId, row);
            m_trackIdsByRow[row].append(track.trackId);
        } else {
            unmatched.append(&track);
        }
    }
    // The file-name fallback runs second so a full path match always wins.
    for (const LibraryTrack* pTrack : unmatched) {
        const int row = rowForBasename(pTrack->location, pTrack->durationSeconds);
        if (row >= 0) {
            m_rowByTrackId.insert(pTrack->trackId, row);
            m_trackIdsByRow[row].append(pTrack->trackId);
        }
    }

    m_mappingValid = true;
    m_mappingAge.start();
}

void SimilarityIndex::ensureLibraryMapping() {
    if (m_mappingValid && m_mappingAge.isValid() &&
            m_mappingAge.elapsed() < kMappingMaxAgeMs) {
        return;
    }
    matchLibrary(m_useTestLibrary ? m_testLibraryTracks : readLibraryTracks());
}

// ---------------------------------------------------------------------- queries

bool SimilarityIndex::isEligibleResult(TrackId trackId) const {
    if (m_excludedByTrackId.value(trackId, false)) {
        return false;
    }
    const double duration = m_durationByTrackId.value(trackId, 0.0);
    if (duration <= 0.0) {
        // Unanalyzed or unknown length: no reason to hide it.
        return true;
    }
    const double minDuration = m_pConfig
            ? m_pConfig->getValue(ConfigKey(kConfigGroup, kConfigMinDuration),
                      kDefaultMinDurationSeconds)
            : kDefaultMinDurationSeconds;
    const double maxDuration = m_pConfig
            ? m_pConfig->getValue(ConfigKey(kConfigGroup, kConfigMaxDuration),
                      kDefaultMaxDurationSeconds)
            : kDefaultMaxDurationSeconds;
    if (minDuration > 0.0 && duration < minDuration) {
        return false;
    }
    if (maxDuration > 0.0 && duration > maxDuration) {
        return false;
    }
    return true;
}

// static
TrackId SimilarityIndex::chooseSeed(TrackId ownTrackId,
        bool isStemFile,
        TrackId originalTrackId,
        const std::function<bool(TrackId)>& hasVector) {
    if (ownTrackId.isValid() && hasVector(ownTrackId)) {
        return ownTrackId;
    }
    if (isStemFile && originalTrackId.isValid() && originalTrackId != ownTrackId &&
            hasVector(originalTrackId)) {
        return originalTrackId;
    }
    return TrackId();
}

bool SimilarityIndex::hasVectorFor(TrackId trackId) {
    if (!trackId.isValid() || !ensureLoaded()) {
        return false;
    }
    ensureLibraryMapping();
    return m_rowByTrackId.contains(trackId);
}

QList<SimilarityIndex::Neighbour> SimilarityIndex::nearestAbove(
        TrackId seedTrackId, double minScore, int minCount) {
    QList<Neighbour> results = nearest(seedTrackId, std::numeric_limits<int>::max());
    int keep = 0;
    while (keep < results.size() &&
            (keep < minCount || results.at(keep).score >= minScore)) {
        ++keep;
    }
    results.resize(keep);
    return results;
}

QList<SimilarityIndex::Neighbour> SimilarityIndex::nearest(TrackId seedTrackId, int count) {
    QList<Neighbour> results;
    if (count <= 0 || !seedTrackId.isValid() || !ensureLoaded()) {
        return results;
    }
    ensureLibraryMapping();
    const int seedRow = m_rowByTrackId.value(seedTrackId, -1);
    if (seedRow < 0) {
        return results;
    }

    const int rowCount = m_relPaths.size();
    const float* pSeed = &m_vectors[static_cast<size_t>(seedRow) * m_dimension];
    QList<Neighbour> candidates;
    candidates.reserve(rowCount);
    for (int row = 0; row < rowCount; ++row) {
        if (row == seedRow || m_trackIdsByRow[row].isEmpty()) {
            continue;
        }
        const float* pOther = &m_vectors[static_cast<size_t>(row) * m_dimension];
        // Both vectors are unit length, so this dot product is the cosine similarity.
        double score = 0.0;
        for (int i = 0; i < m_dimension; ++i) {
            score += static_cast<double>(pSeed[i]) * static_cast<double>(pOther[i]);
        }
        for (const TrackId& trackId : m_trackIdsByRow.at(row)) {
            if (trackId == seedTrackId || !isEligibleResult(trackId)) {
                continue;
            }
            candidates.append(Neighbour{trackId, score});
        }
    }

    const int wanted = std::min(count, static_cast<int>(candidates.size()));
    std::partial_sort(candidates.begin(),
            candidates.begin() + wanted,
            candidates.end(),
            [](const Neighbour& lhs, const Neighbour& rhs) {
                if (lhs.score != rhs.score) {
                    return lhs.score > rhs.score;
                }
                // Stable order for ties so repeated queries agree.
                return lhs.trackId < rhs.trackId;
            });
    results.reserve(wanted);
    for (int i = 0; i < wanted; ++i) {
        results.append(candidates.at(i));
    }
    return results;
}

SimilarityIndex::Status SimilarityIndex::status() {
    Status status;
    status.loaded = ensureLoaded();
    status.filePath = m_filePath;
    status.error = m_error;
    status.builtAt = m_builtAt;
    status.sourceRoot = m_sourceRoot;
    status.vectorCount = m_relPaths.size();
    if (!status.loaded) {
        return status;
    }
    ensureLibraryMapping();
    status.libraryTrackCount = m_libraryTrackCount;
    status.matchedCount = m_rowByTrackId.size();
    int claimed = 0;
    for (const QList<TrackId>& trackIds : std::as_const(m_trackIdsByRow)) {
        if (!trackIds.isEmpty()) {
            ++claimed;
        }
    }
    status.unclaimedCount = status.vectorCount - claimed;
    return status;
}
