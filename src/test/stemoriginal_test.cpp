#include "library/stemoriginal.h"

#include <gtest/gtest.h>

#include <QColor>
#include <QDir>
#include <QString>

#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "test/librarytest.h"
#include "track/beats.h"
#include "track/cue.h"
#include "track/track.h"
#include "util/color/predefinedcolorpalettes.h"

namespace {

constexpr auto kOriginalLocation = "/music/library/Artist - Title.mp3";
constexpr auto kStemLocation = "/music/stems/Artist - Title.stem.m4a";

TrackPointer newTrack(const QString& location, int sampleRate) {
    TrackPointer pTrack = Track::newTemporary(location);
    pTrack->setAudioProperties(
            mixxx::audio::ChannelCount(2),
            mixxx::audio::SampleRate(sampleRate),
            mixxx::audio::Bitrate(320),
            mixxx::Duration::fromSeconds(180));
    return pTrack;
}

class StemOriginalTest : public testing::Test {
  protected:
    StemOriginalTest()
            : m_pOriginal(newTrack(kOriginalLocation, 44100)),
              m_pStem(newTrack(kStemLocation, 44100)) {
    }

    TrackPointer m_pOriginal;
    TrackPointer m_pStem;
};

TEST_F(StemOriginalTest, detectsStemFiles) {
    EXPECT_TRUE(mixxx::stemoriginal::isStemFileLocation(kStemLocation));
    EXPECT_TRUE(mixxx::stemoriginal::isStemFileLocation("/a/b.stem.mp4"));
    // The suffix check must not care about case
    EXPECT_TRUE(mixxx::stemoriginal::isStemFileLocation("/a/b.STEM.M4A"));
    EXPECT_FALSE(mixxx::stemoriginal::isStemFileLocation(kOriginalLocation));
    // A regular m4a is not a stem file
    EXPECT_FALSE(mixxx::stemoriginal::isStemFileLocation("/a/b.m4a"));
    EXPECT_FALSE(mixxx::stemoriginal::isStemFileLocation("/a/stem.mp3"));
}

TEST_F(StemOriginalTest, stripsStemSuffix) {
    EXPECT_EQ(QStringLiteral("Artist - Title"),
            mixxx::stemoriginal::stemBaseName(kStemLocation));
    EXPECT_EQ(QStringLiteral("Trinix, Natalia Doco - Quedate Luna"),
            mixxx::stemoriginal::stemBaseName(
                    "/m/Trinix, Natalia Doco - Quedate Luna.stem.m4a"));
    // Dots inside the name survive
    EXPECT_EQ(QStringLiteral("Mr. X - Feat. Y"),
            mixxx::stemoriginal::stemBaseName("/m/Mr. X - Feat. Y.stem.mp4"));
    EXPECT_TRUE(mixxx::stemoriginal::stemBaseName(kOriginalLocation).isEmpty());
}

TEST_F(StemOriginalTest, onlyHotcuesAndLoopsCountAsUserCueData) {
    EXPECT_FALSE(mixxx::stemoriginal::hasUserCueData(*m_pStem));

    // Analyzer-generated cues are recreated on the next analysis and must not
    // trigger the overwrite warning.
    m_pStem->createAndAddCue(mixxx::CueType::MainCue,
            Cue::kNoHotCue,
            mixxx::audio::FramePos(1000),
            mixxx::audio::FramePos());
    m_pStem->createAndAddCue(mixxx::CueType::Intro,
            Cue::kNoHotCue,
            mixxx::audio::FramePos(1000),
            mixxx::audio::FramePos(2000));
    m_pStem->createAndAddCue(mixxx::CueType::N60dBSound,
            Cue::kNoHotCue,
            mixxx::audio::FramePos(500),
            mixxx::audio::FramePos(90000));
    EXPECT_FALSE(mixxx::stemoriginal::hasUserCueData(*m_pStem));

    m_pStem->createAndAddCue(mixxx::CueType::HotCue,
            0,
            mixxx::audio::FramePos(4000),
            mixxx::audio::FramePos());
    EXPECT_TRUE(mixxx::stemoriginal::hasUserCueData(*m_pStem));
}

TEST_F(StemOriginalTest, savedLoopCountsAsUserCueData) {
    m_pStem->createAndAddCue(mixxx::CueType::Loop,
            2,
            mixxx::audio::FramePos(4000),
            mixxx::audio::FramePos(8000));
    EXPECT_TRUE(mixxx::stemoriginal::hasUserCueData(*m_pStem));
}

TEST_F(StemOriginalTest, copiesCuesAtSameSampleRate) {
    m_pOriginal->createAndAddCue(mixxx::CueType::HotCue,
            3,
            mixxx::audio::FramePos(44100),
            mixxx::audio::FramePos(),
            mixxx::RgbColor(0xFF0000));
    const CuePointer pSourceLoop = m_pOriginal->createAndAddCue(mixxx::CueType::Loop,
            1,
            mixxx::audio::FramePos(88200),
            mixxx::audio::FramePos(132300));
    pSourceLoop->setLabel(QStringLiteral("drop"));

    // The stem track has a stale hot cue that must be gone afterwards
    m_pStem->createAndAddCue(mixxx::CueType::HotCue,
            7,
            mixxx::audio::FramePos(12345),
            mixxx::audio::FramePos());

    const auto result = mixxx::stemoriginal::importFromOriginal(*m_pStem, *m_pOriginal);
    EXPECT_EQ(2, result.cuesCopied);
    EXPECT_EQ(2, m_pStem->getCuePoints().size());
    EXPECT_EQ(nullptr, m_pStem->findHotcueByIndex(7));

    const CuePointer pHotcue = m_pStem->findHotcueByIndex(3);
    ASSERT_NE(nullptr, pHotcue);
    EXPECT_DOUBLE_EQ(44100, pHotcue->getPosition().value());
    EXPECT_EQ(mixxx::RgbColor(0xFF0000), pHotcue->getColor());

    const CuePointer pLoop = m_pStem->findHotcueByIndex(1);
    ASSERT_NE(nullptr, pLoop);
    EXPECT_EQ(mixxx::CueType::Loop, pLoop->getType());
    EXPECT_DOUBLE_EQ(88200, pLoop->getPosition().value());
    EXPECT_DOUBLE_EQ(132300, pLoop->getEndPosition().value());
    EXPECT_EQ(QStringLiteral("drop"), pLoop->getLabel());
}

TEST_F(StemOriginalTest, convertsCuePositionsBetweenSampleRates) {
    // Same music, different encoder settings: the stem file runs at 48 kHz
    TrackPointer pStem48 = newTrack(kStemLocation, 48000);
    m_pOriginal->createAndAddCue(mixxx::CueType::HotCue,
            0,
            mixxx::audio::FramePos(44100), // 1.0 s
            mixxx::audio::FramePos());

    mixxx::stemoriginal::importFromOriginal(*pStem48, *m_pOriginal);

    const CuePointer pHotcue = pStem48->findHotcueByIndex(0);
    ASSERT_NE(nullptr, pHotcue);
    // 1.0 s at 48 kHz
    EXPECT_NEAR(48000, pHotcue->getPosition().value(), 1);
}

TEST_F(StemOriginalTest, copiesBeatGridAndBpm) {
    const auto pBeats = mixxx::Beats::fromConstTempo(
            mixxx::audio::SampleRate(44100),
            mixxx::audio::FramePos(11025),
            mixxx::Bpm(128));
    ASSERT_TRUE(m_pOriginal->trySetBeats(pBeats));

    const auto result = mixxx::stemoriginal::importFromOriginal(*m_pStem, *m_pOriginal);
    EXPECT_TRUE(result.beatsCopied);

    const mixxx::BeatsPointer pStemBeats = m_pStem->getBeats();
    ASSERT_NE(nullptr, pStemBeats);
    EXPECT_DOUBLE_EQ(128, m_pStem->getBpm());
    EXPECT_DOUBLE_EQ(11025, pStemBeats->firstBeat().value());
}

TEST_F(StemOriginalTest, scalesBeatGridBetweenSampleRates) {
    TrackPointer pStem48 = newTrack(kStemLocation, 48000);
    const auto pBeats = mixxx::Beats::fromConstTempo(
            mixxx::audio::SampleRate(44100),
            mixxx::audio::FramePos(44100), // 1.0 s
            mixxx::Bpm(128));
    ASSERT_TRUE(m_pOriginal->trySetBeats(pBeats));

    mixxx::stemoriginal::importFromOriginal(*pStem48, *m_pOriginal);

    const mixxx::BeatsPointer pStemBeats = pStem48->getBeats();
    ASSERT_NE(nullptr, pStemBeats);
    // The tempo is a wall clock property and must not change with the rate
    EXPECT_DOUBLE_EQ(128, pStem48->getBpm());
    EXPECT_EQ(mixxx::audio::SampleRate(48000), pStemBeats->getSampleRate());
    // The anchor stays at 1.0 s, now expressed in 48 kHz frames
    EXPECT_NEAR(48000, pStemBeats->getLastMarkerPosition().value(), 1);
    // ... and so does the first beat of the track (0.0625 s in both files)
    EXPECT_NEAR(0.0625,
            pStemBeats->firstBeat().value() / 48000.0,
            0.0001);
    EXPECT_NEAR(0.0625,
            m_pOriginal->getBeats()->firstBeat().value() / 44100.0,
            0.0001);
}

TEST_F(StemOriginalTest, aLockedBpmOnTheStemDoesNotBlockTheImport) {
    const auto pBeats = mixxx::Beats::fromConstTempo(
            mixxx::audio::SampleRate(44100),
            mixxx::audio::FramePos(11025),
            mixxx::Bpm(128));
    ASSERT_TRUE(m_pOriginal->trySetBeats(pBeats));
    m_pStem->setBpmLocked(true);

    const auto result = mixxx::stemoriginal::importFromOriginal(*m_pStem, *m_pOriginal);
    EXPECT_TRUE(result.beatsCopied);
    EXPECT_DOUBLE_EQ(128, m_pStem->getBpm());
    // The original was not locked, so the stem must not stay locked either
    EXPECT_FALSE(m_pStem->isBpmLocked());
}

TEST_F(StemOriginalTest, adoptsTheBpmLockOfTheOriginal) {
    const auto pBeats = mixxx::Beats::fromConstTempo(
            mixxx::audio::SampleRate(44100),
            mixxx::audio::FramePos(11025),
            mixxx::Bpm(128));
    ASSERT_TRUE(m_pOriginal->trySetBeats(pBeats));
    m_pOriginal->setBpmLocked(true);

    mixxx::stemoriginal::importFromOriginal(*m_pStem, *m_pOriginal);
    EXPECT_TRUE(m_pStem->isBpmLocked());
    EXPECT_DOUBLE_EQ(128, m_pStem->getBpm());
}

TEST_F(StemOriginalTest, copiesBpmWithoutABeatGrid) {
    ASSERT_TRUE(m_pOriginal->trySetBpm(mixxx::Bpm(96)));

    const auto result = mixxx::stemoriginal::importFromOriginal(*m_pStem, *m_pOriginal);
    EXPECT_TRUE(result.bpmCopied);
    EXPECT_DOUBLE_EQ(96, m_pStem->getBpm());
}

TEST_F(StemOriginalTest, copiesDescriptiveMetadata) {
    m_pOriginal->setTitle(QStringLiteral("Quedate Luna"));
    m_pOriginal->setArtist(QStringLiteral("Trinix, Natalia Doco"));
    m_pOriginal->setAlbum(QStringLiteral("Some Album"));
    m_pOriginal->setAlbumArtist(QStringLiteral("Trinix"));
    m_pOriginal->updateGenre(QStringLiteral("Zouk"));
    m_pOriginal->setComposer(QStringLiteral("Composer"));
    // Andy's DJ tag codes live in grouping and comment
    m_pOriginal->setGrouping(QStringLiteral("32122"));
    m_pOriginal->setComment(QStringLiteral("123\xF0\x9F\xA6\x9E""energy"));
    m_pOriginal->setYear(QStringLiteral("2024"));
    m_pOriginal->setTrackNumber(QStringLiteral("3"));
    m_pOriginal->setTrackTotal(QStringLiteral("12"));
    m_pOriginal->setColor(mixxx::RgbColor::optional(0x00FF00));
    m_pOriginal->setRating(4);

    m_pStem->setGrouping(QStringLiteral("stale"));
    m_pStem->setRating(1);
    // Not kStemLocation: Track normalizes the path, which on Windows prepends
    // the drive letter of the working directory.
    const QString stemLocation = m_pStem->getLocation();

    mixxx::stemoriginal::importFromOriginal(*m_pStem, *m_pOriginal);

    EXPECT_EQ(QStringLiteral("Quedate Luna"), m_pStem->getTitle());
    EXPECT_EQ(QStringLiteral("Trinix, Natalia Doco"), m_pStem->getArtist());
    EXPECT_EQ(QStringLiteral("Some Album"), m_pStem->getAlbum());
    EXPECT_EQ(QStringLiteral("Trinix"), m_pStem->getAlbumArtist());
    EXPECT_EQ(QStringLiteral("Zouk"), m_pStem->getGenre());
    EXPECT_EQ(QStringLiteral("Composer"), m_pStem->getComposer());
    EXPECT_EQ(QStringLiteral("32122"), m_pStem->getGrouping());
    EXPECT_EQ(QStringLiteral("123\xF0\x9F\xA6\x9E""energy"), m_pStem->getComment());
    EXPECT_EQ(QStringLiteral("2024"), m_pStem->getYear());
    EXPECT_EQ(QStringLiteral("3"), m_pStem->getTrackNumber());
    EXPECT_EQ(QStringLiteral("12"), m_pStem->getTrackTotal());
    EXPECT_EQ(mixxx::RgbColor::optional(0x00FF00), m_pStem->getColor());
    EXPECT_EQ(4, m_pStem->getRating());

    // The stem file keeps its own location
    EXPECT_EQ(stemLocation, m_pStem->getLocation());
}

} // anonymous namespace

