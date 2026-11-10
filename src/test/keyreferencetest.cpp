#include <gtest/gtest.h>

#include "library/trackset/smartplaylist/keyreference.h"
#include "track/keyutils.h"

using namespace mixxx::track::io::key;
using andykey::DeckState;
using andykey::Lever;

namespace {

TEST(KeyReferenceTest, InvalidReferenceMatchesNothing) {
    EXPECT_TRUE(andykey::matchingKeys(INVALID, Lever::Exact).isEmpty());
    EXPECT_TRUE(andykey::matchingKeys(INVALID, Lever::Transpose).isEmpty());
    EXPECT_TRUE(andykey::searchQueryForKeys({}).isEmpty());
}

TEST(KeyReferenceTest, ExactIsOnlyTheKey) {
    EXPECT_EQ(QList<ChromaticKey>({A_MINOR}), andykey::matchingKeys(A_MINOR, Lever::Exact));
}

TEST(KeyReferenceTest, PlusMinusOneIsWheelNeighboursAndRelative) {
    // 8A (A minor): 9A E minor, 7A D minor, 8B C major. No diagonals.
    const auto keys = andykey::matchingKeys(A_MINOR, Lever::PlusMinusOne);
    EXPECT_EQ(4, keys.size());
    EXPECT_EQ(A_MINOR, keys.first());
    EXPECT_TRUE(keys.contains(E_MINOR));
    EXPECT_TRUE(keys.contains(D_MINOR));
    EXPECT_TRUE(keys.contains(C_MAJOR));
    EXPECT_FALSE(keys.contains(G_MAJOR));
    EXPECT_FALSE(keys.contains(F_MAJOR));
}

TEST(KeyReferenceTest, PlusMinusOneWrapsAroundTheWheel) {
    // 12B (E major) -> 1B (B major), 11B (A major), 12A (C# minor).
    const auto keys = andykey::matchingKeys(E_MAJOR, Lever::PlusMinusOne);
    EXPECT_EQ(4, keys.size());
    EXPECT_TRUE(keys.contains(B_MAJOR));
    EXPECT_TRUE(keys.contains(A_MAJOR));
    EXPECT_TRUE(keys.contains(C_SHARP_MINOR));
}

TEST(KeyReferenceTest, LeversAreAdditive) {
    for (int k = C_MAJOR; k <= B_MINOR; ++k) {
        const auto key = static_cast<ChromaticKey>(k);
        const auto exact = andykey::matchingKeys(key, Lever::Exact);
        const auto pm1 = andykey::matchingKeys(key, Lever::PlusMinusOne);
        const auto transpose = andykey::matchingKeys(key, Lever::Transpose);
        for (const auto match : exact) {
            EXPECT_TRUE(pm1.contains(match));
        }
        for (const auto match : pm1) {
            EXPECT_TRUE(transpose.contains(match));
        }
        EXPECT_LT(pm1.size(), transpose.size());
    }
}

TEST(KeyReferenceTest, TransposeCoversOneSemitoneEitherWay) {
    // A minor's set, B-flat minor's set and G-sharp minor's set; E-flat minor
    // (= D-sharp minor) sits in both shifted sets, so 11 keys, not 12.
    const auto keys = andykey::matchingKeys(A_MINOR, Lever::Transpose);
    EXPECT_EQ(11, keys.size());
    for (const auto key : {A_MINOR, E_MINOR, D_MINOR, C_MAJOR,
                 B_FLAT_MINOR, F_MINOR, E_FLAT_MINOR, D_FLAT_MAJOR,
                 G_SHARP_MINOR, C_SHARP_MINOR, B_MAJOR}) {
        EXPECT_TRUE(keys.contains(key)) << KeyUtils::keyToString(key).toStdString();
    }
}

TEST(KeyReferenceTest, QueryTermsRoundTripThroughTheParser) {
    for (int k = C_MAJOR; k <= B_MINOR; ++k) {
        const auto key = static_cast<ChromaticKey>(k);
        const QString query = andykey::searchQueryForKeys({key});
        ASSERT_TRUE(query.startsWith(QStringLiteral("key:")));
        EXPECT_EQ(key, KeyUtils::guessKeyFromText(query.mid(4))) << query.toStdString();
    }
    EXPECT_EQ(QStringLiteral("key:1m | key:2m"),
            andykey::searchQueryForKeys({A_MINOR, E_MINOR}));
}

DeckState deck(bool loaded, bool playing, double volume, int orientation) {
    DeckState state;
    state.loaded = loaded;
    state.playing = playing;
    state.volume = volume;
    state.orientation = orientation;
    return state;
}

TEST(KeyReferenceTest, PicksLoudestPlayingDeck) {
    const QList<DeckState> decks = {
            deck(true, true, 0.5, 0),
            deck(true, true, 0.9, 2),
    };
    EXPECT_EQ(1, andykey::pickReferenceDeck(decks, 0.0));
    // Crossfader hard left silences the right deck.
    EXPECT_EQ(0, andykey::pickReferenceDeck(decks, -1.0));
}

TEST(KeyReferenceTest, PlayingBeatsLouderPausedDeck) {
    const QList<DeckState> decks = {
            deck(true, false, 1.0, 1),
            deck(true, true, 0.2, 1),
    };
    EXPECT_EQ(1, andykey::pickReferenceDeck(decks, 0.0));
}

TEST(KeyReferenceTest, FallsBackToLoudestLoadedDeck) {
    const QList<DeckState> decks = {
            deck(false, false, 1.0, 1),
            deck(true, false, 0.3, 1),
            deck(true, false, 0.8, 1),
    };
    EXPECT_EQ(2, andykey::pickReferenceDeck(decks, 0.0));
    EXPECT_EQ(-1, andykey::pickReferenceDeck({deck(false, false, 1.0, 1)}, 0.0));
    EXPECT_EQ(-1, andykey::pickReferenceDeck({}, 0.0));
}

TEST(KeyReferenceTest, TieGoesToLowerDeck) {
    const QList<DeckState> decks = {
            deck(true, true, 1.0, 1),
            deck(true, true, 1.0, 1),
    };
    EXPECT_EQ(0, andykey::pickReferenceDeck(decks, 0.0));
}

} // namespace
