#include "mixer/autoheadphones.h"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "control/controlobject.h"
#include "test/mixxxtest.h"

namespace {

using DeckState = AutoHeadphones::DeckState;
using Mode = AutoHeadphones::Mode;

DeckState loadedDeck() {
    DeckState state;
    state.trackLoaded = true;
    return state;
}

TEST(AutoHeadphonesRuleTest, FullyUpDeckHasLoudnessOne) {
    EXPECT_DOUBLE_EQ(1.0, AutoHeadphones::loudness(loadedDeck()));
}

TEST(AutoHeadphonesRuleTest, EmptyMutedOrHeadphonesOnlyIsZero) {
    EXPECT_EQ(0.0, AutoHeadphones::loudness(DeckState{}));
    DeckState state = loadedDeck();
    state.muted = true;
    EXPECT_EQ(0.0, AutoHeadphones::loudness(state));
    state = loadedDeck();
    state.mainMix = false;
    EXPECT_EQ(0.0, AutoHeadphones::loudness(state));
}

TEST(AutoHeadphonesRuleTest, FaderAndCrossfaderMultiply) {
    DeckState state = loadedDeck();
    state.volume = 0.5;
    state.crossfaderGain = 0.5;
    EXPECT_DOUBLE_EQ(0.25, AutoHeadphones::loudness(state));
}

TEST(AutoHeadphonesRuleTest, EqCountsAsAverageWithCentreAsOne) {
    DeckState state = loadedDeck();
    state.eqLoaded = true;
    EXPECT_DOUBLE_EQ(1.0, AutoHeadphones::loudness(state));
    state.eqGains = {1.0, 0.0, 0.5};
    EXPECT_DOUBLE_EQ(1.0, AutoHeadphones::loudness(state));
    state.eqKills = {true, false, false};
    EXPECT_DOUBLE_EQ(1.0 / 3.0, AutoHeadphones::loudness(state));
    state.eqLoaded = false;
    EXPECT_DOUBLE_EQ(1.0, AutoHeadphones::loudness(state));
}

TEST(AutoHeadphonesRuleTest, StemsCountAsAverage) {
    DeckState state = loadedDeck();
    state.stemCount = 4;
    state.stemVolumes = {1.0, 1.0, 0.0, 0.0};
    EXPECT_DOUBLE_EQ(0.5, AutoHeadphones::loudness(state));
    state.stemMuted = {false, true, false, false};
    EXPECT_DOUBLE_EQ(0.25, AutoHeadphones::loudness(state));
    state.stemCount = 2;
    EXPECT_DOUBLE_EQ(0.5, AutoHeadphones::loudness(state));
    state.stemCount = 0;
    EXPECT_DOUBLE_EQ(1.0, AutoHeadphones::loudness(state));
}

TEST(AutoHeadphonesRuleTest, QuietestNeedsTwoLoadedDecks) {
    EXPECT_EQ(std::nullopt, AutoHeadphones::chooseQuietest({0.0, std::nullopt}, std::nullopt));
    EXPECT_EQ(std::optional<int>(1), AutoHeadphones::chooseQuietest({1.0, 0.0}, std::nullopt));
    EXPECT_EQ(std::optional<int>(2),
            AutoHeadphones::chooseQuietest({1.0, std::nullopt, 0.2, 0.5}, std::nullopt));
}

TEST(AutoHeadphonesRuleTest, QuietestIsStickyWithinMargin) {
    // Crossover during a fade: no flip until clearly quieter.
    EXPECT_EQ(std::optional<int>(1), AutoHeadphones::chooseQuietest({0.52, 0.55}, 1));
    EXPECT_EQ(std::optional<int>(0), AutoHeadphones::chooseQuietest({0.45, 0.55}, 1));
    // Tie keeps the current pick.
    EXPECT_EQ(std::optional<int>(1), AutoHeadphones::chooseQuietest({0.0, 0.0}, 1));
    // Current pick emptied: fall back to the quietest.
    EXPECT_EQ(std::optional<int>(0),
            AutoHeadphones::chooseQuietest({0.3, std::nullopt, 0.8}, 1));
}

TEST(AutoHeadphonesRuleTest, PressKeepsItsStateAndMatchesAutoWhenItCan) {
    EXPECT_EQ(Mode::ManualOn, AutoHeadphones::modeAfterPress(true, false));
    EXPECT_EQ(Mode::ManualOff, AutoHeadphones::modeAfterPress(false, true));
    EXPECT_EQ(Mode::Auto, AutoHeadphones::modeAfterPress(true, true));
    EXPECT_EQ(Mode::Auto, AutoHeadphones::modeAfterPress(false, false));
}

} // namespace

