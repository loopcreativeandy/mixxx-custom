#include "mixer/autoheadphones.h"

#include <algorithm>

#include "engine/channels/enginechannel.h"
#include "engine/enginexfader.h"
#include "mixer/playermanager.h"
#include "moc_autoheadphones.cpp"

namespace {

const QString kMasterGroup = QStringLiteral("[Master]");

PollingControlProxy optionalControl(const QString& group, const QString& item) {
    // Quiet lookup: missing controls are expected during startup and are
    // simply looked up again later.
    return PollingControlProxy(ConfigKey(group, item),
            ControlFlag::AllowInvalidKey | ControlFlag::NoWarnIfMissing);
}

/// "[Channel1]" → "[Channel1_Stem1]". PlayerManager::groupForDeckStem() only
/// exists in builds with stem support.
QString stemGroup(const QString& deckGroup, int stemIdx) {
    return deckGroup.chopped(1) + QStringLiteral("_Stem") + QString::number(stemIdx + 1) +
            QChar(']');
}

} // namespace

/// Proxies for one main deck. Decks, their EQ slots and their stem groups are
/// created at different moments during startup (and when the skin adds decks),
/// so a missing control is looked up again on the next poll until all exist.
struct AutoHeadphones::DeckControls {
    explicit DeckControls(int deckIndex)
            : group(PlayerManager::groupForDeck(deckIndex)),
              eqGroup(QStringLiteral("[EqualizerRack1_%1_Effect1]").arg(group)),
              trackLoaded(optionalControl(group, QStringLiteral("track_loaded"))),
              mainMix(optionalControl(group, QStringLiteral("main_mix"))),
              mute(optionalControl(group, QStringLiteral("mute"))),
              volume(optionalControl(group, QStringLiteral("volume"))),
              orientation(optionalControl(group, QStringLiteral("orientation"))),
              pfl(optionalControl(group, QStringLiteral("pfl"))),
              stemCount(optionalControl(group, QStringLiteral("stem_count"))),
              eqLoaded(optionalControl(eqGroup, QStringLiteral("loaded"))),
              eqParameters{optionalControl(eqGroup, QStringLiteral("parameter1")),
                      optionalControl(eqGroup, QStringLiteral("parameter2")),
                      optionalControl(eqGroup, QStringLiteral("parameter3"))},
              eqKills{optionalControl(eqGroup, QStringLiteral("button_parameter1")),
                      optionalControl(eqGroup, QStringLiteral("button_parameter2")),
                      optionalControl(eqGroup, QStringLiteral("button_parameter3"))},
              stemVolumes{optionalControl(stemGroup(group, 0),
                                  QStringLiteral("volume")),
                      optionalControl(stemGroup(group, 1),
                              QStringLiteral("volume")),
                      optionalControl(stemGroup(group, 2),
                              QStringLiteral("volume")),
                      optionalControl(stemGroup(group, 3),
                              QStringLiteral("volume"))},
              stemMutes{optionalControl(stemGroup(group, 0),
                                QStringLiteral("mute")),
                      optionalControl(stemGroup(group, 1),
                              QStringLiteral("mute")),
                      optionalControl(stemGroup(group, 2),
                              QStringLiteral("mute")),
                      optionalControl(stemGroup(group, 3),
                              QStringLiteral("mute"))} {
    }

    /// The deck itself exists. EQ and stem controls are optional: without an
    /// EQ effect or without stem support those checks are skipped.
    bool deckValid() const {
        return trackLoaded.valid() && mainMix.valid() && mute.valid() &&
                volume.valid() && orientation.valid() && pfl.valid();
    }

    bool allValid() const {
        if (!deckValid() || !eqLoaded.valid()) {
            return false;
        }
        for (const auto& control : eqParameters) {
            if (!control.valid()) {
                return false;
            }
        }
#ifdef __STEM__
        if (!stemCount.valid()) {
            return false;
        }
        for (const auto& control : stemVolumes) {
            if (!control.valid()) {
                return false;
            }
        }
#endif
        return true;
    }

    QString group;
    QString eqGroup;
    PollingControlProxy trackLoaded;
    PollingControlProxy mainMix;
    PollingControlProxy mute;
    PollingControlProxy volume;
    PollingControlProxy orientation;
    PollingControlProxy pfl;
    PollingControlProxy stemCount;
    PollingControlProxy eqLoaded;
    std::array<PollingControlProxy, 3> eqParameters;
    std::array<PollingControlProxy, 3> eqKills;
    std::array<PollingControlProxy, mixxx::kMaxSupportedStems> stemVolumes;
    std::array<PollingControlProxy, mixxx::kMaxSupportedStems> stemMutes;

