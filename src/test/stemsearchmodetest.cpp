#include <gtest/gtest.h>

#include <QSet>
#include <QSqlQuery>
#include <QString>

#include "control/controlobject.h"
#include "library/basetrackcache.h"
#include "library/dao/trackschema.h"
#include "library/librarytablemodel.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "test/librarytest.h"
#include "track/track.h"

namespace {

/// andy-custom: [Library],stem_search_mode narrows a typed search to stem files
/// (1) or hides them (2); browsing without search text is never filtered.
class StemSearchModeTest : public LibraryTest {
  protected:
    StemSearchModeTest()
            : m_stemSearchMode(ConfigKey(QStringLiteral("[Library]"),
                      QStringLiteral("stem_search_mode"))),
              m_model(nullptr, trackCollectionManager(), "mixxx.db.model.library") {
    }

    void SetUp() override {
        m_stemA = getOrAddTrackByLocation(
                getTestDir().filePath(QStringLiteral("stems/sin_AAC_256kbps_VBR.stem.mp4")));
        m_stemB = getOrAddTrackByLocation(
                getTestDir().filePath(QStringLiteral("stems/sin_ALAC_24bit.stem.mp4")));
        m_plain = getOrAddTrackByLocation(getTestFile(QStringLiteral("-vbr.mp3")));
        ASSERT_TRUE(m_stemA && m_stemB && m_plain);
        connectTrackSource();
        m_model.setTableModel();
    }

    // The app's track source (search + sort) is created by
    // MixxxLibraryFeature, which a LibraryTest doesn't have. Without it the
    // model never filters and every search returns all tracks.
    void connectTrackSource() {
        TrackCollection* pCollection = trackCollectionManager()->internalCollection();
        const QStringList columns = {
                LIBRARYTABLE_ID,
                LIBRARYTABLE_ARTIST,
                LIBRARYTABLE_TITLE,
                TRACKLOCATIONSTABLE_LOCATION};
        QStringList qualifiedColumns;
        for (const auto& col : columns) {
            qualifiedColumns.append(mixxx::trackschema::tableForColumn(col) +
                    QLatin1Char('.') + col);
        }
        const QString tableName = QStringLiteral("stem_search_test_cache_view");
        QSqlQuery query(pCollection->database());
        ASSERT_TRUE(query.exec(QStringLiteral(
                "CREATE TEMPORARY VIEW IF NOT EXISTS %1 AS SELECT %2 FROM library "
                "INNER JOIN track_locations ON library.location = track_locations.id")
                        .arg(tableName, qualifiedColumns.join(','))));
        pCollection->connectTrackSource(QSharedPointer<BaseTrackCache>::create(
                pCollection,
                tableName,
                LIBRARYTABLE_ID,
                columns,
                QStringList{LIBRARYTABLE_ARTIST,
                        LIBRARYTABLE_TITLE,
                        TRACKLOCATIONSTABLE_LOCATION},
                true));
    }

    QSet<TrackId> idsFor(double mode, const QString& search) {
        m_stemSearchMode.set(mode);
        m_model.search(search);
        QSet<TrackId> ids;
        for (int row = 0; row < m_model.rowCount(); ++row) {
            ids.insert(m_model.getTrackId(m_model.index(row, 0)));
        }
        return ids;
    }

    ControlObject m_stemSearchMode;
    LibraryTableModel m_model;
    TrackPointer m_stemA;
    TrackPointer m_stemB;
    TrackPointer m_plain;
};

// "location:/" matches every track, so the mode is the only thing filtering.
const QString kMatchAll = QStringLiteral("location:/");

TEST_F(StemSearchModeTest, allShowsEverything) {
    const QSet<TrackId> expected{m_stemA->getId(), m_stemB->getId(), m_plain->getId()};
    EXPECT_EQ(expected, idsFor(0, kMatchAll));
}

TEST_F(StemSearchModeTest, stemsOnly) {
    const QSet<TrackId> expected{m_stemA->getId(), m_stemB->getId()};
    EXPECT_EQ(expected, idsFor(1, kMatchAll));
}

TEST_F(StemSearchModeTest, noStems) {
    const QSet<TrackId> expected{m_plain->getId()};
    EXPECT_EQ(expected, idsFor(2, kMatchAll));
}

TEST_F(StemSearchModeTest, appliesToEveryBranchOfAnOrQuery) {
    // Without AND-ing the mode on top, it would only bind to the last branch.
    const QString orQuery = QStringLiteral("location:sin_aac | location:vbr.mp3");
    const QSet<TrackId> expected{m_plain->getId()};
    EXPECT_EQ(expected, idsFor(2, orQuery));
    const QSet<TrackId> expectedStems{m_stemA->getId()};
    EXPECT_EQ(expectedStems, idsFor(1, orQuery));
}

TEST_F(StemSearchModeTest, browsingWithoutSearchTextIsNotFiltered) {
    const QSet<TrackId> expected{m_stemA->getId(), m_stemB->getId(), m_plain->getId()};
    EXPECT_EQ(expected, idsFor(1, QString()));
    EXPECT_EQ(expected, idsFor(2, QStringLiteral("   ")));
}

} // namespace
