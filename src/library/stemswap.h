#pragma once

#include <optional>

/// Andy's live stem workflow (andy-custom, CP98): while a normal track plays, he
/// wants its stem counterpart (or vice versa) brought up in another deck at
/// *exactly* the same musical position, so he can fade over to it by hand and
/// keep mixing with stems.
///
/// The position transfer is the whole problem. Mixxx's own Clone Deck copies the
/// play position as a *fraction of the track* (EngineBuffer::getExactPlayPos()
/// returns getTrackEndPosition() * visualPlayPos). That is correct only when both
/// decks hold the same file. A stem file is re-encoded from its original and
/// picks up a codec delay at the front - measured on Andy's library with Mixxx's
/// own decoders: ~+71.9 ms (48 kHz sources), ~+74.0 ms (44.1 kHz), +47.9 ms for
/// one outlier - and it therefore has a different length too. Cloning the
/// fraction would land tens of milliseconds off and, worse, drift with position.
///
/// So the transfer goes through the time domain: seconds in the source, plus the
/// measured offset, converted back to frames in the target's own sample rate.
namespace mixxx {
namespace stemswap {

/// Which direction the audio offset was measured in.
///
/// `audioOffsetSeconds` is always "how much later the *stem's* audio is than the
/// original's", i.e. the positive codec delay, exactly as
/// stemaudioalign::measureTrackOffset(original, stem) reports it and as
/// stemoriginal::importFromOriginal() consumes it.
enum class Direction {
    /// Playing the original, swapping to the stem: the stem is later, so the
    /// same musical moment sits *further into* the stem file.
    OriginalToStem,
    /// Playing the stem, swapping to the original: the reverse.
    StemToOriginal,
};

/// Position in the target file, in seconds, that holds the same musical moment
/// as `sourcePositionSeconds` in the source file.
///
/// Returns nullopt if the source position is not finite or negative, or if the
/// corrected position would fall before the start of the target file (which
/// happens legitimately in the first ~80 ms of a stem when swapping back to the
/// original - there is no such moment in the original yet).
std::optional<double> targetPositionSeconds(
        double sourcePositionSeconds,
        double audioOffsetSeconds,
        Direction direction);

/// Clamp a target position to the playable range of the target file.
/// `targetDurationSeconds` <= 0 means the duration is unknown; the position is
/// then returned unchanged.
double clampToTrack(double positionSeconds, double targetDurationSeconds);

} // namespace stemswap
} // namespace mixxx
