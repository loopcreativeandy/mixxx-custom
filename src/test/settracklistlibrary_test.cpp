#include <gtest/gtest.h>

#include <QFile>
#include <QTemporaryDir>

#include "library/settracklist.h"
#include "test/librarytest.h"
#include "track/track.h"

using namespace mixxx::settracklist;

namespace {

class SetTracklistLibraryTest : public LibraryTest {
  protected:
    QString copyTestFile(const QString& name, const QString& targetName) {
        const QString source = getTestDir().filePath(QStringLiteral("id3-test-data/") + name);
        const QString target = m_tempDir.filePath(targetName);
        EXPECT_TRUE(QFile::copy(source, target));
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
        return target;
    }

    TrackPointer addTrack(const QString& location, const QString& artist, const QString& title) {
        TrackPointer pTrack = getOrAddTrackByLocation(location);
        EXPECT_TRUE(pTrack);
        pTrack->setArtist(artist);
        pTrack->setTitle(title);
        trackCollectionManager()->saveTrack(pTrack);
        return pTrack;
    }

    QTemporaryDir m_tempDir;
};

TEST_F(SetTracklistLibraryTest, MarkingASetMarksItsSongsFlagOnly) {
    const TrackPointer pSong1 = addTrack(
            copyTestFile(QStringLiteral("cover-test-øé~ł€˚-jpg.mp3"), QStringLiteral("song1.mp3")),
            QStringLiteral("Bicep"),
            QStringLiteral("Glue"));
    // Moved since the recording: found by artist + title.
    const TrackPointer pSong2 = addTrack(
            copyTestFile(QStringLiteral("cover-test-øé~ł€˚.flac"), QStringLiteral("song2.flac")),
            QStringLiteral("Lagum"),
            QStringLiteral("Deixa"));
    const TrackPointer pUnrelated = addTrack(
            copyTestFile(QStringLiteral("cover-test-øé~ł€˚.ogg"), QStringLiteral("other.ogg")),
            QStringLiteral("Someone"),
            QStringLiteral("Elsewhere"));
    const QString setLocation = copyTestFile(
            QStringLiteral("cover-test-øé~ł€˚-png.mp3"), QStringLiteral("set.mp3"));
    ASSERT_TRUE(write(setLocation,
            {Entry{pSong1->getLocation(), QStringLiteral("Bicep"), QStringLiteral("Glue"), 0, 90000},
                    Entry{QStringLiteral("/gone/Deixa.flac"),
                            QStringLiteral("Lagum"),
                            QStringLiteral("Deixa"),
                            80000,
                            90000},
                    Entry{QStringLiteral("/gone/missing.mp3"),
                            QStringLiteral("Nobody"),
                            QStringLiteral("Not In Library"),
                            160000,
                            90000}}));
    const TrackPointer pSet = getOrAddTrackByLocation(setLocation);
    ASSERT_TRUE(pSet);

    EXPECT_EQ(2, markSetsOf(trackCollectionManager(), config(), {pSet}, true));
    EXPECT_TRUE(pSong1->getPlayCounter().isPlayed());
    EXPECT_TRUE(pSong2->getPlayCounter().isPlayed());
    EXPECT_FALSE(pUnrelated->getPlayCounter().isPlayed());
    EXPECT_EQ(0, pSong1->getTimesPlayed());
    EXPECT_EQ(0, pSong2->getTimesPlayed());

    EXPECT_EQ(2, markSetsOf(trackCollectionManager(), config(), {pSet}, false));
    EXPECT_FALSE(pSong1->getPlayCounter().isPlayed());
    EXPECT_FALSE(pSong2->getPlayCounter().isPlayed());
}

TEST_F(SetTracklistLibraryTest, PlainTrackMarksNothingElse) {
    const TrackPointer pTrack = addTrack(
            copyTestFile(QStringLiteral("cover-test-øé~ł€˚-jpg.mp3"), QStringLiteral("plain.mp3")),
            QStringLiteral("A"),
            QStringLiteral("B"));
    EXPECT_EQ(0, markSetsOf(trackCollectionManager(), config(), {pTrack}, true));
}

} // namespace
