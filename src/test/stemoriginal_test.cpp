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

// AnalyzerBeats::shouldAnalyze() re-analyzes any grid whose sub version is
// empty and whose first beat sits at frame 0 - that is its "this came from the
// metadata BPM tag" heuristic. Most of Andy's originals carry exactly that
// grid, so an import that passes it straight through gives the stem track a
// re-analysis on every single load.
TEST_F(StemOriginalTest, theImportedGridIsNotReanalyzedOnEveryLoad) {
    const auto pBeats = mixxx::Beats::fromConstTempo(
            mixxx::audio::SampleRate(44100),
            mixxx::audio::kStartFramePos,
            mixxx::Bpm(84));
    ASSERT_TRUE(pBeats->getSubVersion().isEmpty());
    ASSERT_TRUE(m_pOriginal->trySetBeats(pBeats));

    mixxx::stemoriginal::importFromOriginal(*m_pStem, *m_pOriginal);

    const mixxx::BeatsPointer pStemBeats = m_pStem->getBeats();
    ASSERT_NE(nullptr, pStemBeats);
    EXPECT_FALSE(pStemBeats->getSubVersion().isEmpty());
    EXPECT_DOUBLE_EQ(84, m_pStem->getBpm());
}

// Same trap on the path that only has a tempo number to go on. A constant grid
// always has a beat within the first beat length of the start, so the first
// beat cannot be kept off frame 0 - the sub version is what defuses the
// heuristic.
TEST_F(StemOriginalTest, theCopiedBpmAloneIsNotReanalyzedOnEveryLoad) {
    ASSERT_TRUE(m_pOriginal->trySetBpm(mixxx::Bpm(96)));

    mixxx::stemoriginal::importFromOriginal(*m_pStem, *m_pOriginal);

    const mixxx::BeatsPointer pStemBeats = m_pStem->getBeats();
    ASSERT_NE(nullptr, pStemBeats);
    EXPECT_FALSE(pStemBeats->getSubVersion().isEmpty());
    EXPECT_DOUBLE_EQ(96, m_pStem->getBpm());
}

// --- Codec delay compensation -------------------------------------------
//
// A stem file is decoded from its original and re-encoded, which puts a codec
// delay of some tens of milliseconds in front of the audio (measured at 95 and
// 99 ms across Andy's library). Every cue of the original is that much too
// early for the stem file. The stem's own analyzed beat grid carries the same
// delay, so the offset between the two grids is the correction.

namespace {

// 94.9 ms at 44.1 kHz, i.e. the offset measured on Andy's stem files
constexpr double kDelayFrames44k = 4186;

mixxx::BeatsPointer constTempoBeats(int sampleRate, double firstBeatFrame, double bpm) {
    return mixxx::Beats::fromConstTempo(
            mixxx::audio::SampleRate(sampleRate),
            mixxx::audio::FramePos(firstBeatFrame),
            mixxx::Bpm(bpm));
}

} // anonymous namespace

TEST_F(StemOriginalTest, alignsCuePositionsToTheStemsOwnBeatGrid) {
    // 120 BPM at 44.1 kHz: one beat is 22050 frames
    ASSERT_TRUE(m_pOriginal->trySetBeats(constTempoBeats(44100, 0, 120)));
    ASSERT_TRUE(m_pStem->trySetBeats(constTempoBeats(44100, kDelayFrames44k, 120)));

    // A hot cue right on the fifth beat of the original
    m_pOriginal->createAndAddCue(mixxx::CueType::HotCue,
            1,
            mixxx::audio::FramePos(4 * 22050),
            mixxx::audio::FramePos());

    const auto result = mixxx::stemoriginal::importFromOriginal(*m_pStem, *m_pOriginal);
    EXPECT_TRUE(result.alignedToTargetGrid);
    EXPECT_FALSE(result.alignmentUnavailable);
    EXPECT_NEAR(94.9, result.alignmentShiftMillis, 0.1);
    // The stem's own grid is the reference and must survive the import
    EXPECT_FALSE(result.beatsCopied);

    const CuePointer pHotcue = m_pStem->findHotcueByIndex(1);
    ASSERT_NE(nullptr, pHotcue);
    EXPECT_NEAR(4 * 22050 + kDelayFrames44k, pHotcue->getPosition().value(), 2);

    const mixxx::BeatsPointer pStemBeats = m_pStem->getBeats();
    ASSERT_NE(nullptr, pStemBeats);
    EXPECT_DOUBLE_EQ(kDelayFrames44k, pStemBeats->firstBeat().value());
}

