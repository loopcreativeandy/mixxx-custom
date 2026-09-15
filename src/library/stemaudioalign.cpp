#include "library/stemaudioalign.h"

#include <algorithm>
#include <cmath>

#include "sources/audiosource.h"
#include "sources/soundsourceproxy.h"
#include "track/track.h"
#include "util/logger.h"
#include "util/samplebuffer.h"

namespace mixxx {
namespace stemaudioalign {

namespace {

const Logger kLogger("StemAudioAlign");

/// Rate of the coarse search. Low enough to be cheap over the whole search
/// range, high enough for a 0.5 ms step, and the box filter that gets there
/// keeps the bass and kick drum that line up most clearly.
constexpr double kCoarseRate = 2000.0;
/// The fine search looks this far on either side of the coarse result.
constexpr double kFineSearchSeconds = 0.004;
/// Length of the audio the fine search compares, starting at the first sound.
constexpr double kFineWindowSeconds = 20.0;
/// "The first sound": the first sample above roughly -40 dBFS.
constexpr float kSoundThreshold = 0.01f;
constexpr SINT kReadChunkFrames = 16384;

/// Downsamples by averaging every sample that falls into an output sample,
/// which doubles as the anti-aliasing filter.
std::vector<float> boxResample(
        const std::vector<float>& input, double inputRate, double outputRate) {
    const double ratio = inputRate / outputRate;
    const auto outputSize = static_cast<std::size_t>(
            std::floor(static_cast<double>(input.size()) / ratio));
    std::vector<float> output(outputSize);
    for (std::size_t i = 0; i < outputSize; ++i) {
        const auto begin = static_cast<std::size_t>(std::floor(i * ratio));
        const auto end = std::min(input.size(),
                std::max(begin + 1, static_cast<std::size_t>(std::floor((i + 1) * ratio))));
        double sum = 0.0;
        for (std::size_t j = begin; j < end; ++j) {
            sum += input[j];
        }
        output[i] = static_cast<float>(sum / static_cast<double>(end - begin));
    }
    return output;
}

/// Linear interpolation onto another sample rate. Only used where the rates
/// are already close (44.1 vs 48 kHz) and the signal is compared at the
/// source's rate.
std::vector<float> linearResample(
        const std::vector<float>& input, double inputRate, double outputRate) {
    if (input.empty() || inputRate == outputRate) {
        return input;
    }
    const double step = inputRate / outputRate;
    const auto outputSize = static_cast<std::size_t>(
            std::floor(static_cast<double>(input.size() - 1) / step)) +
            1;
    std::vector<float> output(outputSize);
    for (std::size_t i = 0; i < outputSize; ++i) {
        const double position = i * step;
        const auto index = static_cast<std::size_t>(position);
        const double fraction = position - static_cast<double>(index);
        const float next = index + 1 < input.size() ? input[index + 1] : input[index];
        output[i] = static_cast<float>(input[index] + fraction * (next - input[index]));
    }
    return output;
}

struct Correlation {
    double value = 0.0;
    double sourceEnergy = 0.0;
    double targetEnergy = 0.0;
};

/// sum(source[t] * target[t + lag]) for t in [begin, end), wherever both
/// signals have a sample.
Correlation correlateAt(const std::vector<float>& source,
        const std::vector<float>& target,
        std::ptrdiff_t lag,
        std::ptrdiff_t begin,
        std::ptrdiff_t end,
        bool withEnergy) {
    const auto sourceSize = static_cast<std::ptrdiff_t>(source.size());
    const auto targetSize = static_cast<std::ptrdiff_t>(target.size());
    begin = std::max({begin, std::ptrdiff_t{0}, -lag});
    end = std::min({end, sourceSize, targetSize - lag});
    Correlation result;
    if (end <= begin) {
        return result;
    }
    const float* const pSource = source.data();
    const float* const pTarget = target.data() + lag;
    double sum = 0.0;
    for (std::ptrdiff_t t = begin; t < end; ++t) {
        sum += pSource[t] * pTarget[t];
    }
    result.value = sum;
    if (withEnergy) {
        double sourceEnergy = 0.0;
        double targetEnergy = 0.0;
        for (std::ptrdiff_t t = begin; t < end; ++t) {
            sourceEnergy += pSource[t] * pSource[t];
            targetEnergy += pTarget[t] * pTarget[t];
        }
        result.sourceEnergy = sourceEnergy;
        result.targetEnergy = targetEnergy;
    }
    return result;
}

struct Peak {
    std::ptrdiff_t lag = 0;
    double subSample = 0.0;
    bool atEdge = false;
};

Peak findPeak(const std::vector<float>& source,
        const std::vector<float>& target,
        std::ptrdiff_t minLag,
        std::ptrdiff_t maxLag,
        std::ptrdiff_t begin,
        std::ptrdiff_t end) {
    std::vector<double> values;
    values.reserve(static_cast<std::size_t>(maxLag - minLag + 1));
    for (std::ptrdiff_t lag = minLag; lag <= maxLag; ++lag) {
        values.push_back(correlateAt(source, target, lag, begin, end, false).value);
    }
    const auto best = static_cast<std::size_t>(
            std::max_element(values.begin(), values.end()) - values.begin());
    Peak peak;
    peak.lag = minLag + static_cast<std::ptrdiff_t>(best);
    peak.atEdge = best == 0 || best + 1 == values.size();
    if (!peak.atEdge) {
        // Parabola through the peak and its neighbours.
        const double y0 = values[best - 1];
        const double y1 = values[best];
        const double y2 = values[best + 1];
        const double denominator = y0 - 2.0 * y1 + y2;
        if (denominator != 0.0) {
            peak.subSample = std::clamp(0.5 * (y0 - y2) / denominator, -0.5, 0.5);
        }
    }
    return peak;
}

std::ptrdiff_t firstSoundIndex(const std::vector<float>& signal) {
    const auto it = std::find_if(signal.begin(), signal.end(), [](float sample) {
        return std::abs(sample) > kSoundThreshold;
    });
    return it == signal.end() ? -1 : static_cast<std::ptrdiff_t>(it - signal.begin());
}

} // anonymous namespace

std::optional<AudioOffset> measureOffset(
        const std::vector<float>& source,
        audio::SampleRate sourceRate,
        const std::vector<float>& target,
        audio::SampleRate targetRate,
        double maxLagSeconds) {
    if (!sourceRate.isValid() || !targetRate.isValid()) {
        return std::nullopt;
    }
    const double sourceFramesPerSecond = static_cast<double>(sourceRate);
    const double targetFramesPerSecond = static_cast<double>(targetRate);
    // At least a few seconds past the search range, or there is nothing to
    // compare.
    const double minSeconds = 2.0 * maxLagSeconds + 1.0;
    if (source.size() < minSeconds * sourceFramesPerSecond ||
            target.size() < minSeconds * targetFramesPerSecond) {
        return std::nullopt;
    }

    // Coarse: both files at the same low rate, over the whole search range.
    const std::vector<float> coarseSource =
            boxResample(source, sourceFramesPerSecond, kCoarseRate);
    const std::vector<float> coarseTarget =
            boxResample(target, targetFramesPerSecond, kCoarseRate);
    const auto coarseMaxLag = static_cast<std::ptrdiff_t>(maxLagSeconds * kCoarseRate);
    const Peak coarse = findPeak(coarseSource,
            coarseTarget,
            -coarseMaxLag,
            coarseMaxLag,
            0,
            static_cast<std::ptrdiff_t>(coarseSource.size()));
    if (coarse.atEdge) {
        return std::nullopt;
    }
    const double coarseSeconds = (coarse.lag + coarse.subSample) / kCoarseRate;

    // Fine: the target at the source's rate, a few milliseconds around the
    // coarse result, over a window that starts at the first sound.
    const std::vector<float> fineTarget =
            linearResample(target, targetFramesPerSecond, sourceFramesPerSecond);
    const std::ptrdiff_t begin = firstSoundIndex(source);
    if (begin < 0) {
        return std::nullopt;
    }
    const std::ptrdiff_t end = begin +
            static_cast<std::ptrdiff_t>(kFineWindowSeconds * sourceFramesPerSecond);
    const auto fineCenter =
            static_cast<std::ptrdiff_t>(std::round(coarseSeconds * sourceFramesPerSecond));
    const auto fineRange = static_cast<std::ptrdiff_t>(
            std::ceil(kFineSearchSeconds * sourceFramesPerSecond));
    const Peak fine = findPeak(source,
            fineTarget,
            fineCenter - fineRange,
            fineCenter + fineRange,
            begin,
            end);
    if (fine.atEdge) {
        return std::nullopt;
    }

    const Correlation atPeak = correlateAt(source, fineTarget, fine.lag, begin, end, true);
    const double energy = std::sqrt(atPeak.sourceEnergy * atPeak.targetEnergy);
    if (energy <= 0.0) {
        return std::nullopt;
    }
    AudioOffset offset;
    offset.seconds = (fine.lag + fine.subSample) / sourceFramesPerSecond;
    offset.correlation = atPeak.value / energy;
    if (offset.correlation < kMinCorrelation) {
        kLogger.info() << "Best match only correlates at" << offset.correlation
                       << "- not the same recording";
        return std::nullopt;
    }
    return offset;
}

std::vector<float> decodeMono(
        const TrackPointer& pTrack,
        double maxSeconds,
        audio::SampleRate* pSampleRate) {
    *pSampleRate = audio::SampleRate();
    if (!pTrack) {
        return {};
    }
    AudioSource::OpenParams openParams;
    // A stem file mixes all of its stems down to stereo, which is what the
    // original sounds like.
    openParams.setChannelCount(audio::ChannelCount::stereo());
    const AudioSourcePointer pAudioSource =
            SoundSourceProxy(pTrack).openAudioSource(openParams);
    if (!pAudioSource) {
        kLogger.warning() << "Failed to open" << pTrack->getLocation();
        return {};
    }
    const auto signalInfo = pAudioSource->getSignalInfo();
    const int channels = signalInfo.getChannelCount();
    const auto sampleRate = signalInfo.getSampleRate();
    if (channels <= 0 || !sampleRate.isValid()) {
        return {};
    }

    // Cue positions count from frame 0, so the signal does too.
    const SINT endFrame = std::min(pAudioSource->frameIndexRange().end(),
            static_cast<SINT>(maxSeconds * static_cast<double>(sampleRate)));
    std::vector<float> mono(static_cast<std::size_t>(std::max(SINT{0}, endFrame)), 0.0f);
    SampleBuffer buffer(kReadChunkFrames * channels);
    SINT frame = std::max(SINT{0}, pAudioSource->frameIndexRange().start());
    while (frame < endFrame) {
        const auto chunk = IndexRange::forward(
                frame, std::min(kReadChunkFrames, endFrame - frame));
        const ReadableSampleFrames read = pAudioSource->readSampleFrames(
                WritableSampleFrames(chunk, SampleBuffer::WritableSlice(buffer)));
        const IndexRange readRange = read.frameIndexRange();
        if (readRange.empty()) {
            break;
        }
        const CSAMPLE* pData = read.readableData();
        SINT index = readRange.start();
        for (SINT i = 0; i + channels <= read.readableLength() && index < endFrame;
                i += channels, ++index) {
            float sum = 0.0f;
            for (int channel = 0; channel < channels; ++channel) {
                sum += pData[i + channel];
            }
            mono[static_cast<std::size_t>(index)] = sum / static_cast<float>(channels);
        }
        frame = readRange.end();
    }
    *pSampleRate = sampleRate;
    return mono;
}

std::optional<AudioOffset> measureTrackOffset(
        const TrackPointer& pSource,
        const TrackPointer& pTarget) {
    audio::SampleRate sourceRate;
    const std::vector<float> source = decodeMono(pSource, kDecodeSeconds, &sourceRate);
    audio::SampleRate targetRate;
    const std::vector<float> target = decodeMono(pTarget, kDecodeSeconds, &targetRate);
    const auto offset = measureOffset(source, sourceRate, target, targetRate);
    if (offset) {
        kLogger.info() << pTarget->getLocation() << "is"
                       << offset->seconds * 1000.0 << "ms later than"
                       << pSource->getLocation() << "- correlation"
                       << offset->correlation;
    } else if (pTarget) {
        kLogger.info() << "No audio offset for" << pTarget->getLocation();
    }
    return offset;
}

} // namespace stemaudioalign
} // namespace mixxx
