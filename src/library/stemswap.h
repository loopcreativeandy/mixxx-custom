#pragma once

/// Andy's live stem workflow (andy-custom, CP97): while a normal track plays, he
/// wants its stem counterpart (or vice versa) brought up in another deck at
/// *exactly* the same musical position, so he can fade over to it by hand and
/// keep mixing with stems.
///
/// The position transfer is the whole problem. Mixxx's own Clone Deck copies the
/// play position as a raw frame number of the *other* file. That is correct only
/// when both decks hold the same file. A stem file is re-encoded from its
/// original and picks up a codec delay at the front - measured on Andy's library
/// with Mixxx's own decoders: ~+71.9 ms (48 kHz sources), ~+74.0 ms (44.1 kHz),
/// +47.9 ms for one outlier - and it often has a different sample rate too.
///
/// So the swap reuses Clone Deck (which also copies tempo, pitch and loop state,
/// and seeks sample-exactly inside the engine), but converts the position through
/// the time domain: frames of the source -> seconds -> plus the measured offset
/// -> frames of the target at the target's own sample rate.
namespace mixxx {
namespace stemswap {

/// Which way the swap goes.
enum class Direction {
    /// Playing the original, bringing up the stem: the stem is later, so the
    /// same musical moment sits *further into* the stem file.
    OriginalToStem,
    /// Playing the stem, bringing up the original: the reverse.
    StemToOriginal,
};

/// Turns the measured codec delay into the shift to apply to a source position.
///
/// `stemDelaySeconds` is always "how much later the *stem's* audio is than the
/// original's", i.e. the positive codec delay, exactly as
/// stemaudioalign::measureTrackOffset(original, stem) reports it.
double signedOffsetSeconds(double stemDelaySeconds, Direction direction);

/// Frame position in the target file that holds the same musical moment as
/// `sourceFrame` in the source file.
///
/// Returns a position >= 0. Inside the stem's leading delay (swapping to the
/// original in the first ~70 ms) there is no such moment in the original yet,
/// so the result is the start of the file. Any unusable input (NaN, infinities,
/// a missing sample rate, an absurd offset) also yields 0 rather than a wild seek
/// on a live deck.
double transferFramePosition(double sourceFrame,
        double sourceSampleRate,
        double targetSampleRate,
        double signedOffsetSeconds);

/// Same conversion for a loop marker stored as an engine sample position
/// (interleaved stereo samples, i.e. frames * 2). Negative values mean "no
/// marker" in Mixxx and are passed through unchanged.
double transferEngineSamplePosition(double sourceEngineSamplePos,
        double sourceSampleRate,
        double targetSampleRate,
        double signedOffsetSeconds);

} // namespace stemswap
} // namespace mixxx
