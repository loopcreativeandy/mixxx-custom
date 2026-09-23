#include <gtest/gtest.h>

#include <QList>
#include <QString>

#include "library/columncache.h"
#include "library/librarytablemodel.h"
#include "library/trackmodel.h"
#include "library/trackset/similar/similartablemodel.h"
#include "test/librarytest.h"
#include "track/track.h"

namespace {

/// Covers the mechanism the result view is built on: a temporary table of scores joined
/// into the view, which is what makes the score a real sortable column without adding
/// anything to the database schema.
class SimilarTableModelTest : public LibraryTest {
  protected:
    SimilarTableModelTest()
            : m_model(nullptr, trackCollectionManager()) {
    }

    /// Three tracks in the library, whatever their tags happen to say - only their ids
    /// matter here.
    void addThreeTracks() {
        m_trackA = getOrAddTrackByLocation(getTestFile(QStringLiteral("-jpg.mp3")));
        m_trackB = getOrAddTrackByLocation(getTestFile(QStringLiteral("-png.mp3")));
        m_trackC = getOrAddTrackByLocation(getTestFile(QStringLiteral("-vbr.mp3")));
        ASSERT_TRUE(m_trackA && m_trackB && m_trackC);
    }

    int similarityColumn() const {
        return m_model.fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_SIMILARITY);
    }

    QList<TrackId> idsInOrder() const {
        QList<TrackId> ids;
        for (int row = 0; row < m_model.rowCount(); ++row) {
            ids.append(m_model.getTrackId(m_model.index(row, 0)));
        }
        return ids;
    }

    SimilarTableModel m_model;
    TrackPointer m_trackA;
    TrackPointer m_trackB;
    TrackPointer m_trackC;
};

TEST_F(SimilarTableModelTest, showsOnlyTheResultsInScoreOrder) {
    addThreeTracks();
    // Deliberately handed over out of order: the view has to do the sorting.
    m_model.setResults(QList<SimilarityIndex::Neighbour>{
            {m_trackB->getId(), 0.907},
            {m_trackA->getId(), 0.912},
            {m_trackC->getId(), 0.500},
    });

    ASSERT_EQ(3, m_model.rowCount());
    const QList<TrackId> expected{m_trackA->getId(), m_trackB->getId(), m_trackC->getId()};
    EXPECT_EQ(expected, idsInOrder());
}

TEST_F(SimilarTableModelTest, aTrackOutsideTheResultsIsNotShown) {
    addThreeTracks();
    m_model.setResults(QList<SimilarityIndex::Neighbour>{
            {m_trackA->getId(), 0.9},
    });

    ASSERT_EQ(1, m_model.rowCount());
    EXPECT_EQ(m_trackA->getId(), m_model.getTrackId(m_model.index(0, 0)));
}

TEST_F(SimilarTableModelTest, aNewQueryReplacesThePreviousResults) {
    addThreeTracks();
    m_model.setResults(QList<SimilarityIndex::Neighbour>{
            {m_trackA->getId(), 0.9},
            {m_trackB->getId(), 0.8},
    });
    ASSERT_EQ(2, m_model.rowCount());

    m_model.setResults(QList<SimilarityIndex::Neighbour>{
            {m_trackC->getId(), 0.7},
    });
    ASSERT_EQ(1, m_model.rowCount());
    EXPECT_EQ(m_trackC->getId(), m_model.getTrackId(m_model.index(0, 0)));
}

TEST_F(SimilarTableModelTest, anEmptyResultSetEmptiesTheView) {
    addThreeTracks();
    m_model.setResults(QList<SimilarityIndex::Neighbour>{
            {m_trackA->getId(), 0.9},
    });
    ASSERT_EQ(1, m_model.rowCount());

    m_model.setResults(QList<SimilarityIndex::Neighbour>());
    EXPECT_EQ(0, m_model.rowCount());
}

TEST_F(SimilarTableModelTest, theScoreIsShownToThreeDecimals) {
    addThreeTracks();
    m_model.setResults(QList<SimilarityIndex::Neighbour>{
            {m_trackA->getId(), 0.9124},
            {m_trackB->getId(), 0.9},
    });

    ASSERT_GE(similarityColumn(), 0);
    // Two decimals would print both of these as 0.91 and hide the ranking.
    EXPECT_QSTRING_EQ(QStringLiteral("0.912"),
            m_model.index(0, similarityColumn()).data().toString());
    EXPECT_QSTRING_EQ(QStringLiteral("0.900"),
            m_model.index(1, similarityColumn()).data().toString());
}

