#include "mixer/autoheadphones.h"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "control/controlobject.h"
#include "test/mixxxtest.h"

namespace {

using DeckState = AutoHeadphones::DeckState;

DeckState loadedAudibleDeck() {
    DeckState state;
    state.trackLoaded = true;
    return state;
}

TEST(AutoHeadphonesRuleTest, LoadedDeckWithEverythingUpIsAudible) {
    EXPECT_TRUE(AutoHeadphones::isAudible(loadedAudibleDeck()));
}

TEST(AutoHeadphonesRuleTest, EmptyDeckIsNotAudible) {
    EXPECT_FALSE(AutoHeadphones::isAudible(DeckState{}));
}

TEST(AutoHeadphonesRuleTest, ChannelFaderAtTwoPercentIsSilent) {
    DeckState state = loadedAudibleDeck();
    state.volume = 0.02;
    EXPECT_FALSE(AutoHeadphones::isAudible(state));
    state.volume = 0.03;
    EXPECT_TRUE(AutoHeadphones::isAudible(state));
}

TEST(AutoHeadphonesRuleTest, MuteAndHeadphonesOnlyAreSilent) {
    DeckState state = loadedAudibleDeck();
    state.muted = true;
    EXPECT_FALSE(AutoHeadphones::isAudible(state));
    state = loadedAudibleDeck();
    state.mainMix = false;
    EXPECT_FALSE(AutoHeadphones::isAudible(state));
}

TEST(AutoHeadphonesRuleTest, CrossfaderCutIsSilent) {
    DeckState state = loadedAudibleDeck();
    state.crossfaderGain = 0.0;
    EXPECT_FALSE(AutoHeadphones::isAudible(state));
}

TEST(AutoHeadphonesRuleTest, EqNeedsAllThreeBandsDownOrKilled) {
    DeckState state = loadedAudibleDeck();
    state.eqLoaded = true;
    state.eqGains = {0.0, 0.0, 1.0};
    EXPECT_TRUE(AutoHeadphones::isAudible(state));
    state.eqKills = {false, false, true};
    EXPECT_FALSE(AutoHeadphones::isAudible(state));
    state.eqKills = {false, false, false};
    state.eqGains = {0.01, 0.0, 0.02};
    EXPECT_FALSE(AutoHeadphones::isAudible(state));
}

TEST(AutoHeadphonesRuleTest, EqIgnoredWithoutEqEffect) {
    DeckState state = loadedAudibleDeck();
    state.eqLoaded = false;
    state.eqGains = {0.0, 0.0, 0.0};
    EXPECT_TRUE(AutoHeadphones::isAudible(state));
}

TEST(AutoHeadphonesRuleTest, StemsNeedAllDownOrMuted) {
    DeckState state = loadedAudibleDeck();
    state.stemCount = 4;
    state.stemVolumes = {0.0, 0.0, 0.0, 0.5};
    EXPECT_TRUE(AutoHeadphones::isAudible(state));
    state.stemMuted = {false, false, false, true};
    EXPECT_FALSE(AutoHeadphones::isAudible(state));
}

TEST(AutoHeadphonesRuleTest, StemControlsIgnoredForNormalTracks) {
    DeckState state = loadedAudibleDeck();
    state.stemCount = 0;
    state.stemVolumes = {0.0, 0.0, 0.0, 0.0};
    EXPECT_TRUE(AutoHeadphones::isAudible(state));
}

TEST(AutoHeadphonesRuleTest, StemsBeyondStemCountAreIgnored) {
    DeckState state = loadedAudibleDeck();
    state.stemCount = 2;
    state.stemVolumes = {0.0, 0.0, 1.0, 1.0};
    EXPECT_FALSE(AutoHeadphones::isAudible(state));
}

TEST(AutoHeadphonesRuleTest, PflOnlyWrittenOnChange) {
    EXPECT_EQ(AutoHeadphones::pflToWrite(std::nullopt, true), std::optional<bool>(true));
    EXPECT_EQ(AutoHeadphones::pflToWrite(std::nullopt, false), std::optional<bool>(false));
    EXPECT_EQ(AutoHeadphones::pflToWrite(true, true), std::nullopt);
    EXPECT_EQ(AutoHeadphones::pflToWrite(false, false), std::nullopt);
    EXPECT_EQ(AutoHeadphones::pflToWrite(true, false), std::optional<bool>(false));
    EXPECT_EQ(AutoHeadphones::pflToWrite(false, true), std::optional<bool>(true));
}

} // namespace

