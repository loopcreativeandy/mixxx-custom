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

/// "Auto Headphones": while [Master],auto_headphones is on, every main deck
/// that is loaded but silent on the main output is put on the headphones (its
/// pfl button is switched on), and taken off again the moment it becomes
/// audible.
///
/// The watcher only presses the pfl buttons, it does not touch the audio
/// routing, so skin buttons and controller LEDs always show what is really in
/// the headphones. It acts on edges only: a deck's pfl is set when the deck
/// changes between silent and audible (or when a track is loaded / the feature
/// is switched on). A pfl button pressed by hand therefore stays as it is until
/// the next such change.
class AutoHeadphones : public QObject {
    Q_OBJECT
  public:
    /// Silence threshold for faders, EQ knobs, stem faders and the crossfader
    /// gain. Same value as AUDIBLE_EPS in Andy's controller mapping, so the
    /// controller's "live" LED and the headphones agree.
    static constexpr double kSilentEpsilon = 0.02;
    static constexpr int kPollIntervalMs = 50;
    /// Missing optional controls are looked up again every 2 s.
    static constexpr int kRetryOptionalPolls = 40;

    /// Everything that decides whether a deck is heard on the main output.
    /// Plain values so the rule can be tested without a running engine.
    struct DeckState {
        bool trackLoaded = false;
        bool mainMix = true;
        bool muted = false;
        double volume = 1.0;
        /// Gain of the crossfader side the deck is assigned to, 1.0 for center.
        double crossfaderGain = 1.0;
        /// Only evaluated when an EQ effect is loaded in the deck's EQ slot.
        bool eqLoaded = false;
        std::array<double, 3> eqGains = {1.0, 1.0, 1.0};
        std::array<bool, 3> eqKills = {false, false, false};
        /// 0 for a normal track.
        int stemCount = 0;
        std::array<double, mixxx::kMaxSupportedStems> stemVolumes = {1.0, 1.0, 1.0, 1.0};
        std::array<bool, mixxx::kMaxSupportedStems> stemMuted = {false, false, false, false};
    };

    /// Loaded, and every one of the checks lets sound through.
    static bool isAudible(const DeckState& state);

    /// Edge logic. `lastSilent` is the silence state the watcher acted on last
    /// time (empty after a load or when the feature was just enabled). Returns
    /// the pfl value to write, or nothing when pfl must be left alone.
    static std::optional<bool> pflToWrite(std::optional<bool> lastSilent, bool silent);

    explicit AutoHeadphones(QObject* pParent = nullptr);
    ~AutoHeadphones() override;

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