TEST_F(SimilarTableModelTest, sortingOnTheScoreColumnIsNumericAndReversible) {
    addThreeTracks();
    m_model.setResults(QList<SimilarityIndex::Neighbour>{
            // 0.9 vs 0.85: as text "0.85" sorts before "0.9", numerically it is the
            // other way round, so this catches a lexicographic sort.
            {m_trackA->getId(), 0.9},
            {m_trackB->getId(), 0.85},
            {m_trackC->getId(), 0.1},
    });

    m_model.sort(similarityColumn(), Qt::AscendingOrder);
    const QList<TrackId> ascending{
            m_trackC->getId(), m_trackB->getId(), m_trackA->getId()};
    EXPECT_EQ(ascending, idsInOrder());

    m_model.sort(similarityColumn(), Qt::DescendingOrder);
    const QList<TrackId> descending{
            m_trackA->getId(), m_trackB->getId(), m_trackC->getId()};
    EXPECT_EQ(descending, idsInOrder());
}

TEST_F(SimilarTableModelTest, theScoreColumnIsAKnownSortColumn) {
    addThreeTracks();
    m_model.setResults(QList<SimilarityIndex::Neighbour>{
            {m_trackA->getId(), 0.9},
    });

    // Both directions of the mapping, because the header uses one to remember the sort
    // and the other to restore it.
    EXPECT_EQ(TrackModel::SortColumnId::Similarity,
            m_model.sortColumnIdFromColumnIndex(similarityColumn()));
    EXPECT_EQ(similarityColumn(),
            m_model.columnIndexFromSortColumnId(TrackModel::SortColumnId::Similarity));
}

TEST_F(SimilarTableModelTest, tracksCannotBeDroppedIntoTheResults) {
    // Membership comes from a query, so accepting drops would be meaningless.
    EXPECT_FALSE(m_model.hasCapabilities(TrackModel::Capability::ReceiveDrops));
}

TEST_F(SimilarTableModelTest, keepsItsOwnHeaderStateSeparateFromTheLibrary) {
    // The saved column layout and sort order are keyed off modelKey(), so sharing it
    // with the library model would let this view rearrange the library's columns - the
    // extra score column would land in every track table.
    LibraryTableModel libraryModel(nullptr, trackCollectionManager(), "mixxx.db.model.library");
    EXPECT_NE(libraryModel.modelKey(true), m_model.modelKey(true));
    EXPECT_TRUE(m_model.modelKey(true).contains(QStringLiteral("similar_view")));
    // And the score column exists only here.
    EXPECT_EQ(-1, libraryModel.fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_SIMILARITY));
    EXPECT_EQ(-1,
            libraryModel.columnIndexFromSortColumnId(
                    TrackModel::SortColumnId::Similarity));
}

// Andy, 2026-09-23: a rank column, 1 = most similar.
TEST_F(SimilarTableModelTest, ranksFromOneByScoreAndKeepsTheRankWhenResorted) {
    addThreeTracks();
    m_model.setResults(QList<SimilarityIndex::Neighbour>{
            {m_trackB->getId(), 0.907},
            {m_trackA->getId(), 0.912},
            {m_trackC->getId(), 0.950},
    });
    const int rankColumn =
            m_model.fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_SIMILARITY_RANK);
    ASSERT_GE(rankColumn, 0);
    const auto rankOf = [&](const TrackPointer& pTrack) {
        for (int row = 0; row < m_model.rowCount(); ++row) {
            if (m_model.getTrackId(m_model.index(row, 0)) == pTrack->getId()) {
                return m_model.data(m_model.index(row, rankColumn)).toInt();
            }
        }
        return -1;
    };
    EXPECT_EQ(1, rankOf(m_trackC));
    EXPECT_EQ(2, rankOf(m_trackA));
    EXPECT_EQ(3, rankOf(m_trackB));

    // Sorting by rank is sorting by similarity...
    m_model.sort(rankColumn, Qt::AscendingOrder);
    const QList<TrackId> byRank{m_trackC->getId(), m_trackA->getId(), m_trackB->getId()};
    EXPECT_EQ(byRank, idsInOrder());
    // ...and the rank belongs to the track, not to the row.
    m_model.sort(similarityColumn(), Qt::AscendingOrder);
    EXPECT_EQ(1, rankOf(m_trackC));
    EXPECT_EQ(3, rankOf(m_trackB));

    EXPECT_EQ(TrackModel::SortColumnId::SimilarityRank,
            m_model.sortColumnIdFromColumnIndex(rankColumn));
    EXPECT_EQ(rankColumn,
            m_model.columnIndexFromSortColumnId(TrackModel::SortColumnId::SimilarityRank));
}

} // anonymous namespace
