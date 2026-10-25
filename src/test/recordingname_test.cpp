#include "recording/recordingname.h"

#include <gtest/gtest.h>

using namespace recordingname;

TEST(RecordingNameTest, CamelCasesPlainTitles) {
    EXPECT_EQ(QStringLiteral("BlindingLights"), camelCaseTitle(QStringLiteral("Blinding Lights")));
    EXPECT_EQ(QStringLiteral("TalkToMeNice"), camelCaseTitle(QStringLiteral("talk to me nice")));
    // The rest of a word is kept as it is.
    EXPECT_EQ(QStringLiteral("JUMANJI"), camelCaseTitle(QStringLiteral("JUMANJI")));
    EXPECT_EQ(QStringLiteral("2AM"), camelCaseTitle(QStringLiteral("2AM")));
}

TEST(RecordingNameTest, StripsAccentsAndPunctuation) {
    EXPECT_EQ(QStringLiteral("DeixaEuFalarPraVoce"),
            camelCaseTitle(QStringLiteral("Deixa Eu Falar Pra Você")));
    EXPECT_EQ(QStringLiteral("FroekenReykjavik"),
            camelCaseTitle(QStringLiteral("Froeken Reykjavík")));
    EXPECT_EQ(QStringLiteral("DontCallMeUp"), camelCaseTitle(QStringLiteral("Don't Call Me Up")));
    EXPECT_EQ(QStringLiteral("LocoContigo"), camelCaseTitle(QStringLiteral("Loco Contigo!")));
}

TEST(RecordingNameTest, DropsFeaturingAndVersionParts) {
    EXPECT_EQ(QStringLiteral("CheapThrills"),
            camelCaseTitle(QStringLiteral("Cheap Thrills (feat. Sean Paul)")));
    EXPECT_EQ(QStringLiteral("Bailando"),
            camelCaseTitle(QStringLiteral("Bailando feat. Descemer Bueno")));
    EXPECT_EQ(QStringLiteral("Glue"), camelCaseTitle(QStringLiteral("Glue [Zouk Remix]")));
    EXPECT_EQ(QStringLiteral("Breathe"), camelCaseTitle(QStringLiteral("Breathe - Kizomba Remix")));
    EXPECT_EQ(QStringLiteral("SummerRain"),
            camelCaseTitle(QStringLiteral("Summer Rain - Radio Edit")));
    // "with" inside a title is a word, not a featuring marker.
    EXPECT_EQ(QStringLiteral("DanceWithMe"), camelCaseTitle(QStringLiteral("Dance With Me")));
    // A plain dash part that is not a version stays.
    EXPECT_EQ(QStringLiteral("RedSunLookUp"), camelCaseTitle(QStringLiteral("Red Sun - Look Up")));
}

TEST(RecordingNameTest, BuildsAndysConvention) {
    EXPECT_EQ(QStringLiteral("2025-10-24_01h08m47s_Zouk_X__Glue_Deixa_ArabicDream_Breathe"),
            buildBaseName(QStringLiteral("2025-10-24_01h08m47s"),
                    true,
                    {QStringLiteral("Glue"),
                            QStringLiteral("Deixa"),
                            QStringLiteral("Arabic Dream"),
                            QStringLiteral("Breathe (feat. X)")}));
    EXPECT_EQ(QStringLiteral("2025-08-15_11h47m38s_Fusion_X__Volare_MrBoombastic"),
            buildBaseName(QStringLiteral("2025-08-15_11h47m38s"),
                    false,
                    {QStringLiteral("Volare"), QStringLiteral("Mr. Boombastic")}));
}

TEST(RecordingNameTest, SkipsEmptyTitlesAndCutsAtWholeTitles) {
    EXPECT_EQ(QStringLiteral("D_Zouk_X__One_Two"),
            buildBaseName(QStringLiteral("D"),
                    true,
                    {QStringLiteral("One"), QStringLiteral("(intro)"), QStringLiteral("Two")}));
    // "D_Zouk_X__One_Two" is 17 chars; "_Three" would make 23.
    EXPECT_EQ(QStringLiteral("D_Zouk_X__One_Two"),
            buildBaseName(QStringLiteral("D"),
                    true,
                    {QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Three")},
                    20));
}
