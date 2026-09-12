#pragma once

#include <cstddef>
#include <limits>
#include <span>
#include <vector>

#include "audio/types.h"
#include "util/types.h"

struct GroupFeatureState;

// Range of the [Master],preview_beat_click_gain control, in dB. Shared by the
// engine control and the preferences page.
constexpr double kBeatClickGainMinDb = -24.0;
constexpr double kBeatClickGainMaxDb = 6.0;

/// Mixes a short click onto every beat of the track a deck is playing, so that
/// a beatgrid can be checked by ear instead of by eye: if the clicks and the
/// drums of the track drift apart, the grid (or the BPM) is wrong.
///
/// The beat positions come from the GroupFeatureState the deck collects for the
/// effect chain, i.e. from the same beatgrid the waveform markers are drawn
/// from, already corrected for the deck rate. Nothing is played while the deck
/// is stopped or scratching, or while the track has no beats.
///
/// The class is stateful because a click that starts near the end of one engine
/// buffer has to be continued in the next one.
class BeatClick {
  public:
    /// Mixes the click into pOutput, and additionally into pSecondaryOutput
    /// when that is not nullptr (the pre-EQ headphone tap holds a second copy
    /// of the same signal). Both buffers must hold bufferSize samples.
    /// gain is a linear factor applied to the click only.
    void process(CSAMPLE* pOutput,
            CSAMPLE* pSecondaryOutput,
            std::size_t bufferSize,
            mixxx::audio::SampleRate sampleRate,
            const GroupFeatureState& groupFeatures,
            CSAMPLE_GAIN gain);

    /// Drops a click that is currently in flight, so that switching the click
    /// off and on again does not resume it mid-sample.
    void reset();

  private:
    // The click sample for this sample rate, either the one shipped with the
    // metronome effect or, for sample rates that does not cover, a synthesized
    // one (built once per sample rate).
    std::span<const CSAMPLE> clickSamples(mixxx::audio::SampleRate sampleRate);

    static constexpr std::size_t kNoClickInFlight =
            std::numeric_limits<std::size_t>::max();

    // Frames elapsed since the last click started. kNoClickInFlight when no
    // click is playing.
    std::size_t m_framesSinceLastClick = kNoClickInFlight;

    mixxx::audio::SampleRate m_synthesizedRate;
    std::vector<CSAMPLE> m_synthesizedClick;
};
