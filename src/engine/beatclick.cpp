#include "engine/beatclick.h"

#include <algorithm>
#include <cmath>

#include "effects/backends/builtin/metronomeclick.h"
#include "engine/effects/groupfeaturestate.h"
#include "engine/engine.h"
#include "util/sample.h"

namespace {

// Fallback click for sample rates the shipped metronome recording does not
// cover: a short decaying sine burst. Sharp enough on the attack to judge
// alignment against a kick drum.
constexpr double kSynthSeconds = 0.03;
constexpr double kSynthFrequency = 2000.0;
constexpr double kSynthDecaySeconds = 0.006;
constexpr double kSynthAmplitude = 0.5;

// Both helpers mirror the ones in metronomeeffect.cpp, which computes the same
// click placement for the Metronome effect. Kept separate to avoid changing
// upstream files.
std::size_t playMonoSamplesWithGain(std::span<const CSAMPLE> monoSource,
        std::span<CSAMPLE> output,
        CSAMPLE_GAIN gain) {
    const std::size_t outputBufferFrames = output.size() / mixxx::kEngineChannelOutputCount;
    const std::size_t framesPlayed = std::min(monoSource.size(), outputBufferFrames);
    SampleUtil::addMonoToStereoWithGain(gain,
            output.data(),
            monoSource.data(),
            static_cast<SINT>(framesPlayed));
    return framesPlayed;
}

/// Returns the part of the output buffer that starts on the beat which fell
/// into this buffer, or an empty span when no beat did.
std::span<CSAMPLE> beatOutput(double beatFractionBufferEnd,
        const GroupFeatureBeatLength& beatLengthAndScratch,
        mixxx::audio::SampleRate sampleRate,
        std::span<CSAMPLE> output) {
    if (beatLengthAndScratch.scratch_rate == 0.0) {
        // Stopped or scratching to a standstill.
        return {};
    }
    const double beatLength = beatLengthAndScratch.seconds *
            sampleRate / beatLengthAndScratch.scratch_rate;

    // Playing backwards: the beat we are approaching is the previous one.
    const bool needsPreviousBeat = beatLength < 0;
    const double beatToBufferEndFrames = std::abs(beatLength) *
            (needsPreviousBeat ? (1 - beatFractionBufferEnd) : beatFractionBufferEnd);
    const std::size_t beatToBufferEndSamples =
            static_cast<std::size_t>(beatToBufferEndFrames) *
            mixxx::kEngineChannelOutputCount;

    if (beatToBufferEndSamples <= output.size()) {
        return output.last(beatToBufferEndSamples);
    }
    return {};
}

} // namespace

void BeatClick::reset() {
    m_framesSinceLastClick = kNoClickInFlight;
}

std::span<const CSAMPLE> BeatClick::clickSamples(mixxx::audio::SampleRate sampleRate) {
    switch (sampleRate.value()) {
    case 44100:
    case 48000:
    case 96000:
        // The recording shipped for the Metronome effect.
        return clickForSampleRate(sampleRate);
    default:
        break;
    }

    if (m_synthesizedRate != sampleRate) {
        const auto frames = static_cast<std::size_t>(sampleRate * kSynthSeconds);
        m_synthesizedClick.resize(frames);
        for (std::size_t i = 0; i < frames; ++i) {
            const double t = static_cast<double>(i) / sampleRate;
            m_synthesizedClick[i] = static_cast<CSAMPLE>(kSynthAmplitude *
                    std::sin(2.0 * M_PI * kSynthFrequency * t) *
                    std::exp(-t / kSynthDecaySeconds));
        }
        m_synthesizedRate = sampleRate;
    }
    return m_synthesizedClick;
}

void BeatClick::process(CSAMPLE* pOutput,
        CSAMPLE* pSecondaryOutput,
        std::size_t bufferSize,
        mixxx::audio::SampleRate sampleRate,
        const GroupFeatureState& groupFeatures,
        CSAMPLE_GAIN gain) {
    if (!sampleRate.isValid() || bufferSize == 0) {
        return;
    }

    const std::span<const CSAMPLE> click = clickSamples(sampleRate);
    if (click.empty()) {
        return;
    }

    const auto output = std::span<CSAMPLE>(pOutput, bufferSize);
    const auto secondary = pSecondaryOutput
            ? std::span<CSAMPLE>(pSecondaryOutput, bufferSize)
            : std::span<CSAMPLE>();
    const std::size_t framesPerBuffer = bufferSize / mixxx::kEngineChannelOutputCount;

    // Continue a click that started in one of the previous buffers.
    if (m_framesSinceLastClick != kNoClickInFlight) {
        if (m_framesSinceLastClick < click.size()) {
            const auto rest = click.subspan(m_framesSinceLastClick);
            playMonoSamplesWithGain(rest, output, gain);
            if (!secondary.empty()) {
                playMonoSamplesWithGain(rest, secondary, gain);
            }
            m_framesSinceLastClick += framesPerBuffer;
        } else {
            m_framesSinceLastClick = kNoClickInFlight;
        }
    }

    // Without a beatgrid (or a position on it) there is nothing to click on.
    if (!groupFeatures.beat_length.has_value() ||
            !groupFeatures.beat_fraction_buffer_end.has_value()) {
        return;
    }

    const std::span<CSAMPLE> beatSpan = beatOutput(*groupFeatures.beat_fraction_buffer_end,
            *groupFeatures.beat_length,
            sampleRate,
            output);
    if (beatSpan.empty()) {
        return;
    }

    const std::size_t framesPlayed = playMonoSamplesWithGain(click, beatSpan, gain);
    if (!secondary.empty()) {
        // Same offset from the end of the buffer, same click.
        playMonoSamplesWithGain(click, secondary.last(beatSpan.size()), gain);
    }
    m_framesSinceLastClick = framesPlayed;
}
