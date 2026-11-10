#include "library/trackset/smartplaylist/keyreference.h"

#include <QStringList>

#include "track/keyutils.h"

using mixxx::track::io::key::ChromaticKey;

namespace andykey {

namespace {

bool isValidKey(ChromaticKey key) {
    return mixxx::track::io::key::ChromaticKey_IsValid(key) &&
            key != mixxx::track::io::key::INVALID;
}

void appendUnique(QList<ChromaticKey>* pKeys, ChromaticKey key) {
    if (!pKeys->contains(key)) {
        pKeys->append(key);
    }
}

void appendPlusMinusOne(QList<ChromaticKey>* pKeys, ChromaticKey key) {
    const int number = KeyUtils::keyToOpenKeyNumber(key);
    const bool major = KeyUtils::keyIsMajor(key);
    appendUnique(pKeys, key);
    appendUnique(pKeys, KeyUtils::openKeyNumberToKey(number == 12 ? 1 : number + 1, major));
    appendUnique(pKeys, KeyUtils::openKeyNumberToKey(number == 1 ? 12 : number - 1, major));
    appendUnique(pKeys, KeyUtils::openKeyNumberToKey(number, !major));
}

} // anonymous namespace

QList<ChromaticKey> matchingKeys(ChromaticKey reference, Lever lever) {
    QList<ChromaticKey> keys;
    if (!isValidKey(reference)) {
        return keys;
    }
    switch (lever) {
    case Lever::Exact:
        keys.append(reference);
        break;
    case Lever::PlusMinusOne:
        appendPlusMinusOne(&keys, reference);
        break;
    case Lever::Transpose:
        appendPlusMinusOne(&keys, reference);
        appendPlusMinusOne(&keys, KeyUtils::scaleKeySteps(reference, 1));
        appendPlusMinusOne(&keys, KeyUtils::scaleKeySteps(reference, -1));
        break;
    }
    return keys;
}

QString searchQueryForKeys(const QList<ChromaticKey>& keys) {
    QStringList terms;
    for (const ChromaticKey key : keys) {
        // OpenKey ("1d", "8m") is plain ASCII and round-trips through
        // KeyUtils::guessKeyFromText(), whatever notation the user displays
        // (unit-tested in keyreferencetest).
        terms << QStringLiteral("key:") +
                        KeyUtils::keyToString(key, KeyUtils::KeyNotation::OpenKey);
    }
    return terms.join(QStringLiteral(" | "));
}

double deckLoudness(const DeckState& deck, double crossfader) {
    double side = 1.0;
    if (deck.orientation == 0) { // left
        side = crossfader <= 0.0 ? 1.0 : 1.0 - crossfader;
    } else if (deck.orientation == 2) { // right
        side = crossfader >= 0.0 ? 1.0 : 1.0 + crossfader;
    }
    if (side < 0.0) {
        side = 0.0;
    }
    return deck.volume * side;
}

int pickReferenceDeck(const QList<DeckState>& decks, double crossfader) {
    for (const bool needPlaying : {true, false}) {
        int best = -1;
        double bestLoudness = -1.0;
        for (int i = 0; i < decks.size(); ++i) {
            const DeckState& deck = decks.at(i);
            if (!deck.loaded || (needPlaying && !deck.playing)) {
                continue;
            }
            const double loudness = deckLoudness(deck, crossfader);
            if (loudness > bestLoudness) {
                best = i;
                bestLoudness = loudness;
            }
        }
        if (best >= 0) {
            return best;
        }
    }
    return -1;
}

QString leverLabel(Lever lever) {
    switch (lever) {
    case Lever::Exact:
        return QStringLiteral("Key: exact");
    case Lever::PlusMinusOne:
        return QStringLiteral("Key: ±1");
    case Lever::Transpose:
        return QStringLiteral("Key: transpose ±1");
    }
    return QString();
}

} // namespace andykey
