#pragma once

#include <QList>
#include <QString>

#include "proto/keys.pb.h"

/// andy-custom CP133: key smart playlists relative to the loudest deck.
///
/// Three additive levers:
///  - Exact:       the reference key only.
///  - PlusMinusOne: exact + one step up/down on the Camelot wheel (same mode)
///                 + the relative major/minor. Four keys; deliberately NOT
///                 upstream's KeyUtils::getCompatibleKeys(), which also adds
///                 the two diagonals.
///  - Transpose:   PlusMinusOne of the reference key, of the key one semitone
///                 up and of the key one semitone down - i.e. every track that
///                 fits once it is key-shifted by -1, 0 or +1 semitone.
namespace andykey {

enum class Lever {
    Exact = 0,
    PlusMinusOne = 1,
    Transpose = 2,
};

constexpr int kLeverCount = 3;

/// Keys a track may have to show up in the lever's list. Empty for an
/// invalid reference key. No duplicates; the reference key comes first.
QList<mixxx::track::io::key::ChromaticKey> matchingKeys(
        mixxx::track::io::key::ChromaticKey reference, Lever lever);

/// Search query ("key:1m | key:2m | ...") for the given keys, using exact
/// (non-fuzzy) key filters. Empty input gives an empty string.
QString searchQueryForKeys(const QList<mixxx::track::io::key::ChromaticKey>& keys);

/// One deck as seen by the reference picker.
struct DeckState {
    bool loaded = false;
    bool playing = false;
    double volume = 0.0;      ///< channel fader, 0..1
    int orientation = 1;      ///< 0 left, 1 center, 2 right
    mixxx::track::io::key::ChromaticKey key = mixxx::track::io::key::INVALID;
};

/// Channel fader x crossfader side, both 0..1. Linear crossfader approximation
/// (the configured curve is not needed to tell which deck is loudest).
double deckLoudness(const DeckState& deck, double crossfader);

/// Index of the reference deck, or -1: the loudest playing deck; if none
/// plays, the loudest loaded deck. Ties go to the lower deck number.
int pickReferenceDeck(const QList<DeckState>& decks, double crossfader);

/// Sidebar base label for a lever, e.g. "Key: ±1".
QString leverLabel(Lever lever);

} // namespace andykey
