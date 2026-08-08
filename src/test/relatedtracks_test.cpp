#include <gtest/gtest.h>

#include <QSet>
#include <QString>

#include "library/relatedtracks.h"

namespace {

using namespace mixxx::relatedtracks;

class RelatedTracksTest : public testing::Test {
  protected:
    static bool sameSong(const QString& lhsArtist,
            const QString& lhsTitle,
            const QString& rhsArtist,
            const QString& rhsTitle) {
        const bool result = isSameSong(lhsArtist, lhsTitle, rhsArtist, rhsTitle);
        // The relation has to be symmetric, or which of two tracks happens to
        // be played first would decide whether the other gets marked.
        EXPECT_EQ(result, isSameSong(rhsArtist, rhsTitle, lhsArtist, lhsTitle));
        return result;
    }
};

TEST_F(RelatedTracksTest, NormalizeTitleStripsCaseAccentsAndPunctuation) {
    EXPECT_EQ(QStringLiteral("coracao"), normalizeTitle(QStringLiteral("Coração!")));
    EXPECT_EQ(QStringLiteral("nao vou"), normalizeTitle(QStringLiteral("Não  Vou...")));
    EXPECT_EQ(QStringLiteral("dont stop"), normalizeTitle(QStringLiteral("Don't Stop")));
}

TEST_F(RelatedTracksTest, NormalizeTitleDropsBracketedSections) {
    EXPECT_EQ(QStringLiteral("titanium"),
            normalizeTitle(QStringLiteral("Titanium (feat. Sia)")));
    EXPECT_EQ(QStringLiteral("bella"),
            normalizeTitle(QStringLiteral("Bella [Robin Zouk Remix]")));
    // Nested brackets need more than one pass.
    EXPECT_EQ(QStringLiteral("bella"),
            normalizeTitle(QStringLiteral("Bella (Robin (2024) Remix)")));
}

TEST_F(RelatedTracksTest, NormalizeTitleDropsTrailingVersionWords) {
    EXPECT_EQ(QStringLiteral("bella"), normalizeTitle(QStringLiteral("Bella - Radio Edit")));
    EXPECT_EQ(QStringLiteral("bella"), normalizeTitle(QStringLiteral("Bella Extended Mix")));
    EXPECT_EQ(QStringLiteral("bella"), normalizeTitle(QStringLiteral("Bella - Original")));
}

TEST_F(RelatedTracksTest, NormalizeTitleKeepsLeadingVersionWords) {
    // "Original Sin" must not collapse to "sin": only trailing version words
    // are peeled, and never the last word standing.
    EXPECT_EQ(QStringLiteral("original sin"),
            normalizeTitle(QStringLiteral("Original Sin")));
    EXPECT_EQ(QStringLiteral("live"), normalizeTitle(QStringLiteral("Live")));
    EXPECT_EQ(QStringLiteral("remix"), normalizeTitle(QStringLiteral("Remix")));
}

TEST_F(RelatedTracksTest, NormalizeTitleCutsFeatureTail) {
    EXPECT_EQ(QStringLiteral("titanium"),
            normalizeTitle(QStringLiteral("Titanium feat. Sia")));
    EXPECT_EQ(QStringLiteral("titanium"),
            normalizeTitle(QStringLiteral("Titanium ft Sia & Friends")));
}

TEST_F(RelatedTracksTest, ArtistTokensSplitOnSeparators) {
    EXPECT_EQ(QSet<QString>({QStringLiteral("jorge"), QStringLiteral("mateus")}),
            artistTokens(QStringLiteral("Jorge & Mateus")));
    EXPECT_EQ(QSet<QString>({QStringLiteral("jorge"), QStringLiteral("mateus")}),
            artistTokens(QStringLiteral("Jorge and Mateus")));
    EXPECT_EQ(QSet<QString>({QStringLiteral("jorge"), QStringLiteral("mateus")}),
            artistTokens(QStringLiteral("Jorge, Mateus")));
    EXPECT_EQ(QSet<QString>({QStringLiteral("kaysha"), QStringLiteral("anitta")}),
            artistTokens(QStringLiteral("Kaysha feat. Anitta")));
    EXPECT_EQ(QSet<QString>({QStringLiteral("kaysha"), QStringLiteral("nelson freitas")}),
            artistTokens(QStringLiteral("Kaysha x Nelson Freitas")));
}

TEST_F(RelatedTracksTest, TitlesToleratePartialTypos) {
    // Long enough for one edit.
    EXPECT_TRUE(normalizedTitlesMatch(
            QStringLiteral("beautiful"), QStringLiteral("beautifull")));
    // Short titles have to be exact - one edit away is a different song.
    EXPECT_FALSE(normalizedTitlesMatch(QStringLiteral("mine"), QStringLiteral("mind")));
    EXPECT_FALSE(normalizedTitlesMatch(QStringLiteral("bella"), QStringLiteral("bello")));
    // Two edits are only allowed once a title is properly long.
    EXPECT_TRUE(normalizedTitlesMatch(
            QStringLiteral("nao vou desistir"), QStringLiteral("nao vou desistr")));
    EXPECT_FALSE(normalizedTitlesMatch(QStringLiteral(""), QStringLiteral("")));
}

TEST_F(RelatedTracksTest, RemixOfTheSameSongIsRelated) {
    EXPECT_TRUE(sameSong(QStringLiteral("Kaysha"),
            QStringLiteral("Bella"),
            QStringLiteral("Kaysha"),
            QStringLiteral("Bella (DJ Robin Remix)")));
    EXPECT_TRUE(sameSong(QStringLiteral("Kaysha"),
            QStringLiteral("Bella"),
            QStringLiteral("Kaysha feat. Nelson"),
            QStringLiteral("Bella - Extended Mix")));
}

TEST_F(RelatedTracksTest, DifferentSongsAreNotRelated) {
    EXPECT_FALSE(sameSong(QStringLiteral("Kaysha"),
            QStringLiteral("Bella"),
            QStringLiteral("Kaysha"),
            QStringLiteral("Ciao")));
    // Same title, unrelated artists: a cover is not the track he just played.
    EXPECT_FALSE(sameSong(QStringLiteral("Kaysha"),
            QStringLiteral("Bella"),
            QStringLiteral("Someone Else"),
            QStringLiteral("Bella")));
}

TEST_F(RelatedTracksTest, MissingMetadataNeverMatches) {
    EXPECT_FALSE(sameSong(QStringLiteral(""),
            QStringLiteral("Bella"),
            QStringLiteral(""),
            QStringLiteral("Bella")));
    EXPECT_FALSE(sameSong(QStringLiteral("Kaysha"),
            QStringLiteral(""),
            QStringLiteral("Kaysha"),
            QStringLiteral("")));
    // A title made entirely of version words normalizes to something, but a
    // title made entirely of punctuation does not.
    EXPECT_FALSE(sameSong(QStringLiteral("Kaysha"),
            QStringLiteral("---"),
            QStringLiteral("Kaysha"),
            QStringLiteral("...")));
}

TEST_F(RelatedTracksTest, OptionsDefaultToStemAndTitleOnPlayCountOff) {
    const Options defaults;
    EXPECT_TRUE(defaults.stemCounterpart);
    EXPECT_TRUE(defaults.sameArtistTitle);
    EXPECT_FALSE(defaults.updatePlayCount);
    EXPECT_TRUE(defaults.anyRuleEnabled());

    Options bothOff;
    bothOff.stemCounterpart = false;
    bothOff.sameArtistTitle = false;
    EXPECT_FALSE(bothOff.anyRuleEnabled());
}

} // namespace
