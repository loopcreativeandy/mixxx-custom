#include <gtest/gtest.h>

#include <QList>

#include "analyzer/analyzertrack.h"
#include "analyzer/analyzerwaveform.h"
#include "library/dao/analysisdao.h"
#include "proto/waveform.pb.h"
#include "sources/soundsourceproxy.h"
#include "test/librarytest.h"
#include "track/track.h"
#include "waveform/waveform.h"
#include "waveform/waveformfactory.h"

#ifdef __STEM__

// andy-custom: a stem track's waveform was computed again on every load
// (Andy, 2026-09-23). A track that had an old stored waveform without per-stem
// band data kept that row; the freshly analyzed one was saved as a second row,
// and on the next load upstream kept the first usable row and deleted the
// rest - the new one. Found in his library: 35 stem tracks with two
// Waveform-6.1 rows each.

namespace {

const QString kStemFile = QStringLiteral("stems/sin_ALAC_24bit.stem.mp4");
constexpr int kStemCount = 4;
constexpr SINT kFrames = 44100;
const auto kSampleRate = mixxx::audio::SampleRate(44100);
const auto kStemChannels = mixxx::audio::ChannelCount(kStemCount * 2);

class StoredStemWaveformTest : public LibraryTest {
  protected:
    void SetUp() override {
        m_pTrack = getOrAddTrackByLocation(getTestDir().filePath(kStemFile));
        ASSERT_TRUE(m_pTrack);
        ASSERT_TRUE(m_pTrack->getId().isValid());
        // Opening the file is what imports the stem info.
        mixxx::AudioSource::OpenParams params;
        params.setChannelCount(mixxx::audio::ChannelCount::stereo());
        ASSERT_NE(SoundSourceProxy(m_pTrack).openAudioSource(params), nullptr);
        ASSERT_EQ(m_pTrack->getStemInfo().size(), kStemCount);
        m_analysisDao.initialize(dbConnection());
    }

    /// Stores a waveform row (+ summary row) the way a build does. Without
    /// bands = what upstream 2.6 or a pre-RGB-stem build wrote.
    int storeWaveform(bool withBands) {
        Waveform waveform(kSampleRate, kFrames, 441, -1, kStemCount);
        QByteArray data = waveform.toByteArray();
        if (!withBands) {
            mixxx::track::io::Waveform proto;
            EXPECT_TRUE(proto.ParseFromArray(data.constData(), data.size()));
            proto.clear_signal_stems_filtered();
            const std::string bytes = proto.SerializeAsString();
            data = QByteArray(bytes.data(), static_cast<int>(bytes.size()));
            EXPECT_FALSE(Waveform(data).hasStemBands());
        }
        AnalysisDao::AnalysisInfo info;
        info.trackId = m_pTrack->getId();
        info.type = AnalysisDao::TYPE_WAVEFORM;
        info.version = WaveformFactory::currentWaveformVersion();
        info.description = WaveformFactory::currentWaveformDescription();
        info.data = data;
        EXPECT_TRUE(m_analysisDao.saveAnalysis(&info));

        Waveform summary(kSampleRate, kFrames, 441, 2 * 1920, kStemCount);
        AnalysisDao::AnalysisInfo summaryInfo;
        summaryInfo.trackId = m_pTrack->getId();
        summaryInfo.type = AnalysisDao::TYPE_WAVESUMMARY;
        summaryInfo.version = WaveformFactory::currentWaveformSummaryVersion();
        summaryInfo.description = WaveformFactory::currentWaveformSummaryDescription();
        summaryInfo.data = summary.toByteArray();
        EXPECT_TRUE(m_analysisDao.saveAnalysis(&summaryInfo));
        return info.analysisId;
    }

    QList<AnalysisDao::AnalysisInfo> storedWaveforms() {
        return m_analysisDao.getAnalysesForTrackByType(
                m_pTrack->getId(), AnalysisDao::TYPE_WAVEFORM);
    }

