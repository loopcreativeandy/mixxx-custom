#include "library/trackset/similar/similartablemodel.h"

#include <QSqlQuery>
#include <algorithm>

#include "library/dao/trackschema.h"
#include "library/queryutil.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "moc_similartablemodel.cpp"

namespace {

const QString kTableName = QStringLiteral("similar_results");
const QString kViewName = QStringLiteral("similar_view");

} // anonymous namespace

SimilarTableModel::SimilarTableModel(QObject* parent,
        TrackCollectionManager* pTrackCollectionManager)
        : LibraryTableModel(parent,
                  pTrackCollectionManager,
                  // Own settings name, so the column layout and sort order of this view
                  // are saved separately and existing library/playlist headers are left
                  // exactly as they are.
                  "mixxx.db.model.similar") {
    setupSimilarTable();
}

void SimilarTableModel::setupSimilarTable() {
    QSqlQuery query(m_database);
    // Session-local, so nothing is added to the database file.
    if (!query.exec(QStringLiteral("CREATE TEMP TABLE IF NOT EXISTS ") + kTableName +
                QStringLiteral("(") + LIBRARYTABLE_ID +
                QStringLiteral(" INTEGER PRIMARY KEY, ") + LIBRARYTABLE_SIMILARITY +
                QStringLiteral(" REAL, ") + LIBRARYTABLE_SIMILARITY_RANK +
                QStringLiteral(" INTEGER)"))) {
        LOG_FAILED_QUERY(query);
    }

    QStringList columns;
    columns << kTableName + QStringLiteral(".") + LIBRARYTABLE_ID
            << kTableName + QStringLiteral(".") + LIBRARYTABLE_SIMILARITY_RANK
            << kTableName + QStringLiteral(".") + LIBRARYTABLE_SIMILARITY
            << QStringLiteral("'' AS ") + LIBRARYTABLE_PREVIEW
            << LIBRARYTABLE_COVERART_DIGEST + QStringLiteral(" AS ") + LIBRARYTABLE_COVERART;

    if (!query.exec(QStringLiteral("CREATE TEMPORARY VIEW IF NOT EXISTS ") + kViewName +
                QStringLiteral(" AS SELECT ") + columns.join(QChar(',')) +
                QStringLiteral(" FROM library "
                               "INNER JOIN track_locations "
                               "ON library.location = track_locations.id "
                               "INNER JOIN ") +
                kTableName + QStringLiteral(" ON ") + kTableName +
                QStringLiteral(".") + LIBRARYTABLE_ID +
                QStringLiteral(" = library.id "
                               "WHERE library.mixxx_deleted = 0 "
                               "AND track_locations.fs_deleted = 0"))) {
        LOG_FAILED_QUERY(query);
    }

    QStringList tableColumns;
    tableColumns << LIBRARYTABLE_ID
                 << LIBRARYTABLE_SIMILARITY_RANK
                 << LIBRARYTABLE_SIMILARITY
                 << LIBRARYTABLE_PREVIEW
                 << LIBRARYTABLE_COVERART;
    setTable(kViewName,
            LIBRARYTABLE_ID,
            tableColumns,
            m_pTrackCollectionManager->internalCollection()->getTrackSource());
    setSearch(QString());
    setDefaultSort(fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_SIMILARITY_RANK),
            Qt::AscendingOrder);
    setSort(defaultSortColumn(), defaultSortOrder());
}

void SimilarTableModel::setResults(
        const QList<SimilarityIndex::Neighbour>& results, TrackId seedTrackId) {
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("DELETE FROM ") + kTableName)) {
        LOG_FAILED_QUERY(query);
        return;
    }
    if (!results.isEmpty() || seedTrackId.isValid()) {
        query.prepare(QStringLiteral("INSERT INTO ") + kTableName +
                QStringLiteral(" (") + LIBRARYTABLE_ID + QStringLiteral(", ") +
                LIBRARYTABLE_SIMILARITY + QStringLiteral(", ") +
                LIBRARYTABLE_SIMILARITY_RANK +
                QStringLiteral(") VALUES (:id, :score, :rank)"));
        // Rank 1 = highest score. Fixed at query time: it stays with the track
        // when the view is re-sorted or filtered. Ranked here rather than
        // trusting the caller's order.
        QList<SimilarityIndex::Neighbour> ranked = results;
        std::stable_sort(ranked.begin(),
                ranked.end(),
                [](const SimilarityIndex::Neighbour& lhs,
                        const SimilarityIndex::Neighbour& rhs) {
                    return lhs.score > rhs.score;
                });
        // The seed itself on top as rank 0 (Andy, 2026-09-23), so it is clear
        // what the list is similar *to*. Similarity 1.0 by definition.
        if (seedTrackId.isValid()) {
            ranked.erase(std::remove_if(ranked.begin(),
                                 ranked.end(),
                                 [seedTrackId](const SimilarityIndex::Neighbour& result) {
                                     return result.trackId == seedTrackId;
                                 }),
                    ranked.end());
            query.bindValue(QStringLiteral(":id"), seedTrackId.toVariant());
            query.bindValue(QStringLiteral(":rank"), 0);
            query.bindValue(QStringLiteral(":score"), 1.0);
            if (!query.exec()) {
                LOG_FAILED_QUERY(query);
            }
        }
        int rank = 0;
        for (const auto& result : std::as_const(ranked)) {
            query.bindValue(QStringLiteral(":id"), result.trackId.toVariant());
            query.bindValue(QStringLiteral(":rank"), ++rank);
            // Bound as a double on purpose: a REAL column sorts numerically, a text
            // one would sort "0.9" after "0.85".
            query.bindValue(QStringLiteral(":score"), result.score);
            if (!query.exec()) {
                LOG_FAILED_QUERY(query);
            }
        }
    }
    // Opening the view starts unfiltered; the search bar then filters within the hits.
    setSearch(QString());
    // By rank rather than by score: a duplicate file scores 1.0 too and could
    // otherwise sit above the seed.
    setSort(fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_SIMILARITY_RANK), Qt::AscendingOrder);
    select();
}

TrackModel::Capabilities SimilarTableModel::getCapabilities() const {
    return LibraryTableModel::getCapabilities() & ~Capabilities(Capability::ReceiveDrops);
}

void SimilarTableModel::initSortColumnMapping() {
    BaseSqlTableModel::initSortColumnMapping();
    const int similarityColumn = fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_SIMILARITY);
    m_columnIndexBySortColumnId[static_cast<int>(TrackModel::SortColumnId::Similarity)] =
            similarityColumn;
    // The base class builds the reverse map before we get here, so this side of the
    // mapping has to be added too - otherwise clicking the header would report an
    // unknown sort column and the saved sort order could not be restored.
    if (similarityColumn >= 0) {
        m_sortColumnIdByColumnIndex.insert(
                similarityColumn, TrackModel::SortColumnId::Similarity);
    }
    const int rankColumn = fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_SIMILARITY_RANK);
    m_columnIndexBySortColumnId[static_cast<int>(TrackModel::SortColumnId::SimilarityRank)] =
            rankColumn;
    if (rankColumn >= 0) {
        m_sortColumnIdByColumnIndex.insert(
                rankColumn, TrackModel::SortColumnId::SimilarityRank);
    }
}
