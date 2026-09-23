#include "library/stemswap.h"

#include <gtest/gtest.h>

#include <limits>

namespace {

using mixxx::stemswap::Direction;
using mixxx::stemswap::signedOffsetSeconds;
using mixxx::stemswap::transferEngineSamplePosition;
using mixxx::stemswap::transferFramePosition;

/// The codec delay measured on Andy's library with Mixxx's own decoders
/// (CP93): ~72 ms for 48 kHz sources.
constexpr double kTypicalDelay = 0.07189;
constexpr double k48k = 48000.0;
constexpr double k44k = 44100.0;

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();

TEST(StemSwapTest, OffsetSignFollowsTheDirection) {
    EXPECT_DOUBLE_EQ(kTypicalDelay, signedOffsetSeconds(kTypicalDelay, Direction::OriginalToStem));
    EXPECT_DOUBLE_EQ(-kTypicalDelay, signedOffsetSeconds(kTypicalDelay, Direction::StemToOriginal));
}

/// A failed or absurd measurement must degrade to "no correction", never to a
/// wild seek. Regression guard for the -ffast-math trap: std::isfinite() is
/// folded to `true` in this build, so a naive check lets these through.
TEST(StemSwapTest, UnusableOffsetBecomesZero) {
    EXPECT_DOUBLE_EQ(0.0, signedOffsetSeconds(kNaN, Direction::OriginalToStem));
    EXPECT_DOUBLE_EQ(0.0, signedOffsetSeconds(kInf, Direction::OriginalToStem));
    EXPECT_DOUBLE_EQ(0.0, signedOffsetSeconds(-kInf, Direction::StemToOriginal));
    EXPECT_DOUBLE_EQ(0.0, signedOffsetSeconds(5.0, Direction::OriginalToStem));
}

TEST(StemSwapTest, SameRateAddsTheOffsetInFrames) {
    const double frame = 60.0 * k48k;
    EXPECT_DOUBLE_EQ((60.0 + kTypicalDelay) * k48k,
            transferFramePosition(frame, k48k, k48k, kTypicalDelay));
}

/// Half his library is 48 kHz mp3 with 44.1 kHz stems. Upstream Clone Deck
/// copies raw frames, which would put the stem ~9 % too early - at one minute
/// in, over five seconds off.
TEST(StemSwapTest, DifferentRatesGoThroughSeconds) {
    const double sourceFrame = 60.0 * k48k;
    EXPECT_DOUBLE_EQ((60.0 + kTypicalDelay) * k44k,
            transferFramePosition(sourceFrame, k48k, k44k, kTypicalDelay));
}

/// Swapping there and back must land on the starting frame, otherwise repeated
/// swaps would walk away from the music.
TEST(StemSwapTest, RoundTripReturnsToTheStartingFrame) {
    const double start = 123.456 * k48k;
    const double inStem = transferFramePosition(
            start, k48k, k44k, signedOffsetSeconds(kTypicalDelay, Direction::OriginalToStem));
    const double back = transferFramePosition(
            inStem, k44k, k48k, signedOffsetSeconds(kTypicalDelay, Direction::StemToOriginal));
    EXPECT_NEAR(start, back, 1e-6);
}

/// Inside the stem's leading delay there is no matching moment in the original.
TEST(StemSwapTest, LeadingDelayLandsAtTheStart) {
    const double frame = 0.02 * k44k;
    EXPECT_DOUBLE_EQ(0.0, transferFramePosition(frame, k44k, k48k, -kTypicalDelay));
}

/// The CKay outlier (+47.9 ms): nothing may assume a particular constant.
TEST(StemSwapTest, WorksWithAnOutlierOffset) {
    const double outlier = 0.04789;
    EXPECT_DOUBLE_EQ((30.0 + outlier) * k48k,
            transferFramePosition(30.0 * k48k, k48k, k48k, outlier));
}

TEST(StemSwapTest, UnusableInputSeeksToTheStart) {
    EXPECT_DOUBLE_EQ(0.0, transferFramePosition(kNaN, k48k, k48k, kTypicalDelay));
    EXPECT_DOUBLE_EQ(0.0, transferFramePosition(kInf, k48k, k48k, kTypicalDelay));
    EXPECT_DOUBLE_EQ(0.0, transferFramePosition(-100.0, k48k, k48k, kTypicalDelay));
    EXPECT_DOUBLE_EQ(0.0, transferFramePosition(1000.0, 0.0, k48k, kTypicalDelay));
    EXPECT_DOUBLE_EQ(0.0, transferFramePosition(1000.0, k48k, kNaN, kTypicalDelay));
    EXPECT_DOUBLE_EQ(0.0, transferFramePosition(1000.0, k48k, k48k, kNaN));
}

/// Loop markers are interleaved stereo sample positions (frames * 2).
TEST(StemSwapTest, LoopMarkersAreConvertedInEngineSamples) {
    const double sourceSamples = 2.0 * 10.0 * k48k;
    EXPECT_DOUBLE_EQ(2.0 * (10.0 + kTypicalDelay) * k44k,
            transferEngineSamplePosition(sourceSamples, k48k, k44k, kTypicalDelay));
}

/// -1 is Mixxx's "no loop marker"; it must stay a no-marker, not become 0.
TEST(StemSwapTest, MissingLoopMarkerStaysMissing) {
    EXPECT_DOUBLE_EQ(-1.0, transferEngineSamplePosition(-1.0, k48k, k44k, kTypicalDelay));
}

} // namespace
