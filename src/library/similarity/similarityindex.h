#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QMultiHash>
#include <QString>
#include <functional>
#include <vector>

#include "preferences/usersettings.h"
#include "track/trackid.h"

class TrackCollectionManager;

/// Nearest-neighbour lookup over an external similarity index (andy-custom).
///
/// The index is a standalone SQLite file of L2-normalized float vectors keyed by a
/// path relative to the music library, built outside Mixxx and copied in alongside
/// the music. Deliberately not a table in mixxxdb.sqlite: adding one would raise the
/// required schema version and lock other Mixxx builds out of the same database.
///
/// Because the vectors are normalized, the dot product *is* the cosine similarity, so
/// a full scan is one matrix-vector product - well under a millisecond for a personal
/// library. There is no approximate-nearest-neighbour structure and no worker thread;
/// queries run straight on the calling thread.
///
/// Nothing is written, ever: the file is opened read-only.
class SimilarityIndex {
  public:
    /// One library track as far as this class is concerned.
    struct LibraryTrack {
        TrackId trackId;
        QString location;
        double durationSeconds = 0.0;
    };

    struct Neighbour {
        TrackId trackId;
        double score = 0.0;
    };

    /// What to show the user about the index - it is copied in by hand, so it can be
    /// absent, out of date, or from a different library.
    struct Status {
        bool loaded = false;
        QString filePath;
        QString error;
        QString builtAt;
        QString sourceRoot;
        int vectorCount = 0;
        int libraryTrackCount = 0;
        /// Library tracks that were matched to a vector.
        int matchedCount = 0;
        /// Vectors that no library track claimed (removed or renamed files).
        int unclaimedCount = 0;
    };

    explicit SimilarityIndex(UserSettingsPointer pConfig);
    ~SimilarityIndex();

    /// Number of results a query returns, from the config.
    int resultCount() const;

    /// True if `trackId` can be used as a seed. Triggers the lazy load.
    bool hasVectorFor(TrackId trackId);

    /// Which track's vector stands in for `ownTrackId` as a seed (andy-custom,
    /// 2026-09-23). Its own vector if it has one; otherwise, for a stem file,
    /// the vector of the original it was generated from - stems are the same
    /// recording, but the index is built from the originals. Invalid if neither
    /// has a vector.
    static TrackId chooseSeed(TrackId ownTrackId,
            bool isStemFile,
            TrackId originalTrackId,
            const std::function<bool(TrackId)>& hasVector);

    /// The `count` most similar tracks, best first, seed excluded. Empty when the
    /// seed has no vector or the index is unavailable.
    QList<Neighbour> nearest(TrackId seedTrackId, int count);

    /// Status for the sidebar's empty state. Triggers the lazy load.
    Status status();

    /// Forces the next call to re-read the file and re-match the library.
    void invalidate();

    // --- Seams for testing: these bypass the config and the track collection.

    /// Open an index file explicitly instead of looking for it in the library.
    bool openForTesting(const QString& filePath);
    /// Supply the library instead of querying the track collection.
    void setLibraryTracksForTesting(const QList<LibraryTrack>& tracks);

    void setTrackCollectionManager(TrackCollectionManager* pTrackCollectionManager) {
        m_pTrackCollectionManager = pTrackCollectionManager;
    }

  private:
    /// Loads the vectors if that has not happened yet; returns false if unavailable.
    bool ensureLoaded();
    /// Re-reads the library and re-matches it against the vectors when stale.
    void ensureLibraryMapping();

    bool loadVectors(const QString& filePath);
    QString findIndexFile() const;
    QList<LibraryTrack> readLibraryTracks() const;
    void matchLibrary(const QList<LibraryTrack>& tracks);

    /// Row holding the vector for `location`, or -1. Matches on the longest
    /// trailing path segment sequence, so the index does not care what the library
    /// is called or where it is mounted.
    int rowForLocation(const QString& location) const;
    /// Row for a file whose path did not match, by file name alone. Ambiguity is
    /// broken on duration; -1 when it stays ambiguous.
    int rowForBasename(const QString& location, double durationSeconds) const;

    bool isEligibleResult(TrackId trackId) const;

    const UserSettingsPointer m_pConfig;
    TrackCollectionManager* m_pTrackCollectionManager;

    bool m_loadAttempted;
    bool m_loaded;
    bool m_useTestLibrary;
    QString m_filePath;
    QString m_error;
    QString m_builtAt;
    QString m_sourceRoot;

    int m_dimension;
    int m_maxPathDepth;
    /// Row-major, m_dimension floats per row.
    std::vector<float> m_vectors;
    QList<QString> m_relPaths;
    std::vector<double> m_indexDurations;
    QHash<QString, int> m_rowByRelPath;
    QMultiHash<QString, int> m_rowsByBasename;

    QElapsedTimer m_mappingAge;
    bool m_mappingValid;
    QHash<TrackId, int> m_rowByTrackId;
    QList<QList<TrackId>> m_trackIdsByRow;
    QHash<TrackId, double> m_durationByTrackId;
    QHash<TrackId, bool> m_excludedByTrackId;
    int m_libraryTrackCount;
    QList<LibraryTrack> m_testLibraryTracks;
};
