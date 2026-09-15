#pragma once

#include <optional>
#include <vector>

#include "audio/types.h"
#include "track/track_decl.h"

/// Andy's stem workflow (andy-custom): measures how much later the audio of a
/// stem file starts than the audio of the track it was generated from.
///
/// The stem extractor decodes the original and re-encodes it, and the codec
/// delay of that round trip ends up in front of the stem's audio. It is not a
/// constant - 95 ms and 99 ms for most of Andy's files, 24 ms for others - and
/// guessing it from the two analyzed beat grids fails whenever one of those
/// grids is off. The audio itself does not lie: both files contain the same
/// recording, so cross-correlating them finds the delay directly, with a
/// correlation close to 1.0 when the files really match.
namespace mixxx {
namespace stemaudioalign {

/// Largest delay that is searched for, in either direction.
constexpr double kMaxLagSeconds = 1.0;
/// How much audio from the start of each file is decoded and compared.
constexpr double kDecodeSeconds = 45.0;
/// Below this normalized correlation the two files are not considered the same
/// recording, and no offset is reported.
constexpr double kMinCorrelation = 0.6;

struct AudioOffset {
    /// Positive: the target's audio is later than the source's.
    double seconds = 0.0;
    /// Normalized correlation at the measured offset, 1.0 = identical.
    double correlation = 0.0;
};

/// Finds the time offset of `target` against `source`, both mono signals at
/// their own sample rate. Coarse search on a low-passed 2 kHz copy over
/// +-`maxLagSeconds`, then refined at the source's sample rate to a fraction of
/// a sample.
///
/// Returns nullopt if either signal is too short or silent, if the best match
/// sits at the edge of the search range, or if the correlation there is below
/// kMinCorrelation.
std::optional<AudioOffset> measureOffset(
        const std::vector<float>& source,
        audio::SampleRate sourceRate,
        const std::vector<float>& target,
        audio::SampleRate targetRate,
        double maxLagSeconds = kMaxLagSeconds);

/// Decodes up to `maxSeconds` from the start of the track, the way a deck plays
/// it (a stem file is mixed down to all of its stems), as a mono signal.
/// Returns an empty vector and an invalid rate if the file cannot be read.
std::vector<float> decodeMono(
        const TrackPointer& pTrack,
        double maxSeconds,
        audio::SampleRate* pSampleRate);

/// Decodes both tracks and measures the offset of `pTarget` against `pSource`.
std::optional<AudioOffset> measureTrackOffset(
        const TrackPointer& pSource,
        const TrackPointer& pTarget);

} // namespace stemaudioalign
} // namespace mixxx
