#include "engine/beatclick.h"

#include <gtest/gtest.h>

#include <vector>

#include "engine/effects/groupfeaturestate.h"
#include "engine/engine.h"
#include "util/types.h"

namespace {

constexpr std::size_t kFramesPerBuffer = 1024;
constexpr std::size_t kBufferSize = kFramesPerBuffer * mixxx::kEngineChannelOutputCount;
const auto kSampleRate = mixxx::audio::SampleRate(44100);
// 120 BPM
constexpr double kBeatSeconds = 0.5;

GroupFeatureState beatState(double fractionAtBufferEnd, double scratchRate = 1.0) {
    GroupFeatureState features;
    features.beat_length = GroupFeatureBeatLength{kBeatSeconds, scratchRate};
    features.beat_fraction_buffer_end = fractionAtBufferEnd;
    return features;
}

/// Index of the first frame that is not silent, or -1 if the buffer is silent.
int firstLoudFrame(const std::vector<CSAMPLE>& buffer) {
    for (std::size_t i = 0; i < buffer.size(); i += mixxx::kEngineChannelOutputCount) {
        if (buffer[i] != 0.0f) {
            return static_cast<int>(i / mixxx::kEngineChannelOutputCount);
        }
    }
    return -1;
}

class BeatClickTest : public testing::Test {
  protected:
    void process(const GroupFeatureState& features, bool withSecondary = false) {
        m_buffer.assign(kBufferSize, 0.0f);
        m_secondary.assign(kBufferSize, 0.0f);
        m_beatClick.process(m_buffer.data(),
                withSecondary ? m_secondary.data() : nullptr,
                kBufferSize,
                kSampleRate,
                features,
                1.0f);
    }

    BeatClick m_beatClick;
    std::vector<CSAMPLE> m_buffer;
    std::vector<CSAMPLE> m_secondary;
};

TEST_F(BeatClickTest, ClicksOnTheBeatThatFellIntoThisBuffer) {
    // The beat is 22 frames (0.001 * 0.5 s * 44100) before the end of the buffer.
    process(beatState(0.001));
    EXPECT_EQ(static_cast<int>(kFramesPerBuffer) - 22, firstLoudFrame(m_buffer));
}

TEST_F(BeatClickTest, StaysSilentWhenNoBeatIsInThisBuffer) {
    // Half a beat back is far outside a 1024 frame buffer.
    process(beatState(0.5));
    EXPECT_EQ(-1, firstLoudFrame(m_buffer));
}

TEST_F(BeatClickTest, StaysSilentWhileStopped) {
    process(beatState(0.001, /*scratchRate*/ 0.0));
    EXPECT_EQ(-1, firstLoudFrame(m_buffer));
}

TEST_F(BeatClickTest, StaysSilentWithoutBeatgrid) {
    process(GroupFeatureState{});
    EXPECT_EQ(-1, firstLoudFrame(m_buffer));
}

TEST_F(BeatClickTest, ContinuesAClickAcrossBufferBoundaries) {
    process(beatState(0.001));
    ASSERT_NE(-1, firstLoudFrame(m_buffer));
    // No beat in the next buffer, but the click is only 22 frames in.
    process(beatState(0.5));
    EXPECT_EQ(0, firstLoudFrame(m_buffer));
}

TEST_F(BeatClickTest, ResetDropsTheClickInFlight) {
    process(beatState(0.001));
    m_beatClick.reset();
    process(beatState(0.5));
    EXPECT_EQ(-1, firstLoudFrame(m_buffer));
}

TEST_F(BeatClickTest, FeedsTheSecondaryBufferIdentically) {
    process(beatState(0.001), /*withSecondary*/ true);
    EXPECT_EQ(m_buffer, m_secondary);
    EXPECT_NE(-1, firstLoudFrame(m_secondary));
}

TEST_F(BeatClickTest, ClicksAtEveryGainWithoutTouchingTheRestOfTheBuffer) {
    m_buffer.assign(kBufferSize, 0.0f);
    m_beatClick.process(m_buffer.data(),
            nullptr,
            kBufferSize,
            kSampleRate,
            beatState(0.001),
            0.5f);
    const int loud = firstLoudFrame(m_buffer);
    ASSERT_NE(-1, loud);
    // Everything before the beat is untouched.
    for (int i = 0; i < loud * static_cast<int>(mixxx::kEngineChannelOutputCount); ++i) {
        ASSERT_EQ(0.0f, m_buffer[i]) << "sample " << i;
    }
}

} // namespace
