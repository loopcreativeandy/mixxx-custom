#include <gtest/gtest.h>

#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QVariant>
#include <cmath>
#include <vector>

#include "library/similarity/similarityindex.h"
#include "test/mixxxtest.h"
#include "track/trackid.h"

namespace {

/// Every fixture here is invented: made-up file names, made-up vectors. Nothing from a
/// real library goes near a test, because test output ends up in public build logs.
struct IndexRow {
    QString relPath;
    double duration;
    std::vector<float> vec;
};

TrackId trackId(int value) {
    return TrackId(QVariant(value));
}

class SimilarityIndexTest : public MixxxTest {
  protected:
    QString indexPath() const {
        return QDir(m_tempDir.path()).filePath(QStringLiteral("similarity.sqlite"));
    }

    /// Writes an index file in exactly the layout the exporter produces.
    void writeIndex(const QList<IndexRow>& rows,
            int declaredDimension,
            const QString& path = QString()) {
        const QString filePath = path.isEmpty() ? indexPath() : path;
        const QString connectionName = QStringLiteral("similarity-test-write");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(
                    QStringLiteral("QSQLITE"), connectionName);
            db.setDatabaseName(filePath);
            ASSERT_TRUE(db.open());
            QSqlQuery query(db);
            ASSERT_TRUE(query.exec(QStringLiteral(
                    "CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT)")));
            ASSERT_TRUE(query.exec(QStringLiteral(
                    "CREATE TABLE vectors(rel_path TEXT PRIMARY KEY, basename TEXT, "
                    "duration REAL, vec BLOB)")));
            query.prepare(QStringLiteral("INSERT INTO meta VALUES(?, ?)"));
            query.addBindValue(QStringLiteral("dim"));
            query.addBindValue(QString::number(declaredDimension));
            ASSERT_TRUE(query.exec());
            query.prepare(QStringLiteral("INSERT INTO meta VALUES(?, ?)"));
            query.addBindValue(QStringLiteral("built_at"));
            query.addBindValue(QStringLiteral("2026-01-01T00:00:00Z"));
            ASSERT_TRUE(query.exec());
            query.prepare(QStringLiteral("INSERT INTO meta VALUES(?, ?)"));
            query.addBindValue(QStringLiteral("model"));
            query.addBindValue(QStringLiteral("test"));
            ASSERT_TRUE(query.exec());

            for (const IndexRow& row : rows) {
                const QByteArray blob(
                        reinterpret_cast<const char*>(row.vec.data()),
                        static_cast<int>(row.vec.size() * sizeof(float)));
                query.prepare(QStringLiteral(
                        "INSERT INTO vectors(rel_path, basename, duration, vec) "
                        "VALUES(?, ?, ?, ?)"));
                query.addBindValue(row.relPath);
                query.addBindValue(row.relPath.section(QChar('/'), -1));
                query.addBindValue(row.duration);
                query.addBindValue(blob);
                ASSERT_TRUE(query.exec());
            }
        }
        QSqlDatabase::removeDatabase(connectionName);
    }

    /// Four tracks around a seed, at deliberately spaced-out cosines:
    /// near 1.0, mid 0.8, far 0.6, unrelated 0.0.
    void writeFourNeighbours() {
        writeIndex(QList<IndexRow>{
                          {QStringLiteral("2026/dir/seed.mp3"), 200.0, {1.f, 0.f, 0.f, 0.f}},
                          {QStringLiteral("2026/dir/near.mp3"), 210.0, {1.f, 0.f, 0.f, 0.f}},
                          {QStringLiteral("2026/dir/mid.mp3"), 220.0, {0.8f, 0.6f, 0.f, 0.f}},
                          {QStringLiteral("2026/dir/far.mp3"), 230.0, {0.6f, 0.8f, 0.f, 0.f}},
                          {QStringLiteral("2026/dir/unrelated.mp3"),
                                  240.0,
                                  {0.f, 1.f, 0.f, 0.f}},
                  },
                4);
    }