    /// What the watcher remembers about this deck. Kept when the controls are
    /// looked up again, dropped on load and when the feature is switched off.
    struct Memory {
        /// Silence state decided last; empty = act on the next poll.
        std::optional<bool> lastSilent;
        /// pfl as it was after the last poll; a different value now means the
        /// button was pressed by hand.
        std::optional<bool> lastPfl;
        /// Pressed by hand: leave pfl alone until the next load.
        bool manualOverride = false;
    };
    Memory memory;
};

// static
bool AutoHeadphones::isAudible(const DeckState& state, double threshold) {
    if (!state.trackLoaded || !state.mainMix || state.muted) {
        return false;
    }
    if (state.volume <= threshold) {
        return false;
    }
    if (state.crossfaderGain <= threshold) {
        return false;
    }
    if (state.eqLoaded) {
        bool allBandsSilent = true;
        for (std::size_t band = 0; band < state.eqGains.size(); ++band) {
            if (!state.eqKills[band] && state.eqGains[band] > threshold) {
                allBandsSilent = false;
                break;
            }
        }
        if (allBandsSilent) {
            return false;
        }
    }
    if (state.stemCount > 0) {
        const int stems = std::min(state.stemCount, mixxx::kMaxSupportedStems);
        bool allStemsSilent = true;
        for (int stem = 0; stem < stems; ++stem) {
            if (!state.stemMuted[stem] && state.stemVolumes[stem] > threshold) {
                allStemsSilent = false;
                break;
            }
        }
        if (allStemsSilent) {
            return false;
        }
    }
    return true;
}

// static
bool AutoHeadphones::nextSilent(std::optional<bool> lastSilent, const DeckState& state) {
    if (lastSilent.value_or(false)) {
        // Cued: stays cued until everything is back above 50 %.
        return !isAudible(state, kUncueThreshold);
    }
    return !isAudible(state, kCueThreshold);
}

// static
std::optional<bool> AutoHeadphones::pflToWrite(std::optional<bool> lastSilent, bool silent) {
    if (lastSilent.has_value() && *lastSilent == silent) {
        // Nothing changed: leave a hand-pressed pfl button alone.
        return std::nullopt;
    }
    return silent;
}

AutoHeadphones::AutoHeadphones(QObject* pParent)
        : QObject(pParent),
          m_enabled(optionalControl(kMasterGroup, QStringLiteral("auto_headphones"))),
          m_numDecks(optionalControl(QStringLiteral("[App]"), QStringLiteral("num_decks"))),
          m_crossfader(optionalControl(kMasterGroup, QStringLiteral("crossfader"))),
          m_xfaderCurve(optionalControl(QString(EngineXfader::kXfaderConfigKey),
                  QStringLiteral("xFaderCurve"))),
          m_xfaderCalibration(optionalControl(QString(EngineXfader::kXfaderConfigKey),
                  QStringLiteral("xFaderCalibration"))),
          m_xfaderMode(optionalControl(QString(EngineXfader::kXfaderConfigKey),
                  QStringLiteral("xFaderMode"))),
          m_xfaderReverse(optionalControl(QString(EngineXfader::kXfaderConfigKey),
                  QStringLiteral("xFaderReverse"))) {
    connect(&m_timer, &QTimer::timeout, this, &AutoHeadphones::slotPoll);
    m_timer.start(kPollIntervalMs);
}

AutoHeadphones::~AutoHeadphones() = default;

void AutoHeadphones::syncDeckControls() {
    // The engine and the mixer controls are created after PlayerManager.
    if (!m_enabled.valid()) {
        m_enabled = optionalControl(kMasterGroup, QStringLiteral("auto_headphones"));
    }
    if (!m_numDecks.valid()) {
        m_numDecks = optionalControl(QStringLiteral("[App]"), QStringLiteral("num_decks"));
    }
    if (!m_crossfader.valid()) {
        m_crossfader = optionalControl(kMasterGroup, QStringLiteral("crossfader"));
        const QString xfaderGroup(EngineXfader::kXfaderConfigKey);
        m_xfaderCurve = optionalControl(xfaderGroup, QStringLiteral("xFaderCurve"));
        m_xfaderCalibration = optionalControl(xfaderGroup, QStringLiteral("xFaderCalibration"));
        m_xfaderMode = optionalControl(xfaderGroup, QStringLiteral("xFaderMode"));
        m_xfaderReverse = optionalControl(xfaderGroup, QStringLiteral("xFaderReverse"));
    }

    const int numDecks = std::max(0, static_cast<int>(m_numDecks.get()));
    while (static_cast<int>(m_decks.size()) < numDecks) {
        m_decks.push_back(std::make_unique<DeckControls>(static_cast<int>(m_decks.size())));
    }
    // A deck that is not there yet is looked up on every poll. Optional
    // controls (no EQ effect configured, no stem support) may never appear, so
    // those lookups are only retried every few seconds.
    const bool retryOptional = ++m_pollsSinceRetry >= kRetryOptionalPolls;
    if (retryOptional) {
        m_pollsSinceRetry = 0;
    }
    for (std::size_t deckIndex = 0; deckIndex < m_decks.size(); ++deckIndex) {
        auto& pDeck = m_decks[deckIndex];
        if (pDeck->deckValid() && (!retryOptional || pDeck->allValid())) {
            continue;
        }
        // Re-resolve, keeping what the watcher last did for this deck.
        const auto memory = pDeck->memory;
        pDeck = std::make_unique<DeckControls>(static_cast<int>(deckIndex));
        pDeck->memory = memory;
    }
}

