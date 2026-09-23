#include "library/stemswap.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace {

using mixxx::stemswap::Direction;
using mixxx::stemswap::clampToTrack;
using mixxx::stemswap::targetPositionSeconds;

/// The codec delay measured on Andy's library with Mixxx's own decoders
/// (CP93): ~72 ms for 48 kHz sources.
constexpr double kTypicalOffset = 0.07189;

TEST(StemSwapTest, OriginalToStemAddsTheOffset) {
    const auto result = targetPositionSeconds(60.0, kTypicalOffset, Direction::OriginalToStem);
    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(60.0 + kTypicalOffset, *result);
}

TEST(StemSwapTest, StemToOriginalSubtractsTheOffset) {
    const auto result = targetPositionSeconds(60.0, kTypicalOffset, Direction::StemToOriginal);
    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(60.0 - kTypicalOffset, *result);
}

/// The two directions must undo each other exactly, otherwise swapping back and
/// forth would walk the position away from the music.
TEST(StemSwapTest, RoundTripReturnsToTheStartingPosition) {
    const double start = 123.456;
    const auto toStem = targetPositionSeconds(start, kTypicalOffset, Direction::OriginalToStem);
    ASSERT_TRUE(toStem.has_value());
    const auto backToOriginal =
            targetPositionSeconds(*toStem, kTypicalOffset, Direction::StemToOriginal);
    ASSERT_TRUE(backToOriginal.has_value());
    EXPECT_DOUBLE_EQ(start, *backToOriginal);
}

/// Inside the stem's leading codec delay there is no corresponding moment in the
/// original yet, so the swap must refuse rather than clamp silently to 0.
TEST(StemSwapTest, StemToOriginalRefusesInsideTheLeadingDelay) {
    const auto result = targetPositionSeconds(0.02, kTypicalOffset, Direction::StemToOriginal);
    EXPECT_FALSE(result.has_value());
}

TEST(StemSwapTest, StartOfOriginalMapsIntoTheStem) {
    const auto result = targetPositionSeconds(0.0, kTypicalOffset, Direction::OriginalToStem);
    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(kTypicalOffset, *result);
}

TEST(StemSwapTest, ZeroOffsetIsAPlainPositionTransfer) {
    const auto result = targetPositionSeconds(42.0, 0.0, Direction::OriginalToStem);
    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(42.0, *result);
}

/// A failed measurement must not be able to produce a wild seek.
///
/// Regression guard: the first version used std::isfinite(), which Mixxx's
/// -ffast-math build folds into a constant `true` - the check was dead code and
/// an infinity sailed straight through into a seek.
TEST(StemSwapTest, RefusesNonFiniteInput) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(targetPositionSeconds(nan, kTypicalOffset, Direction::OriginalToStem).has_value());
    EXPECT_FALSE(targetPositionSeconds(60.0, nan, Direction::OriginalToStem).has_value());
    EXPECT_FALSE(targetPositionSeconds(inf, kTypicalOffset, Direction::OriginalToStem).has_value());
    EXPECT_FALSE(targetPositionSeconds(60.0, inf, Direction::OriginalToStem).has_value());
}

TEST(StemSwapTest, RefusesNegativeSourcePosition) {
    EXPECT_FALSE(targetPositionSeconds(-1.0, kTypicalOffset, Direction::OriginalToStem)
                         .has_value());
}

/// The outlier from Andy's library (CKay, +47.9 ms) must behave the same way -
/// nothing in here assumes a particular constant.
TEST(StemSwapTest, WorksWithAnOutlierOffset) {
    const double outlier = 0.04789;
    const auto result = targetPositionSeconds(30.0, outlier, Direction::OriginalToStem);
    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(30.0 + outlier, *result);
}

TEST(StemSwapTest, ClampKeepsPositionsInsideTheTrack) {
    EXPECT_DOUBLE_EQ(0.0, clampToTrack(-5.0, 100.0));
    EXPECT_DOUBLE_EQ(100.0, clampToTrack(150.0, 100.0));
    EXPECT_DOUBLE_EQ(50.0, clampToTrack(50.0, 100.0));
}

/// An unknown duration must not collapse the position to zero.
TEST(StemSwapTest, ClampPassesThroughWhenDurationIsUnknown) {
    EXPECT_DOUBLE_EQ(50.0, clampToTrack(50.0, 0.0));
    EXPECT_DOUBLE_EQ(50.0, clampToTrack(50.0, -1.0));
}

/// std::clamp would hand a NaN straight back; a NaN fraction reaching the
/// playposition control is an unpredictable seek on a live deck.
TEST(StemSwapTest, ClampCollapsesNonFinitePositionsToTheStart) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_DOUBLE_EQ(0.0, clampToTrack(nan, 100.0));
    EXPECT_DOUBLE_EQ(100.0, clampToTrack(std::numeric_limits<double>::infinity(), 100.0));
    EXPECT_DOUBLE_EQ(0.0, clampToTrack(-std::numeric_limits<double>::infinity(), 100.0));
}

/// A stem is normally a few tens of milliseconds longer than its original; a
/// swap late in the track must not seek past the end.
TEST(StemSwapTest, LatePositionClampsToTheTargetEnd) {
    const double originalDuration = 200.0;
    const auto result = targetPositionSeconds(
            originalDuration, kTypicalOffset, Direction::OriginalToStem);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(*result, originalDuration);
    EXPECT_DOUBLE_EQ(originalDuration, clampToTrack(*result, originalDuration));
}

} // namespace