/// Exercises the actual library lookup against a real (temporary) database.
class StemOriginalDbTest : public LibraryTest {
  protected:
    struct LibraryEntry {
        TrackId trackId;
        TrackPointer pTrack;
    };

    /// Inserts a library row for a track. `subDir` and `fileName` are relative
    /// to the temp dir; the files do not need to exist. Written straight to
    /// the database because the lookup under test is a plain SQL query and
    /// TrackCollection::addTrack() is not accessible from here.
    LibraryEntry addTrack(const QString& subDir,
            const QString& fileName,
            const QString& artist = QString(),
            const QString& title = QString(),
            bool withBeats = false) {
        const QString directory = QDir::tempPath() + QChar('/') + subDir;
        const QString location = directory + QChar('/') + fileName;

        QSqlQuery locationQuery(dbConnection());
        locationQuery.prepare(QStringLiteral(
                "INSERT INTO track_locations "
                "(location, filename, directory, filesize, fs_deleted, "
                "needs_verification) "
                "VALUES (:location, :filename, :directory, 1, 0, 0)"));
        locationQuery.bindValue(":location", location);
        locationQuery.bindValue(":filename", fileName);
        locationQuery.bindValue(":directory", directory);
        EXPECT_TRUE(locationQuery.exec()) << locationQuery.lastError().text().toStdString();
        const QVariant locationId = locationQuery.lastInsertId();

        QSqlQuery libraryQuery(dbConnection());
        libraryQuery.prepare(QStringLiteral(
                "INSERT INTO library (artist, title, location, duration, "
                "mixxx_deleted, beats) "
                "VALUES (:artist, :title, :location, 180, 0, :beats)"));
        libraryQuery.bindValue(":artist", artist);
        libraryQuery.bindValue(":title", title);
        libraryQuery.bindValue(":location", locationId);
        libraryQuery.bindValue(":beats",
                withBeats ? QVariant(QByteArray("beatgrid")) : QVariant());
        EXPECT_TRUE(libraryQuery.exec()) << libraryQuery.lastError().text().toStdString();

        TrackPointer pTrack = Track::newTemporary(location);
        pTrack->setArtist(artist);
        pTrack->setTitle(title);
        return LibraryEntry{TrackId(libraryQuery.lastInsertId()), pTrack};
    }