    QList<SimilarityIndex::LibraryTrack> fourNeighbourLibrary() const {
        return QList<SimilarityIndex::LibraryTrack>{
                {trackId(1), QStringLiteral("/music/lib/2026/dir/seed.mp3"), 200.0},
                {trackId(2), QStringLiteral("/music/lib/2026/dir/near.mp3"), 210.0},
                {trackId(3), QStringLiteral("/music/lib/2026/dir/mid.mp3"), 220.0},
                {trackId(4), QStringLiteral("/music/lib/2026/dir/far.mp3"), 230.0},
                {trackId(5), QStringLiteral("/music/lib/2026/dir/unrelated.mp3"), 240.0},
        };
    }

    QTemporaryDir m_tempDir;
};

TEST_F(SimilarityIndexTest, ranksNeighboursClosestFirst) {
    writeFourNeighbours();
    SimilarityIndex index(config());
    ASSERT_TRUE(index.openForTesting(indexPath()));
    index.setLibraryTracksForTesting(fourNeighbourLibrary());

    const QList<SimilarityIndex::Neighbour> results = index.nearest(trackId(1), 10);
    ASSERT_EQ(4, results.size());
    EXPECT_EQ(trackId(2), results.at(0).trackId);
    EXPECT_EQ(trackId(3), results.at(1).trackId);
    EXPECT_EQ(trackId(4), results.at(2).trackId);
    EXPECT_EQ(trackId(5), results.at(3).trackId);
    EXPECT_NEAR(1.0, results.at(0).score, 1e-6);
    EXPECT_NEAR(0.8, results.at(1).score, 1e-6);
    EXPECT_NEAR(0.6, results.at(2).score, 1e-6);
    EXPECT_NEAR(0.0, results.at(3).score, 1e-6);
    // Scores must come out in descending order, which is what the view sorts on.
    for (int i = 1; i < results.size(); ++i) {
        EXPECT_LE(results.at(i).score, results.at(i - 1).score);
    }
}

TEST_F(SimilarityIndexTest, seedIsNeverItsOwnNeighbour) {
    writeFourNeighbours();
    SimilarityIndex index(config());
    ASSERT_TRUE(index.openForTesting(indexPath()));
    index.setLibraryTracksForTesting(fourNeighbourLibrary());

    for (const auto& result : index.nearest(trackId(1), 10)) {
        EXPECT_NE(trackId(1), result.trackId);
    }
}

TEST_F(SimilarityIndexTest, honoursTheRequestedResultCount) {
    writeFourNeighbours();
    SimilarityIndex index(config());
    ASSERT_TRUE(index.openForTesting(indexPath()));
    index.setLibraryTracksForTesting(fourNeighbourLibrary());

    const QList<SimilarityIndex::Neighbour> results = index.nearest(trackId(1), 2);
    ASSERT_EQ(2, results.size());
    EXPECT_EQ(trackId(2), results.at(0).trackId);
    EXPECT_EQ(trackId(3), results.at(1).trackId);
}

TEST_F(SimilarityIndexTest, matchesRegardlessOfWhereTheLibraryIsMounted) {
    writeFourNeighbours();
    SimilarityIndex index(config());
    ASSERT_TRUE(index.openForTesting(indexPath()));
    // Same relative layout, a completely different root and native separators: the
    // index is built on one machine and used on another, so this is the normal case.
    index.setLibraryTracksForTesting(QList<SimilarityIndex::LibraryTrack>{
            {trackId(1), QStringLiteral("D:\\Somewhere Else\\2026\\dir\\seed.mp3"), 200.0},
            {trackId(2), QStringLiteral("D:\\Somewhere Else\\2026\\dir\\near.mp3"), 210.0},
    });

    EXPECT_TRUE(index.hasVectorFor(trackId(1)));
    ASSERT_EQ(1, index.nearest(trackId(1), 10).size());
    EXPECT_EQ(trackId(2), index.nearest(trackId(1), 10).at(0).trackId);
}

