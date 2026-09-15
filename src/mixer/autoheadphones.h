#pragma once

#include <QObject>
#include <QTimer>
#include <array>
#include <memory>
#include <optional>
#include <vector>

#include "control/pollingcontrolproxy.h"
#include "engine/engine.h"
#include "util/types.h"

/// "Auto Headphones": while [Master],auto_headphones is on, a main deck that
/// is pulled down on the main output is put on the headphones (its pfl button
/// is switched on), and taken off again once it is brought back up.
///
/// Hysteresis: a deck is cued when a fader or knob goes below 10 % (or it is
/// muted / cut by the crossfader), and only uncued once everything is above
/// 50 % again. In between, the headphones stay as they are.
///
/// The watcher only presses the pfl buttons, it does not touch the audio
/// routing, so skin buttons and controller LEDs always show what is really in
/// the headphones. A pfl button pressed by hand wins: the watcher leaves that
/// deck alone until the next track is loaded into it.
class AutoHeadphones : public QObject {
    Q_OBJECT
  public:
    /// Faders, EQ knobs and stem faders are compared by their position
    /// (0..1, EQ centre = 0.5), the crossfader by the gain of the deck's side.
    /// At or below this a deck is cued.
    static constexpr double kCueThreshold = 0.10;
    /// Above this (everything) a cued deck is uncued. 50 %, minus a hair so a
    /// MIDI knob parked on its centre (63/127 = 0.496) counts as back up.
    static constexpr double kUncueThreshold = 0.49;
    static constexpr int kPollIntervalMs = 50;
    /// Missing optional controls are looked up again every 2 s.
    static constexpr int kRetryOptionalPolls = 40;

    /// Everything that decides whether a deck is heard on the main output.
    /// Plain values so the rule can be tested without a running engine.
    struct DeckState {
        bool trackLoaded = false;
        bool mainMix = true;
        bool muted = false;
        /// Channel fader position.
        double volume = 1.0;
        /// Gain of the crossfader side the deck is assigned to, 1.0 for center.
        double crossfaderGain = 1.0;
        /// Only evaluated when an EQ effect is loaded in the deck's EQ slot.
        bool eqLoaded = false;
        /// Knob positions, 0.5 = neutral.
        std::array<double, 3> eqGains = {0.5, 0.5, 0.5};
        std::array<bool, 3> eqKills = {false, false, false};
        /// 0 for a normal track.
        int stemCount = 0;
        std::array<double, mixxx::kMaxSupportedStems> stemVolumes = {1.0, 1.0, 1.0, 1.0};
        std::array<bool, mixxx::kMaxSupportedStems> stemMuted = {false, false, false, false};
    };

    /// Loaded, and every one of the checks is above `threshold`.
    static bool isAudible(const DeckState& state, double threshold);

    /// Hysteresis. `lastSilent` is the silence state the watcher decided last
    /// time (empty after a load or when the feature was just enabled). A deck
    /// becomes silent at kCueThreshold and audible again only above
    /// kUncueThreshold.
    static bool nextSilent(std::optional<bool> lastSilent, const DeckState& state);

    /// Edge logic: returns the pfl value to write, or nothing when pfl must be
    /// left alone because the silence state did not change.
    static std::optional<bool> pflToWrite(std::optional<bool> lastSilent, bool silent);

    explicit AutoHeadphones(QObject* pParent = nullptr);
    ~AutoHeadphones() override;

    /// A track was loaded into main deck `deckIndex`: drop a manual override
    /// and act afresh on the next poll.
    void trackLoaded(int deckIndex);

  private slots:
    void slotPoll();

  private:
    friend class AutoHeadphonesWatcherTest;

  private:
    struct DeckControls;

    void syncDeckControls();
    DeckState readDeck(const DeckControls& deck,
            double crossfaderLeftGain,
            double crossfaderRightGain) const;

    QTimer m_timer;
    PollingControlProxy m_enabled;
    PollingControlProxy m_numDecks;
    PollingControlProxy m_crossfader;
    PollingControlProxy m_xfaderCurve;
    PollingControlProxy m_xfaderCalibration;
    PollingControlProxy m_xfaderMode;
    PollingControlProxy m_xfaderReverse;
    std::vector<std::unique_ptr<DeckControls>> m_decks;
    int m_pollsSinceRetry = 0;
};