    TrackId findOriginalOf(const Track& stemTrack) {
        return mixxx::stemoriginal::findOriginalTrackId(dbConnection(), stemTrack);
    }
};

TEST_F(StemOriginalDbTest, findsTheOriginalByFileBaseName) {
    const LibraryEntry original = addTrack(
            QStringLiteral("library"), QStringLiteral("Artist - Title.mp3"));
    // A decoy with a different name
    addTrack(QStringLiteral("library"), QStringLiteral("Artist - Other.mp3"));
    const LibraryEntry stem = addTrack(
            QStringLiteral("stems"), QStringLiteral("Artist - Title.stem.m4a"));

    EXPECT_EQ(original.trackId, findOriginalOf(*stem.pTrack));
}

TEST_F(StemOriginalDbTest, neverReturnsAnotherStemFile) {
    // Two stem files of the same track, no ordinary file anywhere
    const LibraryEntry stem = addTrack(
            QStringLiteral("stems"), QStringLiteral("Artist - Title.stem.m4a"));
    addTrack(QStringLiteral("stems2"), QStringLiteral("Artist - Title.stem.mp4"));

    EXPECT_FALSE(findOriginalOf(*stem.pTrack).isValid());
}

TEST_F(StemOriginalDbTest, prefersTheAnalyzedCandidate) {
    // Andy keeps backup copies; the one he works with is the analyzed one.
    // The un-analyzed copy is added first so it has the lower track id.
    addTrack(QStringLiteral("backup"), QStringLiteral("Artist - Title.mp3"));
    const LibraryEntry analyzed = addTrack(QStringLiteral("library"),
            QStringLiteral("Artist - Title.flac"),
            QString(),
            QString(),
            true);
    const LibraryEntry stem = addTrack(
            QStringLiteral("stems"), QStringLiteral("Artist - Title.stem.m4a"));

    EXPECT_EQ(analyzed.trackId, findOriginalOf(*stem.pTrack));
}