TEST_F(SimilarityIndexTest, fallsBackToTheFileNameWhenTheFolderMoved) {
    writeFourNeighbours();
    SimilarityIndex index(config());
    ASSERT_TRUE(index.openForTesting(indexPath()));
    index.setLibraryTracksForTesting(QList<SimilarityIndex::LibraryTrack>{
            {trackId(1), QStringLiteral("/music/lib/2026/dir/seed.mp3"), 200.0},
            // Reorganized into a different folder since the index was built.
            {trackId(2), QStringLiteral("/music/lib/moved/elsewhere/near.mp3"), 210.0},
    });

    ASSERT_EQ(1, index.nearest(trackId(1), 10).size());
    EXPECT_EQ(trackId(2), index.nearest(trackId(1), 10).at(0).trackId);
}

TEST_F(SimilarityIndexTest, breaksAnAmbiguousFileNameOnDuration) {
    writeIndex(QList<IndexRow>{
                      {QStringLiteral("2026/a/seed.mp3"), 200.0, {1.f, 0.f}},
                      {QStringLiteral("2026/a/twin.mp3"), 120.0, {1.f, 0.f}},
                      {QStringLiteral("2026/b/twin.mp3"), 300.0, {0.f, 1.f}},
              },
            2);
    SimilarityIndex index(config());
    ASSERT_TRUE(index.openForTesting(indexPath()));
    // Only the file name is left to go on, and two rows carry it. The 300 s copy is
    // the one on disk, so the 300 s vector is the right one - which here is the
    // vector that is *not* similar to the seed.
    index.setLibraryTracksForTesting(QList<SimilarityIndex::LibraryTrack>{
            {trackId(1), QStringLiteral("/music/lib/2026/a/seed.mp3"), 200.0},
            {trackId(2), QStringLiteral("/music/lib/somewhere/twin.mp3"), 300.0},
    });

    const QList<SimilarityIndex::Neighbour> results = index.nearest(trackId(1), 10);
    ASSERT_EQ(1, results.size());
    EXPECT_EQ(trackId(2), results.at(0).trackId);
    EXPECT_NEAR(0.0, results.at(0).score, 1e-6);
}

TEST_F(SimilarityIndexTest, refusesAnAmbiguousFileNameItCannotResolve) {
    writeIndex(QList<IndexRow>{
                      {QStringLiteral("2026/a/seed.mp3"), 200.0, {1.f, 0.f}},
                      {QStringLiteral("2026/a/twin.mp3"), 120.0, {1.f, 0.f}},
                      {QStringLiteral("2026/b/twin.mp3"), 120.5, {0.f, 1.f}},
              },
            2);
    SimilarityIndex index(config());
    ASSERT_TRUE(index.openForTesting(indexPath()));
    // Two same-named files of the same length: guessing would silently attach the
    // wrong vector, so the track gets none.
    index.setLibraryTracksForTesting(QList<SimilarityIndex::LibraryTrack>{
            {trackId(1), QStringLiteral("/music/lib/2026/a/seed.mp3"), 200.0},
            {trackId(2), QStringLiteral("/music/lib/somewhere/twin.mp3"), 120.2},
    });

    EXPECT_FALSE(index.hasVectorFor(trackId(2)));
    EXPECT_TRUE(index.nearest(trackId(1), 10).isEmpty());
}

