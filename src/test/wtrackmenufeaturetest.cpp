// andy-custom: every WTrackMenu::Feature needs a rule for views backed by a track
// model. A missing case fell into `default` and silently dropped the entry from
// every library/playlist/crate menu in release builds - Find Similar shipped
// invisible in CP95 that way.

#include <gtest/gtest.h>

#include "widget/wtrackmenu.h"

namespace {

TEST(WTrackMenuFeatureTest, EveryFeatureHasATrackModelRule) {
    const auto allCaps = [](TrackModel::Capabilities) { return true; };
    const int all = static_cast<int>(WTrackMenu::Feature::All);
    for (int bit = 0; bit < 31; ++bit) {
        const int flag = 1 << bit;
        if (!(all & flag)) {
            continue;
        }
        const auto feature = static_cast<WTrackMenu::Feature>(flag);
        // UpdateReplayGainFromPregain is deck-only and guarded before the rule
        // is ever consulted (it needs a deck group).
        if (feature == WTrackMenu::Feature::UpdateReplayGainFromPregain) {
            continue;
        }
        EXPECT_TRUE(WTrackMenu::featureEnabledForTrackModel(feature, allCaps, true, true)
                            .has_value())
                << "Feature bit " << bit << " has no rule in featureEnabledForTrackModel()";
    }
}

/// The CP95 bug itself: Find Similar must show up in the library table.
TEST(WTrackMenuFeatureTest, FindSimilarIsOfferedInLibraryViews) {
    const auto noCaps = [](TrackModel::Capabilities) { return false; };
    const auto enabled = WTrackMenu::featureEnabledForTrackModel(
            WTrackMenu::Feature::FindSimilar, noCaps, true, true);
    ASSERT_TRUE(enabled.has_value());
    EXPECT_TRUE(*enabled);
}

/// A library row has no play position to transfer - the swap is deck-only.
TEST(WTrackMenuFeatureTest, SwapWithStemIsNotOfferedInLibraryViews) {
    const auto allCaps = [](TrackModel::Capabilities) { return true; };
    const auto enabled = WTrackMenu::featureEnabledForTrackModel(
            WTrackMenu::Feature::SwapWithStem, allCaps, true, true);
    ASSERT_TRUE(enabled.has_value());
    EXPECT_FALSE(*enabled);
}

} // namespace