TEST_F(StemOriginalDbTest, fallsBackToArtistAndTitle) {
    // The original was renamed after the stems had been extracted
    const LibraryEntry original = addTrack(QStringLiteral("library"),
            QStringLiteral("01 - renamed.mp3"),
            QStringLiteral("Ayra Starr"),
            QStringLiteral("Commas"));
    const LibraryEntry stem = addTrack(QStringLiteral("stems"),
            QStringLiteral("Ayra Starr - Commas.stem.m4a"),
            QStringLiteral("Ayra Starr"),
            QStringLiteral("Commas"));

    EXPECT_EQ(original.trackId, findOriginalOf(*stem.pTrack));
}

TEST_F(StemOriginalDbTest, returnsNothingForANonStemTrack) {
    addTrack(QStringLiteral("library"), QStringLiteral("Artist - Title.mp3"));
    const LibraryEntry regular = addTrack(
            QStringLiteral("other"), QStringLiteral("Artist - Title.flac"));

    EXPECT_FALSE(findOriginalOf(*regular.pTrack).isValid());
}

TEST_F(StemOriginalDbTest, findsTheStemOfARegularTrack) {
    const LibraryEntry regular = addTrack(
            QStringLiteral("library"), QStringLiteral("Artist - Title.mp3"));
    const LibraryEntry stem = addTrack(
            QStringLiteral("stems"), QStringLiteral("Artist - Title.stem.m4a"));
    // A stem file of a different track must not be picked
    addTrack(QStringLiteral("stems"), QStringLiteral("Artist - Other.stem.m4a"));

    EXPECT_EQ(stem.trackId,
            mixxx::stemoriginal::findCounterpartTrackId(dbConnection(),
                    *regular.pTrack,
                    mixxx::stemoriginal::Counterpart::Stem));
}