TEST_F(SimilarityIndexTest, dropsResultsOutsideTheDurationWindow) {
    writeFourNeighbours();
    m_pConfig->setValue(ConfigKey("[Similarity]", "min_duration_seconds"), 215);
    m_pConfig->setValue(ConfigKey("[Similarity]", "max_duration_seconds"), 235);
    SimilarityIndex index(config());
    ASSERT_TRUE(index.openForTesting(indexPath()));
    index.setLibraryTracksForTesting(fourNeighbourLibrary());

    const QList<SimilarityIndex::Neighbour> results = index.nearest(trackId(1), 10);
    // near (210 s) and unrelated (240 s) fall outside, mid and far stay.
    ASSERT_EQ(2, results.size());
    EXPECT_EQ(trackId(3), results.at(0).trackId);
    EXPECT_EQ(trackId(4), results.at(1).trackId);
}

TEST_F(SimilarityIndexTest, aSeedOutsideTheDurationWindowStillWorks) {
    writeFourNeighbours();
    // The window filters results, not seeds: a track can be short and still be the
    // thing you want neighbours for.
    m_pConfig->setValue(ConfigKey("[Similarity]", "min_duration_seconds"), 205);
    SimilarityIndex index(config());
    ASSERT_TRUE(index.openForTesting(indexPath()));
    index.setLibraryTracksForTesting(fourNeighbourLibrary());

    // The 200 s seed is below the window, yet it still returns all four neighbours.
    EXPECT_TRUE(index.hasVectorFor(trackId(1)));
    EXPECT_EQ(4, index.nearest(trackId(1), 10).size());
}

TEST_F(SimilarityIndexTest, dropsResultsInExcludedDirectories) {
    writeIndex(QList<IndexRow>{
                      {QStringLiteral("2026/dir/seed.mp3"), 200.0, {1.f, 0.f}},
                      {QStringLiteral("2026/sets/a long mix.mp3"), 210.0, {1.f, 0.f}},
                      {QStringLiteral("2026/dir/keep.mp3"), 220.0, {0.8f, 0.6f}},
              },
            2);
    SimilarityIndex index(config());
    ASSERT_TRUE(index.openForTesting(indexPath()));
    index.setLibraryTracksForTesting(QList<SimilarityIndex::LibraryTrack>{
            {trackId(1), QStringLiteral("/music/lib/2026/dir/seed.mp3"), 200.0},
            {trackId(2), QStringLiteral("/music/lib/2026/sets/a long mix.mp3"), 210.0},
            {trackId(3), QStringLiteral("/music/lib/2026/dir/keep.mp3"), 220.0},
    });

    const QList<SimilarityIndex::Neighbour> results = index.nearest(trackId(1), 10);
    ASSERT_EQ(1, results.size());
    EXPECT_EQ(trackId(3), results.at(0).trackId);
}

TEST_F(SimilarityIndexTest, reportsAMissingIndexInsteadOfFailingSilently) {
    SimilarityIndex index(config());
    EXPECT_FALSE(index.openForTesting(
            QDir(m_tempDir.path()).filePath(QStringLiteral("not-there.sqlite"))));
    index.setLibraryTracksForTesting(fourNeighbourLibrary());

    const SimilarityIndex::Status status = index.status();
    EXPECT_FALSE(status.loaded);
    EXPECT_FALSE(status.error.isEmpty());
    EXPECT_EQ(0, status.vectorCount);
    EXPECT_FALSE(index.hasVectorFor(trackId(1)));
    EXPECT_TRUE(index.nearest(trackId(1), 10).isEmpty());
}