/// Drives the watcher against real controls for two decks and a preview deck.
class AutoHeadphonesWatcherTest : public MixxxTest {
  protected:
    void SetUp() override {
        add("[Master]", "auto_headphones", 0.0);
        add("[App]", "num_decks", 2.0);
        add("[App]", "num_preview_decks", 1.0);
        add("[PreviewDeck1]", "play", 0.0);
        add("[Master]", "crossfader", 0.0);
        add("[Mixer Profile]", "xFaderCurve", 0.9);
        add("[Mixer Profile]", "xFaderCalibration", 1.0);
        add("[Mixer Profile]", "xFaderMode", 0.0);
        add("[Mixer Profile]", "xFaderReverse", 0.0);
        for (const char* group : {"[Channel1]", "[Channel2]"}) {
            add(group, "track_loaded", 1.0);
            add(group, "main_mix", 1.0);
            add(group, "mute", 0.0);
            add(group, "volume", 1.0);
            add(group, "orientation", 1.0);
            add(group, "pfl", 0.0);
        }
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

    static double pfl1() {
        return ControlObject::get(ConfigKey("[Channel1]", "pfl"));
    }
    static double pfl2() {
        return ControlObject::get(ConfigKey("[Channel2]", "pfl"));
    }

    void poll() {
        m_pWatcher->slotPoll();
    }

    std::vector<std::unique_ptr<ControlObject>> m_controls;
    std::unique_ptr<AutoHeadphones> m_pWatcher;
};

TEST_F(AutoHeadphonesWatcherTest, DoesNothingWhileDisabled) {
    set("[Channel2]", "volume", 0.0);
    poll();
    EXPECT_EQ(0.0, pfl1());
    EXPECT_EQ(0.0, pfl2());
}

TEST_F(AutoHeadphonesWatcherTest, QuietestDeckFollowsAMix) {
    set("[Master]", "auto_headphones", 1.0);
    set("[Channel2]", "volume", 0.0);
    poll();
    EXPECT_EQ(0.0, pfl1());
    EXPECT_EQ(1.0, pfl2());

    // Deck 2 fades in, deck 1 fades out: headphones stay until deck 1 is
    // clearly the quieter one.
    set("[Channel2]", "volume", 0.6);
    set("[Channel1]", "volume", 0.62);
    poll();
    EXPECT_EQ(1.0, pfl2());
    set("[Channel1]", "volume", 0.3);
    poll();
    EXPECT_EQ(1.0, pfl1());
    EXPECT_EQ(0.0, pfl2());
}

TEST_F(AutoHeadphonesWatcherTest, SingleLoadedDeckIsNotCued) {
    set("[Master]", "auto_headphones", 1.0);
    set("[Channel2]", "track_loaded", 0.0);
    set("[Channel1]", "volume", 0.0);
    poll();
    EXPECT_EQ(0.0, pfl1());
}

TEST_F(AutoHeadphonesWatcherTest, PreviewTakesTheHeadphones) {
    set("[Master]", "auto_headphones", 1.0);
    set("[Channel2]", "volume", 0.0);
    poll();
    EXPECT_EQ(1.0, pfl2());
    set("[PreviewDeck1]", "play", 1.0);
    poll();
    EXPECT_EQ(0.0, pfl2());
    set("[PreviewDeck1]", "play", 0.0);
    poll();
    EXPECT_EQ(1.0, pfl2());
}

TEST_F(AutoHeadphonesWatcherTest, ManualOnStaysUntilPressedOff) {
    set("[Master]", "auto_headphones", 1.0);
    set("[Channel2]", "volume", 0.0);
    poll();
    // Andy also puts deck 1 on the headphones.
    set("[Channel1]", "pfl", 1.0);
    poll();
    set("[Channel1]", "volume", 0.5);
    poll();
    EXPECT_EQ(1.0, pfl1());
    EXPECT_EQ(1.0, pfl2());
    // Pressed off: automatic again, deck 1 is not the quietest → stays off.
    set("[Channel1]", "pfl", 0.0);
    poll();
    EXPECT_EQ(0.0, pfl1());
    // Automatic still moves it on when deck 1 becomes the quietest.
    set("[Channel2]", "volume", 1.0);
    poll();
    EXPECT_EQ(1.0, pfl1());
    EXPECT_EQ(0.0, pfl2());
}

TEST_F(AutoHeadphonesWatcherTest, ManualOffStaysUntilPressedOn) {
    set("[Master]", "auto_headphones", 1.0);
    set("[Channel2]", "volume", 0.0);
    poll();
    set("[Channel2]", "pfl", 0.0);
    poll();
    set("[PreviewDeck1]", "play", 1.0);
    poll();
    set("[PreviewDeck1]", "play", 0.0);
    poll();
    EXPECT_EQ(0.0, pfl2()); // still off, even after a preview
    EXPECT_EQ(0.0, pfl1()); // and nobody else gets cued
    // Pressed on while automatic wants it on: automatic again, so it comes
    // off by itself once deck 1 is the quieter one.
    set("[Channel2]", "pfl", 1.0);
    poll();
    set("[Channel2]", "volume", 1.0);
    set("[Channel1]", "volume", 0.2);
    poll();
    EXPECT_EQ(0.0, pfl2());
    EXPECT_EQ(1.0, pfl1());
}

TEST_F(AutoHeadphonesWatcherTest, LoadHandsTheDeckBackToAutomatic) {
    set("[Master]", "auto_headphones", 1.0);
    set("[Channel2]", "volume", 0.0);
    poll();
    set("[Channel2]", "pfl", 0.0);
    poll();
    EXPECT_EQ(0.0, pfl2());
    m_pWatcher->trackLoaded(1);
    poll();
    EXPECT_EQ(1.0, pfl2());
}

TEST_F(AutoHeadphonesWatcherTest, CrossfaderCutMakesTheDeckQuietest) {
    set("[Master]", "auto_headphones", 1.0);
    set("[Channel1]", "orientation", 0.0); // left
    set("[Channel2]", "orientation", 2.0); // right
    set("[Master]", "crossfader", -1.0);   // all the way left
    poll();
    EXPECT_EQ(1.0, pfl2());
    set("[Master]", "crossfader", 1.0);
    poll();
    EXPECT_EQ(1.0, pfl1());
    EXPECT_EQ(0.0, pfl2());
}