TEST_F(StemOriginalTest, keepsTheDistanceOfCuesThatAreNotOnABeat) {
    ASSERT_TRUE(m_pOriginal->trySetBeats(constTempoBeats(44100, 0, 120)));
    ASSERT_TRUE(m_pStem->trySetBeats(constTempoBeats(44100, kDelayFrames44k, 120)));

    // Deliberately 1000 frames behind the fifth beat
    m_pOriginal->createAndAddCue(mixxx::CueType::HotCue,
            2,
            mixxx::audio::FramePos(4 * 22050 + 1000),
            mixxx::audio::FramePos());

    mixxx::stemoriginal::importFromOriginal(*m_pStem, *m_pOriginal);

    const CuePointer pHotcue = m_pStem->findHotcueByIndex(2);
    ASSERT_NE(nullptr, pHotcue);
    // Shifted by the codec delay, not snapped onto the beat
    EXPECT_NEAR(4 * 22050 + 1000 + kDelayFrames44k, pHotcue->getPosition().value(), 2);
}

TEST_F(StemOriginalTest, movesBothEndsOfASavedLoop) {
    ASSERT_TRUE(m_pOriginal->trySetBeats(constTempoBeats(44100, 0, 120)));
    ASSERT_TRUE(m_pStem->trySetBeats(constTempoBeats(44100, kDelayFrames44k, 120)));

    m_pOriginal->createAndAddCue(mixxx::CueType::Loop,
            0,
            mixxx::audio::FramePos(4 * 22050),
            mixxx::audio::FramePos(8 * 22050));

    mixxx::stemoriginal::importFromOriginal(*m_pStem, *m_pOriginal);

    const CuePointer pLoop = m_pStem->findHotcueByIndex(0);
    ASSERT_NE(nullptr, pLoop);
    EXPECT_NEAR(4 * 22050 + kDelayFrames44k, pLoop->getPosition().value(), 2);
    EXPECT_NEAR(8 * 22050 + kDelayFrames44k, pLoop->getEndPosition().value(), 2);
}

TEST_F(StemOriginalTest, alignsWhenTheStemWasAnalyzedAtDoubleTempo) {
    // The analyzer regularly picks the double or half tempo for a stem file.
    // The grids still describe the same beats, so the offset is still valid.
    ASSERT_TRUE(m_pOriginal->trySetBeats(constTempoBeats(44100, 0, 120)));
    ASSERT_TRUE(m_pStem->trySetBeats(constTempoBeats(44100, kDelayFrames44k, 240)));

    m_pOriginal->createAndAddCue(mixxx::CueType::HotCue,
            1,
            mixxx::audio::FramePos(4 * 22050),
            mixxx::audio::FramePos());

    const auto result = mixxx::stemoriginal::importFromOriginal(*m_pStem, *m_pOriginal);
    EXPECT_TRUE(result.alignedToTargetGrid);

    const CuePointer pHotcue = m_pStem->findHotcueByIndex(1);
    ASSERT_NE(nullptr, pHotcue);
    EXPECT_NEAR(4 * 22050 + kDelayFrames44k, pHotcue->getPosition().value(), 2);
    // The tempo number follows the original, so the double-tempo reading is
    // corrected. Every beat of the original is still a beat of the stem: the
    // re-tempoed grid is anchored on a beat that both grids share, never on one
    // of the in-between beats of the double-tempo reading.
    EXPECT_DOUBLE_EQ(120, m_pStem->getBpm());
    const mixxx::BeatsPointer pStemBeats = m_pStem->getBeats();
    ASSERT_NE(nullptr, pStemBeats);
    EXPECT_NEAR(kDelayFrames44k,
            pStemBeats->findClosestBeat(mixxx::audio::FramePos(kDelayFrames44k)).value(),
            2);
}