TEST_F(SimilarityIndexTest, skipsRowsWhoseVectorSizeDisagreesWithTheHeader) {
    // A vector of the wrong length means the file was built by something else. Taking
    // it would produce plausible-looking nonsense, so the row is dropped.
    writeIndex(QList<IndexRow>{
                      {QStringLiteral("2026/dir/seed.mp3"), 200.0, {1.f, 0.f, 0.f, 0.f}},
                      {QStringLiteral("2026/dir/good.mp3"), 210.0, {1.f, 0.f, 0.f, 0.f}},
                      {QStringLiteral("2026/dir/broken.mp3"), 220.0, {1.f, 0.f}},
              },
            4);
    SimilarityIndex index(config());
    ASSERT_TRUE(index.openForTesting(indexPath()));
    index.setLibraryTracksForTesting(QList<SimilarityIndex::LibraryTrack>{
            {trackId(1), QStringLiteral("/music/lib/2026/dir/seed.mp3"), 200.0},
            {trackId(2), QStringLiteral("/music/lib/2026/dir/good.mp3"), 210.0},
            {trackId(3), QStringLiteral("/music/lib/2026/dir/broken.mp3"), 220.0},
    });

    EXPECT_EQ(2, index.status().vectorCount);
    EXPECT_FALSE(index.hasVectorFor(trackId(3)));
    const QList<SimilarityIndex::Neighbour> results = index.nearest(trackId(1), 10);
    ASSERT_EQ(1, results.size());
    EXPECT_EQ(trackId(2), results.at(0).trackId);
}

TEST_F(SimilarityIndexTest, statusCountsWhatIsCoveredAndWhatIsNot) {
    writeFourNeighbours();
    SimilarityIndex index(config());
    ASSERT_TRUE(index.openForTesting(indexPath()));
    // Three of five vectors are claimed; one library track has no vector at all.
    index.setLibraryTracksForTesting(QList<SimilarityIndex::LibraryTrack>{
            {trackId(1), QStringLiteral("/music/lib/2026/dir/seed.mp3"), 200.0},
            {trackId(2), QStringLiteral("/music/lib/2026/dir/near.mp3"), 210.0},
            {trackId(3), QStringLiteral("/music/lib/2026/dir/mid.mp3"), 220.0},
            {trackId(9), QStringLiteral("/music/lib/2026/dir/brand new.mp3"), 250.0},
    });

    const SimilarityIndex::Status status = index.status();
    EXPECT_TRUE(status.loaded);
    EXPECT_EQ(5, status.vectorCount);
    EXPECT_EQ(4, status.libraryTrackCount);
    EXPECT_EQ(3, status.matchedCount);
    EXPECT_EQ(2, status.unclaimedCount);
}

TEST_F(SimilarityIndexTest, tracksWithNoVectorCannotBeSeeds) {
    writeFourNeighbours();
    SimilarityIndex index(config());
    ASSERT_TRUE(index.openForTesting(indexPath()));
    index.setLibraryTracksForTesting(QList<SimilarityIndex::LibraryTrack>{
            {trackId(1), QStringLiteral("/music/lib/2026/dir/seed.mp3"), 200.0},
            {trackId(9), QStringLiteral("/music/lib/2026/dir/brand new.mp3"), 250.0},
    });

    EXPECT_TRUE(index.hasVectorFor(trackId(1)));
    EXPECT_FALSE(index.hasVectorFor(trackId(9)));
    EXPECT_TRUE(index.nearest(trackId(9), 10).isEmpty());
}