    QList<AnalysisDao::AnalysisInfo> storedSummaries() {
        return m_analysisDao.getAnalysesForTrackByType(
                m_pTrack->getId(), AnalysisDao::TYPE_WAVESUMMARY);
    }

    /// Like loading the track into a deck: forget the in-memory waveform so
    /// the analyzer has to look at what is stored.
    bool analyzerWantsToAnalyze() {
        m_pTrack->setWaveform(ConstWaveformPointer());
        m_pTrack->setWaveformSummary(ConstWaveformPointer());
        AnalyzerWaveform analyzer(config(), dbConnection());
        const bool analyze = analyzer.initialize(
                AnalyzerTrack(m_pTrack), kSampleRate, kStemChannels, kFrames);
        if (analyze) {
            analyzer.cleanup();
        }
        return analyze;
    }

    /// A complete analysis run, as the analyzer thread does it.
    void analyze() {
        m_pTrack->setWaveform(ConstWaveformPointer());
        m_pTrack->setWaveformSummary(ConstWaveformPointer());
        AnalyzerWaveform analyzer(config(), dbConnection());
        ASSERT_TRUE(analyzer.initialize(
                AnalyzerTrack(m_pTrack), kSampleRate, kStemChannels, kFrames));
        std::vector<CSAMPLE> buffer(1024 * kStemChannels, 0.25f);
        for (SINT done = 0; done < kFrames; done += 1024) {
            analyzer.processSamples(buffer.data(), static_cast<SINT>(buffer.size()));
        }
        analyzer.storeResults(m_pTrack);
        analyzer.cleanup();
    }

    TrackPointer m_pTrack;
    AnalysisDao m_analysisDao{config()};
};

TEST_F(StoredStemWaveformTest, prefersTheRowWithBandsOverAnOlderOneWithout) {
    storeWaveform(false);
    const int bandedId = storeWaveform(true);
    ASSERT_EQ(storedWaveforms().size(), 2);

    // Upstream logic: loads the old row, deletes the banded one, analyzes.
    EXPECT_FALSE(analyzerWantsToAnalyze());
    const auto rows = storedWaveforms();
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows.first().analysisId, bandedId);
}

TEST_F(StoredStemWaveformTest, reanalysisReplacesTheOldRowAndIsNotRepeated) {
    const int oldId = storeWaveform(false);
    ASSERT_TRUE(analyzerWantsToAnalyze()); // no bands -> analyze once

    analyze();
    const auto rows = storedWaveforms();
    ASSERT_EQ(rows.size(), 1);
    EXPECT_NE(rows.first().analysisId, oldId);
    EXPECT_EQ(storedSummaries().size(), 1);

    // The load after that, and every one after it: nothing to do.
    EXPECT_FALSE(analyzerWantsToAnalyze());
    EXPECT_FALSE(analyzerWantsToAnalyze());
    EXPECT_EQ(storedWaveforms().size(), 1);
}

TEST_F(StoredStemWaveformTest, isBetterStoredWaveform) {
    Waveform banded(kSampleRate, kFrames, 441, -1, kStemCount);
    Waveform plain(kSampleRate, kFrames, 441, -1, 0);
    ASSERT_TRUE(banded.hasStemBands());
    ASSERT_FALSE(plain.hasStemBands());
    EXPECT_TRUE(AnalyzerWaveform::isBetterStoredWaveform(banded, plain, true));
    EXPECT_FALSE(AnalyzerWaveform::isBetterStoredWaveform(plain, banded, true));
    // Equal, or not a stem track: the first one stays (upstream behaviour).
    EXPECT_FALSE(AnalyzerWaveform::isBetterStoredWaveform(banded, banded, true));
    EXPECT_FALSE(AnalyzerWaveform::isBetterStoredWaveform(banded, plain, false));
}

} // namespace

#endif // __STEM__