TEST_F(StemOriginalDbTest, findsTheStemByArtistAndTitle) {
    const LibraryEntry regular = addTrack(QStringLiteral("library"),
            QStringLiteral("01 - renamed.mp3"),
            QStringLiteral("Ayra Starr"),
            QStringLiteral("Commas"));
    const LibraryEntry stem = addTrack(QStringLiteral("stems"),
            QStringLiteral("Ayra Starr - Commas.stem.m4a"),
            QStringLiteral("Ayra Starr"),
            QStringLiteral("Commas"));

    EXPECT_EQ(stem.trackId,
            mixxx::stemoriginal::findCounterpartTrackId(dbConnection(),
                    *regular.pTrack,
                    mixxx::stemoriginal::Counterpart::Stem));
}

TEST_F(StemOriginalDbTest, aTrackIsNeverItsOwnCounterpart) {
    // Asking a stem file for a stem file (or a regular file for a regular
    // file) must come up empty instead of returning a same-named sibling.
    const LibraryEntry stem = addTrack(
            QStringLiteral("stems"), QStringLiteral("Artist - Title.stem.m4a"));
    const LibraryEntry regular = addTrack(
            QStringLiteral("library"), QStringLiteral("Artist - Title.mp3"));

    EXPECT_FALSE(mixxx::stemoriginal::findCounterpartTrackId(dbConnection(),
            *stem.pTrack,
            mixxx::stemoriginal::Counterpart::Stem)
                         .isValid());
    EXPECT_FALSE(mixxx::stemoriginal::findCounterpartTrackId(dbConnection(),
            *regular.pTrack,
            mixxx::stemoriginal::Counterpart::Original)
                         .isValid());
}

TEST_F(StemOriginalDbTest, returnsNothingWhenTheStemIsMissing) {
    const LibraryEntry regular = addTrack(QStringLiteral("library"),
            QStringLiteral("Nobody - Nothing.mp3"),
            QStringLiteral("Nobody"),
            QStringLiteral("Nothing"));

    EXPECT_FALSE(mixxx::stemoriginal::findCounterpartTrackId(dbConnection(),
            *regular.pTrack,
            mixxx::stemoriginal::Counterpart::Stem)
                         .isValid());
}

TEST_F(StemOriginalDbTest, returnsNothingWhenTheOriginalIsMissing) {
    const LibraryEntry stem = addTrack(QStringLiteral("stems"),
            QStringLiteral("Nobody - Nothing.stem.m4a"),
            QStringLiteral("Nobody"),
            QStringLiteral("Nothing"));

    EXPECT_FALSE(findOriginalOf(*stem.pTrack).isValid());
}