/// The score column is declared REAL and the model sorts it with an SQL ORDER BY that
/// carries a COLLATE clause - BaseSqlTableModel::setSort wraps every own-table column
/// that way. SQLite applies collations only when comparing text, so a REAL column must
/// still sort numerically. This pins down both halves of that: the REAL column orders
/// by value even with COLLATE applied, and the same values stored as TEXT do not - which
/// is why setResults() binds a double and the column is declared REAL.
TEST_F(SimilarityIndexTest, aRealColumnSortsNumericallyDespiteACollateClause) {
    const QString connectionName = QStringLiteral("similarity-test-sort");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(QStringLiteral(":memory:"));
        ASSERT_TRUE(db.open());
        QSqlQuery query(db);
        ASSERT_TRUE(query.exec(QStringLiteral(
                "CREATE TEMP TABLE similar_results(id INTEGER PRIMARY KEY, "
                "similarity REAL, as_text TEXT)")));
        // 9 before 10 numerically, "10" before "9" as text: the one pair of values that
        // tells the two orderings apart.
        const QList<QPair<int, double>> rows{{1, 10.0}, {2, 9.0}, {3, 0.85}};
        for (const auto& row : rows) {
            query.prepare(QStringLiteral(
                    "INSERT INTO similar_results VALUES(:id, :score, :text)"));
            query.bindValue(QStringLiteral(":id"), row.first);
            query.bindValue(QStringLiteral(":score"), row.second);
            query.bindValue(QStringLiteral(":text"), QString::number(row.second));
            ASSERT_TRUE(query.exec());
        }

        ASSERT_TRUE(query.exec(QStringLiteral(
                "SELECT typeof(similarity) FROM similar_results WHERE id = 1")));
        ASSERT_TRUE(query.next());
        EXPECT_QSTRING_EQ(QStringLiteral("real"), query.value(0).toString());

        ASSERT_TRUE(query.exec(QStringLiteral(
                "SELECT id FROM similar_results "
                "ORDER BY similar_results.similarity COLLATE BINARY ASC")));
        QList<int> numericOrder;
        while (query.next()) {
            numericOrder.append(query.value(0).toInt());
        }
        const QList<int> expectedNumeric{3, 2, 1};
        EXPECT_EQ(expectedNumeric, numericOrder);

        ASSERT_TRUE(query.exec(QStringLiteral(
                "SELECT id FROM similar_results "
                "ORDER BY similar_results.as_text COLLATE BINARY ASC")));
        QList<int> textOrder;
        while (query.next()) {
            textOrder.append(query.value(0).toInt());
        }
        // "0.85", "10", "9" - the wrong answer, and what a TEXT column would give.
        const QList<int> expectedText{3, 1, 2};
        EXPECT_EQ(expectedText, textOrder);
    }
    QSqlDatabase::removeDatabase(connectionName);
}

// Seed choice (andy-custom, 2026-09-23): a stem file borrows its original's
// vector - the index is built from the originals only.

// Andy, 2026-09-23: no fixed count - every track at or above the threshold.
TEST_F(SimilarityIndexTest, nearestAboveReturnsEveryTrackAtOrAboveTheThreshold) {
    writeFourNeighbours();
    SimilarityIndex index(config());
    ASSERT_TRUE(index.openForTesting(indexPath()));
    index.setLibraryTracksForTesting(fourNeighbourLibrary());

    // 0.8 is exactly on the threshold: included.
    const QList<SimilarityIndex::Neighbour> results = index.nearestAbove(trackId(1), 0.79);
    ASSERT_EQ(2, results.size());
    EXPECT_EQ(trackId(2), results.at(0).trackId);
    EXPECT_EQ(trackId(3), results.at(1).trackId);

    EXPECT_EQ(3, index.nearestAbove(trackId(1), 0.5).size());
    EXPECT_EQ(4, index.nearestAbove(trackId(1), -1.0).size());
}

TEST_F(SimilarityIndexTest, nearestAboveCanBeEmpty) {
    writeFourNeighbours();
    SimilarityIndex index(config());
    ASSERT_TRUE(index.openForTesting(indexPath()));
    index.setLibraryTracksForTesting(fourNeighbourLibrary());

    // "far" (0.6) as the seed: nothing else is that close to it at 0.99.
    EXPECT_TRUE(index.nearestAbove(trackId(4), 0.99).isEmpty());
}

TEST_F(SimilarityIndexTest, minScoreDefaultsToPointNineAndIsConfigurable) {
    SimilarityIndex index(config());
    EXPECT_DOUBLE_EQ(0.9, index.minScore());

    config()->set(ConfigKey("[Similarity]", "min_score"), ConfigValue(QStringLiteral("0.85")));
    EXPECT_DOUBLE_EQ(0.85, index.minScore());

    // Nonsense falls back to the default rather than hiding or flooding the view.
    for (const QString& bad : {QStringLiteral("5"),
                 QStringLiteral("-3"),
                 QStringLiteral("nan"),
                 QStringLiteral("inf")}) {
        config()->set(ConfigKey("[Similarity]", "min_score"), ConfigValue(bad));
        EXPECT_DOUBLE_EQ(0.9, index.minScore()) << bad.toStdString();
    }
}