TEST_F(StemOriginalTest, adoptsTheOriginalsHandCorrectedBpmWithoutMovingTheGrid) {
    // What Andy actually does: the analyzer read 127.98 for the stem file, he
    // corrected the original to a round 128 by hand.
    ASSERT_TRUE(m_pOriginal->trySetBeats(constTempoBeats(44100, 0, 128)));
    ASSERT_TRUE(m_pStem->trySetBeats(constTempoBeats(44100, kDelayFrames44k, 127.98)));

    const double beatFrames128 = 60.0 * 44100 / 128.0;
    m_pOriginal->createAndAddCue(mixxx::CueType::HotCue,
            1,
            mixxx::audio::FramePos(4 * beatFrames128),
            mixxx::audio::FramePos());

    const auto result = mixxx::stemoriginal::importFromOriginal(*m_pStem, *m_pOriginal);
    EXPECT_TRUE(result.alignedToTargetGrid);
    // The grid was not copied from the original, only its tempo was adopted
    EXPECT_FALSE(result.beatsCopied);
    EXPECT_TRUE(result.bpmCopied);
    EXPECT_DOUBLE_EQ(128, result.bpmAdopted);
    // Exactly the original's number, not a rounded or averaged version of it
    EXPECT_DOUBLE_EQ(128, m_pStem->getBpm());

    // The phase still belongs to the stem file: its first beat sits at the
    // codec delay, not at zero like the original's.
    const mixxx::BeatsPointer pStemBeats = m_pStem->getBeats();
    ASSERT_NE(nullptr, pStemBeats);
    EXPECT_NEAR(kDelayFrames44k,
            pStemBeats->findClosestBeat(mixxx::audio::FramePos(kDelayFrames44k)).value(),
            2);

    // And the cue correction happened as before
    const CuePointer pHotcue = m_pStem->findHotcueByIndex(1);
    ASSERT_NE(nullptr, pHotcue);
    EXPECT_NEAR(4 * beatFrames128 + kDelayFrames44k, pHotcue->getPosition().value(), 2);
}

TEST_F(StemOriginalTest, reportsAMatchingBpmAsAdoptedWithoutTouchingTheGrid) {
    ASSERT_TRUE(m_pOriginal->trySetBeats(constTempoBeats(44100, 0, 120)));
    ASSERT_TRUE(m_pStem->trySetBeats(constTempoBeats(44100, kDelayFrames44k, 120)));
    const mixxx::BeatsPointer pBeatsBefore = m_pStem->getBeats();

    const auto result = mixxx::stemoriginal::importFromOriginal(*m_pStem, *m_pOriginal);
    EXPECT_TRUE(result.bpmCopied);
    EXPECT_DOUBLE_EQ(120, result.bpmAdopted);
    // Same tempo on both sides: the grid object is not even replaced
    EXPECT_EQ(pBeatsBefore, m_pStem->getBeats());
}

TEST_F(StemOriginalTest, adoptsTheOriginalsBpmAcrossSampleRates) {
    TrackPointer pOriginal48 = newTrack(kOriginalLocation, 48000);
    ASSERT_TRUE(pOriginal48->trySetBeats(constTempoBeats(48000, 0, 128)));
    ASSERT_TRUE(m_pStem->trySetBeats(constTempoBeats(44100, kDelayFrames44k, 127.98)));

    const auto result = mixxx::stemoriginal::importFromOriginal(*m_pStem, *pOriginal48);
    EXPECT_TRUE(result.alignedToTargetGrid);
    EXPECT_DOUBLE_EQ(128, m_pStem->getBpm());
    // The new grid belongs to the stem file, so it is built at the stem's rate
    const mixxx::BeatsPointer pStemBeats = m_pStem->getBeats();
    ASSERT_NE(nullptr, pStemBeats);
    EXPECT_EQ(44100, pStemBeats->getSampleRate());
    EXPECT_NEAR(kDelayFrames44k,
            pStemBeats->findClosestBeat(mixxx::audio::FramePos(kDelayFrames44k)).value(),
            2);
}

