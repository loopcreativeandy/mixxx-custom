#include "library/stemswap.h"

#include <algorithm>
#include <cmath>

namespace mixxx {
namespace stemswap {

namespace {

/// No track is longer than this; anything beyond it (including an infinity, and
/// any NaN, which fails every comparison) is a broken measurement, not a
/// position. Written as explicit comparisons because -ffast-math makes
/// std::isfinite()/std::isnan() unusable here.
constexpr double kMaxUsableSeconds = 24.0 * 60.0 * 60.0;

bool isUsableSeconds(double value) {
    return value >= -kMaxUsableSeconds && value <= kMaxUsableSeconds;
}

} // anonymous namespace

std::optional<double> targetPositionSeconds(
        double sourcePositionSeconds,
        double audioOffsetSeconds,
        Direction direction) {
    // NOTE: Mixxx is built with -ffast-math, under which std::isfinite() is
    // optimized into a constant `true` and cannot be used to reject NaN/inf.
    // Compare against the finite range explicitly instead - those comparisons
    // survive because they are false for NaN and for infinities either way.
    if (!isUsableSeconds(sourcePositionSeconds) || sourcePositionSeconds < 0.0) {
        return std::nullopt;
    }
    if (!isUsableSeconds(audioOffsetSeconds)) {
        return std::nullopt;
    }
    // The offset is always "the stem is this much later than the original", so
    // it is added going to the stem and subtracted coming back from it.
    const double corrected = direction == Direction::OriginalToStem
            ? sourcePositionSeconds + audioOffsetSeconds
            : sourcePositionSeconds - audioOffsetSeconds;
    if (corrected < 0.0) {
        // Only reachable inside the leading codec delay of the stem file: that
        // audio simply does not exist in the original.
        return std::nullopt;
    }
    return corrected;
}

double clampToTrack(double positionSeconds, double targetDurationSeconds) {
    if (!(targetDurationSeconds > 0.0)) {
        return positionSeconds;
    }
    // Only a value proven to sit inside the track is passed through; anything
    // else (NaN, either infinity, a negative position) collapses to a safe end
    // of the range. Phrased as a positive range test because -ffast-math lets
    // the compiler rewrite a negated comparison as if NaNs did not exist.
    if (positionSeconds >= 0.0 && positionSeconds <= targetDurationSeconds) {
        return positionSeconds;
    }
    if (positionSeconds > targetDurationSeconds) {
        return targetDurationSeconds;
    }
    // Negative, or not a number at all.
    return 0.0;
}

} // namespace stemswap
} // namespace mixxx