/// Drives the watcher against real controls for one deck.
class AutoHeadphonesWatcherTest : public MixxxTest {
  protected:
    void SetUp() override {
        add("[Master]", "auto_headphones", 0.0);
        add("[App]", "num_decks", 1.0);
        add("[Master]", "crossfader", 0.0);
        add("[Mixer Profile]", "xFaderCurve", 0.9);
        add("[Mixer Profile]", "xFaderCalibration", 1.0);
        add("[Mixer Profile]", "xFaderMode", 0.0);
        add("[Mixer Profile]", "xFaderReverse", 0.0);
        add("[Channel1]", "track_loaded", 1.0);
        add("[Channel1]", "main_mix", 1.0);
        add("[Channel1]", "mute", 0.0);
        add("[Channel1]", "volume", 1.0);
        add("[Channel1]", "orientation", 1.0);
        add("[Channel1]", "pfl", 0.0);
        m_pWatcher = std::make_unique<AutoHeadphones>();
        // Only the test drives polls.
        m_pWatcher->m_timer.stop();
    }

    void add(const char* group, const char* item, double value) {
        auto pControl = std::make_unique<ControlObject>(ConfigKey(group, item));
        pControl->set(value);
        m_controls.push_back(std::move(pControl));
    }

    static void set(const char* group, const char* item, double value) {
        ControlObject::set(ConfigKey(group, item), value);
    }

    static double pfl() {
        return ControlObject::get(ConfigKey("[Channel1]", "pfl"));
    }

    void poll() {
        m_pWatcher->slotPoll();
    }

    std::vector<std::unique_ptr<ControlObject>> m_controls;
    std::unique_ptr<AutoHeadphones> m_pWatcher;
};

TEST_F(AutoHeadphonesWatcherTest, DoesNothingWhileDisabled) {
    set("[Channel1]", "volume", 0.0);
    poll();
    EXPECT_EQ(0.0, pfl());
}

TEST_F(AutoHeadphonesWatcherTest, FollowsFaderAndRespectsManualPress) {
    set("[Master]", "auto_headphones", 1.0);
    poll();
    EXPECT_EQ(0.0, pfl()); // audible → off

    set("[Channel1]", "volume", 0.0);
    poll();
    EXPECT_EQ(1.0, pfl()); // silent → cued

    // Andy switches the headphones off by hand: stays off while nothing changes.
    set("[Channel1]", "pfl", 0.0);
    poll();
    EXPECT_EQ(0.0, pfl());

    set("[Channel1]", "volume", 0.5);
    poll();
    EXPECT_EQ(0.0, pfl());
    // Pressed by hand while audible: kept.
    set("[Channel1]", "pfl", 1.0);
    poll();
    EXPECT_EQ(1.0, pfl());

    set("[Channel1]", "volume", 0.0);
    poll();
    set("[Channel1]", "volume", 0.7);
    poll();
    EXPECT_EQ(0.0, pfl()); // faded in → off the headphones
}

TEST_F(AutoHeadphonesWatcherTest, CrossfaderOnTheOtherSideCues) {
    set("[Master]", "auto_headphones", 1.0);
    set("[Channel1]", "orientation", 0.0); // left
    poll();
    EXPECT_EQ(0.0, pfl());
    set("[Master]", "crossfader", 1.0); // all the way right
    poll();
    EXPECT_EQ(1.0, pfl());
    set("[Master]", "crossfader", -1.0);
    poll();
    EXPECT_EQ(0.0, pfl());
}

TEST_F(AutoHeadphonesWatcherTest, EmptyDeckIsLeftAlone) {
    set("[Master]", "auto_headphones", 1.0);
    set("[Channel1]", "track_loaded", 0.0);
    set("[Channel1]", "pfl", 1.0);
    poll();
    EXPECT_EQ(1.0, pfl());
    set("[Channel1]", "pfl", 0.0);
    set("[Channel1]", "volume", 0.0);
    poll();
    EXPECT_EQ(0.0, pfl());
    // Loading into the faded-down deck cues it.
    set("[Channel1]", "track_loaded", 1.0);
    poll();
    EXPECT_EQ(1.0, pfl());
}