TEST_F(StemOriginalTest, doesNotAdoptABpmOntoALockedStemTrack) {
    // The import unlocks the target, so a BPM lock must not block the tempo
    // change either - and the source's lock state is what survives.
    ASSERT_TRUE(m_pOriginal->trySetBeats(constTempoBeats(44100, 0, 128)));
    ASSERT_TRUE(m_pStem->trySetBeats(constTempoBeats(44100, kDelayFrames44k, 127.98)));
    m_pStem->setBpmLocked(true);

    const auto result = mixxx::stemoriginal::importFromOriginal(*m_pStem, *m_pOriginal);
    EXPECT_TRUE(result.bpmCopied);
    EXPECT_DOUBLE_EQ(128, m_pStem->getBpm());
    EXPECT_FALSE(m_pStem->isBpmLocked());
}

TEST_F(StemOriginalTest, alignsAcrossSampleRates) {
    // The original is a 48 kHz mp3, the stem file a 44.1 kHz m4a
    TrackPointer pOriginal48 = newTrack(kOriginalLocation, 48000);
    ASSERT_TRUE(pOriginal48->trySetBeats(constTempoBeats(48000, 0, 120)));
    ASSERT_TRUE(m_pStem->trySetBeats(constTempoBeats(44100, kDelayFrames44k, 120)));

    // 2.0 s = the fifth beat at 120 BPM
    pOriginal48->createAndAddCue(mixxx::CueType::HotCue,
            1,
            mixxx::audio::FramePos(96000),
            mixxx::audio::FramePos());

    const auto result = mixxx::stemoriginal::importFromOriginal(*m_pStem, *pOriginal48);
    EXPECT_TRUE(result.alignedToTargetGrid);

    const CuePointer pHotcue = m_pStem->findHotcueByIndex(1);
    ASSERT_NE(nullptr, pHotcue);
    EXPECT_NEAR(2.0 * 44100 + kDelayFrames44k, pHotcue->getPosition().value(), 2);
}

TEST_F(StemOriginalTest, fallsBackToCopyingTheGridWhenTheStemHasNone) {
    ASSERT_TRUE(m_pOriginal->trySetBeats(constTempoBeats(44100, 0, 120)));
    m_pOriginal->createAndAddCue(mixxx::CueType::HotCue,
            1,
            mixxx::audio::FramePos(4 * 22050),
            mixxx::audio::FramePos());

    const auto result = mixxx::stemoriginal::importFromOriginal(*m_pStem, *m_pOriginal);
    EXPECT_FALSE(result.alignedToTargetGrid);
    EXPECT_TRUE(result.alignmentUnavailable);
    EXPECT_TRUE(result.beatsCopied);

    // Nothing is known about the delay, so the cue is copied as it is
    const CuePointer pHotcue = m_pStem->findHotcueByIndex(1);
    ASSERT_NE(nullptr, pHotcue);
    EXPECT_NEAR(4 * 22050, pHotcue->getPosition().value(), 2);
}

TEST_F(StemOriginalTest, doesNotAlignAgainstAnUnrelatedTempo) {
    // 120 vs 100 BPM is not a half/double confusion, so the stem's grid does
    // not describe the same beats and must not be trusted.
    ASSERT_TRUE(m_pOriginal->trySetBeats(constTempoBeats(44100, 0, 120)));
    ASSERT_TRUE(m_pStem->trySetBeats(constTempoBeats(44100, kDelayFrames44k, 100)));

    m_pOriginal->createAndAddCue(mixxx::CueType::HotCue,
            1,
            mixxx::audio::FramePos(4 * 22050),
            mixxx::audio::FramePos());

    const auto result = mixxx::stemoriginal::importFromOriginal(*m_pStem, *m_pOriginal);
    EXPECT_FALSE(result.alignedToTargetGrid);
    EXPECT_TRUE(result.beatsCopied);
    EXPECT_DOUBLE_EQ(120, m_pStem->getBpm());
    const CuePointer pHotcue = m_pStem->findHotcueByIndex(1);
    ASSERT_NE(nullptr, pHotcue);
    EXPECT_NEAR(4 * 22050, pHotcue->getPosition().value(), 2);
}

