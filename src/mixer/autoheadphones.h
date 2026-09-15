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

/// "Auto Headphones": while [Master],auto_headphones is on, the quietest
/// loaded main deck is on the headphones (its pfl button is switched on) and
/// the other decks are not. Andy's rule, CP89d:
///
/// - Quietest = lowest loudness(): channel fader position x crossfader side
///   gain x average EQ knob position x average stem fader position. The
///   headphones only move to another deck once it is clearly quieter
///   (kSwitchMargin), so two decks at about the same level do not flip-flop.
///   Playing or stopping a deck does not matter.
/// - Needs at least two loaded main decks; with one, nothing is cued.
/// - While a preview deck plays, no main deck is cued automatically. When the
///   preview stops, the quietest deck goes back on.
/// - A hand press always wins and keeps the state it set: if that is what the
///   automatic rule would do anyway the deck goes back to automatic, otherwise
///   it stays manual (on or off) until the next press. Loading a track into
///   the deck or switching the feature off hands it back to automatic too.
///
/// The watcher only presses the pfl buttons, it does not touch the audio
/// routing, so skin buttons and controller LEDs always show what is really in
/// the headphones.
class AutoHeadphones : public QObject {
    Q_OBJECT
  public:
    /// Loudness difference needed before the headphones move to another deck.
    static constexpr double kSwitchMargin = 0.05;
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

    /// What a deck's headphone button follows.
    enum class Mode {
        Auto,
        ManualOn,
        ManualOff,
    };

    /// How loud the deck is on the main output; 0 = not heard (also empty,
    /// muted or not routed to main). EQ at centre counts as 1, boosts above.
    static double loudness(const DeckState& state);

    /// Picks the deck for the headphones. `loudness` has one entry per main
    /// deck, empty for an empty deck; `current` is the previous pick. Keeps
    /// `current` unless another deck is quieter by more than kSwitchMargin.
    static std::optional<int> chooseQuietest(
            const std::vector<std::optional<double>>& loudness,
            std::optional<int> current);

    /// Mode after the button was pressed by hand to `pflNow`, given whether
    /// the automatic rule wants the deck on the headphones right now.
    static Mode modeAfterPress(bool pflNow, bool autoWantsPfl);

    explicit AutoHeadphones(QObject* pParent = nullptr);
    ~AutoHeadphones() override;

    /// A track was loaded into main deck `deckIndex`: back to automatic.
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
    PollingControlProxy m_numPreviewDecks;
    std::vector<PollingControlProxy> m_previewPlay;
    /// Deck the automatic rule currently puts on the headphones.
    std::optional<int> m_autoDeck;
    int m_pollsSinceRetry = 0;
};
