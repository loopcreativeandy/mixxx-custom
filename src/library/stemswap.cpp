#include "library/stemswap.h"

namespace mixxx {
namespace stemswap {

namespace {

// NOTE: Mixxx is built with -ffast-math. Under it std::isfinite()/std::isnan()
// fold to constants, std::clamp passes a NaN through, and even `!(x > 0.0)` may
// be rewritten as if NaNs did not exist. Every guard below is therefore a
// *positive* range test: only a value proven to be inside a sane band gets
// through, which is false for NaN and both infinities either way.

/// No track is longer than a day; anything beyond is a broken value.
constexpr double kMaxUsableSeconds = 24.0 * 60.0 * 60.0;
/// A codec delay is tens of milliseconds. A full second would already mean the
/// measurement matched the wrong part of the song.
constexpr double kMaxUsableOffsetSeconds = 1.0;
constexpr double kMinSampleRate = 1000.0;
constexpr double kMaxSampleRate = 1000000.0;

bool isUsableSampleRate(double rate) {
    return rate >= kMinSampleRate && rate <= kMaxSampleRate;
}

bool isUsableOffset(double seconds) {
    return seconds >= -kMaxUsableOffsetSeconds && seconds <= kMaxUsableOffsetSeconds;
}

} // anonymous namespace

double signedOffsetSeconds(double stemDelaySeconds, Direction direction) {
    if (!isUsableOffset(stemDelaySeconds)) {
        return 0.0;
    }
    // The delay is always "the stem is this much later than the original", so
    // it is added going to the stem and subtracted coming back from it.
    return direction == Direction::OriginalToStem ? stemDelaySeconds : -stemDelaySeconds;
}

double transferFramePosition(double sourceFrame,
        double sourceSampleRate,
        double targetSampleRate,
        double signedOffsetSeconds) {
    if (!isUsableSampleRate(sourceSampleRate) || !isUsableSampleRate(targetSampleRate)) {
        return 0.0;
    }
    if (!isUsableOffset(signedOffsetSeconds)) {
        return 0.0;
    }
    const double sourceSeconds = sourceFrame / sourceSampleRate;
    if (!(sourceSeconds >= 0.0 && sourceSeconds <= kMaxUsableSeconds)) {
        return 0.0;
    }
    const double targetSeconds = sourceSeconds + signedOffsetSeconds;
    if (targetSeconds >= 0.0) {
        return targetSeconds * targetSampleRate;
    }
    // Inside the stem's leading delay: the moment does not exist in the target.
    return 0.0;
}

double transferEngineSamplePosition(double sourceEngineSamplePos,
        double sourceSampleRate,
        double targetSampleRate,
        double signedOffsetSeconds) {
    if (sourceEngineSamplePos >= 0.0) {
        return 2.0 *
                transferFramePosition(sourceEngineSamplePos / 2.0,
                        sourceSampleRate,
                        targetSampleRate,
                        signedOffsetSeconds);
    }
    // -1 (kNoTrigger) = no marker set. Anything else that is not a
    // non-negative number is equally "no marker".
    return sourceEngineSamplePos < 0.0 ? sourceEngineSamplePos : -1.0;
}

} // namespace stemswap
} // namespace mixxx