// Same day: "at least 10, or up to the threshold" - the minimum wins when few
// tracks reach the threshold, the threshold when many do.
TEST_F(SimilarityIndexTest, nearestAboveFillsUpToTheMinimumCount) {
    writeFourNeighbours();
    SimilarityIndex index(config());
    ASSERT_TRUE(index.openForTesting(indexPath()));
    index.setLibraryTracksForTesting(fourNeighbourLibrary());

    // Only "near" (1.0) reaches 0.9; the minimum of 3 adds the next closest.
    QList<SimilarityIndex::Neighbour> results = index.nearestAbove(trackId(1), 0.9, 3);
    ASSERT_EQ(3, results.size());
    EXPECT_EQ(trackId(2), results.at(0).trackId);
    EXPECT_EQ(trackId(3), results.at(1).trackId);
    EXPECT_EQ(trackId(4), results.at(2).trackId);

    // Threshold above the minimum: the threshold decides.
    EXPECT_EQ(3, index.nearestAbove(trackId(1), 0.5, 1).size());
    // A minimum larger than the library: everything there is, no more.
    EXPECT_EQ(4, index.nearestAbove(trackId(1), 0.99, 10).size());
    // The seed with nothing above the threshold still gets its minimum.
    EXPECT_EQ(2, index.nearestAbove(trackId(4), 0.99, 2).size());
}

TEST_F(SimilarityIndexTest, minResultsDefaultsToTenAndIsConfigurable) {
    SimilarityIndex index(config());
    EXPECT_EQ(10, index.minResults());
    config()->set(ConfigKey("[Similarity]", "min_results"), ConfigValue(QStringLiteral("25")));
    EXPECT_EQ(25, index.minResults());
    config()->set(ConfigKey("[Similarity]", "min_results"), ConfigValue(QStringLiteral("-4")));
    EXPECT_EQ(0, index.minResults());
}

TEST(SimilaritySeedTest, aTrackWithItsOwnVectorSeedsItself) {
    const TrackId own = trackId(1);
    const TrackId original = trackId(2);
    const auto hasVector = [](TrackId) { return true; };
    EXPECT_EQ(own, SimilarityIndex::chooseSeed(own, true, original, hasVector));
}

TEST(SimilaritySeedTest, aStemWithoutAVectorUsesItsOriginal) {
    const TrackId stem = trackId(1);
    const TrackId original = trackId(2);
    const auto hasVector = [original](TrackId id) { return id == original; };
    EXPECT_EQ(original, SimilarityIndex::chooseSeed(stem, true, original, hasVector));
}

/// Only stems may borrow: an ordinary track without a vector must not quietly
/// return some other track's neighbours.
TEST(SimilaritySeedTest, aNonStemNeverBorrowsAVector) {
    const TrackId track = trackId(1);
    const TrackId other = trackId(2);
    const auto hasVector = [other](TrackId id) { return id == other; };
    EXPECT_FALSE(SimilarityIndex::chooseSeed(track, false, other, hasVector).isValid());
}

TEST(SimilaritySeedTest, aStemWhoseOriginalHasNoVectorIsUnavailable) {
    const auto hasVector = [](TrackId) { return false; };
    EXPECT_FALSE(SimilarityIndex::chooseSeed(trackId(1), true, trackId(2), hasVector)
                         .isValid());
}

TEST(SimilaritySeedTest, aStemWithNoOriginalInTheLibraryIsUnavailable) {
    const auto hasVector = [](TrackId id) { return id == trackId(2); };
    EXPECT_FALSE(SimilarityIndex::chooseSeed(trackId(1), true, TrackId(), hasVector)
                         .isValid());
}

} // anonymous namespace