TEST_F(StemOriginalTest, beatPeriodIsTheDistanceBetweenTwoBeats) {
    const auto pBeats = constTempoBeats(44100, 0, 120);
    EXPECT_NEAR(0.5, mixxx::stemoriginal::beatPeriodSecondsAt(*pBeats, 10.0), 1e-6);
    // Also on a position that is exactly a beat
    EXPECT_NEAR(0.5, mixxx::stemoriginal::beatPeriodSecondsAt(*pBeats, 0.0), 1e-6);
}

TEST_F(StemOriginalTest, tempoCompatibilityAcceptsWholeMultiplesOnly) {
    const auto pBase = constTempoBeats(44100, 0, 120);
    EXPECT_TRUE(mixxx::stemoriginal::tempoIsCompatible(
            *pBase, *constTempoBeats(44100, 0, 120), 30.0));
    EXPECT_TRUE(mixxx::stemoriginal::tempoIsCompatible(
            *pBase, *constTempoBeats(44100, 0, 240), 30.0));
    EXPECT_TRUE(mixxx::stemoriginal::tempoIsCompatible(
            *pBase, *constTempoBeats(44100, 0, 60), 30.0));
    // Analyzer noise on the same tempo
    EXPECT_TRUE(mixxx::stemoriginal::tempoIsCompatible(
            *pBase, *constTempoBeats(44100, 0, 120.4), 30.0));
    EXPECT_FALSE(mixxx::stemoriginal::tempoIsCompatible(
            *pBase, *constTempoBeats(44100, 0, 100), 30.0));
    EXPECT_FALSE(mixxx::stemoriginal::tempoIsCompatible(
            *pBase, *constTempoBeats(44100, 0, 180), 30.0));
}

TEST_F(StemOriginalTest, gridOffsetIsNeverMoreThanHalfABeat) {
    const auto pSource = constTempoBeats(44100, 0, 120);
    // Grid B sits a whole beat plus the codec delay behind grid A, which is
    // indistinguishable from the delay alone: both grids mark the same beats.
    const auto pTarget = constTempoBeats(44100, 22050 + kDelayFrames44k, 120);
    const auto offset = mixxx::stemoriginal::beatGridTimeOffsetSeconds(
            *pSource, *pTarget, 30.0);
    ASSERT_TRUE(offset.has_value());
    EXPECT_NEAR(kDelayFrames44k / 44100.0, *offset, 1e-6);
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

// The stem extractor only ever pads silence at the front, so the correction a
// codec delay produces is always a small positive shift. Anything negative, or
// far past the usual 50-100 ms, means the grids were matched on the wrong beat
// - Andy asked to be warned about exactly that (2026-08-07).
TEST_F(StemOriginalTest, implausibleAlignmentShiftFlagsNegativeAndOversizedOffsets) {
    using mixxx::stemoriginal::isImplausibleAlignmentShift;

    // The normal band Andy sees on a healthy stem/original pair.
    EXPECT_FALSE(isImplausibleAlignmentShift(50.0));
    EXPECT_FALSE(isImplausibleAlignmentShift(100.0));
    EXPECT_FALSE(isImplausibleAlignmentShift(0.0));
    EXPECT_FALSE(isImplausibleAlignmentShift(
            mixxx::stemoriginal::kMaxPlausibleShiftMillis));

    // Negative: the stem's audio cannot start earlier than the original's.
    EXPECT_TRUE(isImplausibleAlignmentShift(-0.5));
    EXPECT_TRUE(isImplausibleAlignmentShift(-60.0));

    // Too large to be a codec delay.
    EXPECT_TRUE(isImplausibleAlignmentShift(
            mixxx::stemoriginal::kMaxPlausibleShiftMillis + 0.5));
    EXPECT_TRUE(isImplausibleAlignmentShift(250.0));
}