AutoHeadphones::DeckState AutoHeadphones::readDeck(const DeckControls& deck,
        double crossfaderLeftGain,
        double crossfaderRightGain) const {
    DeckState state;
    state.trackLoaded = deck.trackLoaded.toBool();
    state.mainMix = deck.mainMix.toBool();
    state.muted = deck.mute.toBool();
    // Positions, not gains: the channel fader has an audio taper, the EQ
    // knobs a logarithmic scale with unity gain in the centre.
    state.volume = deck.volume.getParameter();
    switch (static_cast<int>(deck.orientation.get())) {
    case EngineChannel::LEFT:
        state.crossfaderGain = crossfaderLeftGain;
        break;
    case EngineChannel::RIGHT:
        state.crossfaderGain = crossfaderRightGain;
        break;
    default:
        state.crossfaderGain = 1.0;
        break;
    }
    state.eqLoaded = deck.eqLoaded.valid() && deck.eqLoaded.toBool();
    for (std::size_t band = 0; band < state.eqGains.size(); ++band) {
        state.eqGains[band] = deck.eqParameters[band].valid()
                ? deck.eqParameters[band].getParameter()
                : 0.5;
        state.eqKills[band] = deck.eqKills[band].valid() && deck.eqKills[band].toBool();
    }
#ifdef __STEM__
    state.stemCount = deck.stemCount.valid() ? static_cast<int>(deck.stemCount.get()) : 0;
    for (int stem = 0; stem < mixxx::kMaxSupportedStems; ++stem) {
        state.stemVolumes[stem] = deck.stemVolumes[stem].valid()
                ? deck.stemVolumes[stem].getParameter()
                : 1.0;
        state.stemMuted[stem] = deck.stemMutes[stem].valid() && deck.stemMutes[stem].toBool();
    }
#endif
    return state;
}

void AutoHeadphones::slotPoll() {
    syncDeckControls();

    if (!m_enabled.toBool()) {
        // Start from scratch when it is switched on again.
        for (auto& pDeck : m_decks) {
            pDeck->memory = {};
        }
        return;
    }

    // Same curve the engine uses, so a sharp crossfader curve that cuts a deck
    // before the end of the fader counts as silent.
    CSAMPLE_GAIN leftGain = 1.0f;
    CSAMPLE_GAIN rightGain = 1.0f;
    if (m_crossfader.valid()) {
        EngineXfader::getXfadeGains(m_crossfader.get(),
                m_xfaderCurve.get(),
                m_xfaderCalibration.get(),
                m_xfaderMode.get(),
                m_xfaderReverse.toBool(),
                &leftGain,
                &rightGain);
    }

    for (auto& pDeck : m_decks) {
        if (!pDeck->deckValid()) {
            continue;
        }
        const DeckState state = readDeck(*pDeck, leftGain, rightGain);
        auto& memory = pDeck->memory;
        if (!state.trackLoaded) {
            // Empty deck: leave its pfl alone, and act afresh on the next load.
            memory = {};
            continue;
        }
        const bool pflNow = pDeck->pfl.toBool();
        if (memory.lastPfl.has_value() && *memory.lastPfl != pflNow) {
            // Headphone button pressed by hand (skin, keyboard, controller).
            memory.manualOverride = true;
        }
        memory.lastPfl = pflNow;
        if (memory.manualOverride) {
            continue;
        }
        const bool silent = nextSilent(memory.lastSilent, state);
        const auto pfl = pflToWrite(memory.lastSilent, silent);
        memory.lastSilent = silent;
        if (pfl.has_value() && pflNow != *pfl) {
            pDeck->pfl.set(*pfl ? 1.0 : 0.0);
            memory.lastPfl = *pfl;
        }
    }
}

void AutoHeadphones::trackLoaded(int deckIndex) {
    if (deckIndex >= 0 && deckIndex < static_cast<int>(m_decks.size())) {
        m_decks[deckIndex]->memory = {};
    }
}
