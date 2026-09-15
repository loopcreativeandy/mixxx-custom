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

constexpr double kCue = AutoHeadphones::kCueThreshold;

TEST(AutoHeadphonesRuleTest, LoadedDeckWithEverythingUpIsAudible) {
    EXPECT_TRUE(AutoHeadphones::isAudible(loadedAudibleDeck(), kCue));
    EXPECT_TRUE(AutoHeadphones::isAudible(loadedAudibleDeck(),
            AutoHeadphones::kUncueThreshold));
}

TEST(AutoHeadphonesRuleTest, EmptyDeckIsNotAudible) {
    EXPECT_FALSE(AutoHeadphones::isAudible(DeckState{}, kCue));
}

TEST(AutoHeadphonesRuleTest, ChannelFaderAtTenPercentIsSilent) {
    DeckState state = loadedAudibleDeck();
    state.volume = 0.10;
    EXPECT_FALSE(AutoHeadphones::isAudible(state, kCue));
    state.volume = 0.11;
    EXPECT_TRUE(AutoHeadphones::isAudible(state, kCue));
}

TEST(AutoHeadphonesRuleTest, MuteAndHeadphonesOnlyAreSilent) {
    DeckState state = loadedAudibleDeck();
    state.muted = true;
    EXPECT_FALSE(AutoHeadphones::isAudible(state, kCue));
    state = loadedAudibleDeck();
    state.mainMix = false;
    EXPECT_FALSE(AutoHeadphones::isAudible(state, kCue));
}

TEST(AutoHeadphonesRuleTest, CrossfaderCutIsSilent) {
    DeckState state = loadedAudibleDeck();
    state.crossfaderGain = 0.0;
    EXPECT_FALSE(AutoHeadphones::isAudible(state, kCue));
}

TEST(AutoHeadphonesRuleTest, EqNeedsAllThreeBandsDownOrKilled) {
    DeckState state = loadedAudibleDeck();
    state.eqLoaded = true;
    state.eqGains = {0.0, 0.0, 0.5};
    EXPECT_TRUE(AutoHeadphones::isAudible(state, kCue));
    state.eqKills = {false, false, true};
    EXPECT_FALSE(AutoHeadphones::isAudible(state, kCue));
    state.eqKills = {false, false, false};
    state.eqGains = {0.05, 0.0, 0.10};
    EXPECT_FALSE(AutoHeadphones::isAudible(state, kCue));
}

TEST(AutoHeadphonesRuleTest, EqIgnoredWithoutEqEffect) {
    DeckState state = loadedAudibleDeck();
    state.eqLoaded = false;
    state.eqGains = {0.0, 0.0, 0.0};
    EXPECT_TRUE(AutoHeadphones::isAudible(state, kCue));
}

TEST(AutoHeadphonesRuleTest, StemsNeedAllDownOrMuted) {
    DeckState state = loadedAudibleDeck();
    state.stemCount = 4;
    state.stemVolumes = {0.0, 0.0, 0.0, 0.5};
    EXPECT_TRUE(AutoHeadphones::isAudible(state, kCue));
    state.stemMuted = {false, false, false, true};
    EXPECT_FALSE(AutoHeadphones::isAudible(state, kCue));
}

TEST(AutoHeadphonesRuleTest, StemControlsIgnoredForNormalTracks) {
    DeckState state = loadedAudibleDeck();
    state.stemCount = 0;
    state.stemVolumes = {0.0, 0.0, 0.0, 0.0};
    EXPECT_TRUE(AutoHeadphones::isAudible(state, kCue));
}

TEST(AutoHeadphonesRuleTest, StemsBeyondStemCountAreIgnored) {
    DeckState state = loadedAudibleDeck();
    state.stemCount = 2;
    state.stemVolumes = {0.0, 0.0, 1.0, 1.0};
    EXPECT_FALSE(AutoHeadphones::isAudible(state, kCue));
}

TEST(AutoHeadphonesRuleTest, HysteresisCuesBelowTenUncuesAboveFifty) {
    DeckState state = loadedAudibleDeck();
    state.volume = 0.3;
    EXPECT_FALSE(AutoHeadphones::nextSilent(std::nullopt, state));
    EXPECT_FALSE(AutoHeadphones::nextSilent(false, state));
    // Once cued, 30 % is not enough to come back.
    EXPECT_TRUE(AutoHeadphones::nextSilent(true, state));
    state.volume = 0.08;
    EXPECT_TRUE(AutoHeadphones::nextSilent(false, state));
    state.volume = 0.51;
    EXPECT_FALSE(AutoHeadphones::nextSilent(true, state));
}

TEST(AutoHeadphonesRuleTest, EqKnobOnMidiCentreCountsAsBackUp) {
    DeckState state = loadedAudibleDeck();
    state.eqLoaded = true;
    state.eqGains = {63.0 / 127.0, 0.0, 0.0};
    EXPECT_FALSE(AutoHeadphones::nextSilent(true, state));
    state.eqGains = {0.4, 0.4, 0.4};
    EXPECT_TRUE(AutoHeadphones::nextSilent(true, state));
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

TEST_F(AutoHeadphonesWatcherTest, FollowsFaderWithHysteresis) {
    set("[Master]", "auto_headphones", 1.0);
    poll();
    EXPECT_EQ(0.0, pfl()); // audible → off

    set("[Channel1]", "volume", 0.3);
    poll();
    EXPECT_EQ(0.0, pfl()); // 30 % is still up

    set("[Channel1]", "volume", 0.05);
    poll();
    EXPECT_EQ(1.0, pfl()); // below 10 % → cued

    set("[Channel1]", "volume", 0.3);
    poll();
    EXPECT_EQ(1.0, pfl()); // fading in, not above 50 % yet

    set("[Channel1]", "volume", 0.6);
    poll();
    EXPECT_EQ(0.0, pfl()); // above 50 % → off the headphones
}

TEST_F(AutoHeadphonesWatcherTest, ManualPressWinsUntilNextLoad) {
    set("[Master]", "auto_headphones", 1.0);
    set("[Channel1]", "volume", 0.0);
    poll();
    EXPECT_EQ(1.0, pfl()); // cued

    // Andy switches the headphones off by hand: stays off.
    set("[Channel1]", "pfl", 0.0);
    poll();
    EXPECT_EQ(0.0, pfl());
    set("[Channel1]", "volume", 1.0);
    poll();
    set("[Channel1]", "volume", 0.0);
    poll();
    EXPECT_EQ(0.0, pfl()); // pulled down again, override still holds

    // Pressed on by hand, then faded in: stays on.
    set("[Channel1]", "pfl", 1.0);
    poll();
    set("[Channel1]", "volume", 1.0);
    poll();
    EXPECT_EQ(1.0, pfl());

    // Next track loaded: the watcher takes over again.
    m_pWatcher->trackLoaded(0);
    poll();
    EXPECT_EQ(0.0, pfl());
    set("[Channel1]", "volume", 0.0);
    poll();
    EXPECT_EQ(1.0, pfl());
}

TEST_F(AutoHeadphonesWatcherTest, SwitchingOffAndOnClearsOverride) {
    set("[Master]", "auto_headphones", 1.0);
    poll();
    set("[Channel1]", "pfl", 1.0);
    poll();
    set("[Master]", "auto_headphones", 0.0);
    poll();
    set("[Master]", "auto_headphones", 1.0);
    poll();
    EXPECT_EQ(0.0, pfl());
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
