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
        Mode mode = Mode::Auto;
        /// pfl as it was after the last poll; a different value now means the
        /// button was pressed by hand.
        std::optional<bool> lastPfl;
    };
    Memory memory;
};

// static
double AutoHeadphones::loudness(const DeckState& state) {
    if (!state.trackLoaded || !state.mainMix || state.muted) {
        return 0.0;
    }
    double result = state.volume * state.crossfaderGain;
    if (state.eqLoaded) {
        double sum = 0.0;
        for (std::size_t band = 0; band < state.eqGains.size(); ++band) {
            if (!state.eqKills[band]) {
                sum += state.eqGains[band];
            }
        }
        // Average knob position relative to the centre (unity gain).
        result *= sum / state.eqGains.size() / 0.5;
    }
    if (state.stemCount > 0) {
        const int stems = std::min(state.stemCount, mixxx::kMaxSupportedStems);
        double sum = 0.0;
        for (int stem = 0; stem < stems; ++stem) {
            if (!state.stemMuted[stem]) {
                sum += state.stemVolumes[stem];
            }
        }
        result *= sum / stems;
    }
    return result;
}

// static
std::optional<int> AutoHeadphones::chooseQuietest(
        const std::vector<std::optional<double>>& loudness,
        std::optional<int> current) {
    std::optional<int> quietest;
    int loadedDecks = 0;
    for (int deck = 0; deck < static_cast<int>(loudness.size()); ++deck) {
        if (!loudness[deck].has_value()) {
            continue;
        }
        ++loadedDecks;
        if (!quietest.has_value() || *loudness[deck] < *loudness[*quietest]) {
            quietest = deck;
        }
    }
    if (loadedDecks < 2) {
        return std::nullopt;
    }
    if (current.has_value() && *current >= 0 &&
            *current < static_cast<int>(loudness.size()) &&
            loudness[*current].has_value() &&
            *loudness[*quietest] >= *loudness[*current] - kSwitchMargin) {
        return current;
    }
    return quietest;
}

// static
AutoHeadphones::Mode AutoHeadphones::modeAfterPress(bool pflNow, bool autoWantsPfl) {
    if (pflNow == autoWantsPfl) {
        return Mode::Auto;
    }
    return pflNow ? Mode::ManualOn : Mode::ManualOff;
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
                  QStringLiteral("xFaderReverse"))),
          m_numPreviewDecks(optionalControl(QStringLiteral("[App]"),
                  QStringLiteral("num_preview_decks"))) {
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

    if (!m_numPreviewDecks.valid()) {
        m_numPreviewDecks = optionalControl(
                QStringLiteral("[App]"), QStringLiteral("num_preview_decks"));
    }
    const int numPreviewDecks = std::max(0, static_cast<int>(m_numPreviewDecks.get()));
    while (static_cast<int>(m_previewPlay.size()) < numPreviewDecks) {
        m_previewPlay.push_back(optionalControl(
                PlayerManager::groupForPreviewDeck(static_cast<int>(m_previewPlay.size())),
                QStringLiteral("play")));
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
        for (std::size_t i = 0; i < m_previewPlay.size(); ++i) {
            if (!m_previewPlay[i].valid()) {
                m_previewPlay[i] = optionalControl(
                        PlayerManager::groupForPreviewDeck(static_cast<int>(i)),
                        QStringLiteral("play"));
            }
        }
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
        m_autoDeck.reset();
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

    bool previewPlaying = false;
    for (const auto& play : m_previewPlay) {
        if (play.valid() && play.toBool()) {
            previewPlaying = true;
            break;
        }
    }

    std::vector<std::optional<double>> deckLoudness(m_decks.size());
    for (std::size_t deckIndex = 0; deckIndex < m_decks.size(); ++deckIndex) {
        const auto& pDeck = m_decks[deckIndex];
        if (!pDeck->deckValid()) {
            continue;
        }
        const DeckState state = readDeck(*pDeck, leftGain, rightGain);
        if (state.trackLoaded) {
            deckLoudness[deckIndex] = loudness(state);
        }
    }
    m_autoDeck = chooseQuietest(deckLoudness, m_autoDeck);

    for (std::size_t deckIndex = 0; deckIndex < m_decks.size(); ++deckIndex) {
        auto& pDeck = m_decks[deckIndex];
        if (!pDeck->deckValid()) {
            continue;
        }
        auto& memory = pDeck->memory;
        if (!deckLoudness[deckIndex].has_value()) {
            // Empty deck: leave its pfl alone, automatic again on the next load.
            memory = {};
            continue;
        }
        const bool autoWantsPfl = !previewPlaying &&
                m_autoDeck == static_cast<int>(deckIndex);
        bool pflNow = pDeck->pfl.toBool();
        if (memory.lastPfl.has_value() && *memory.lastPfl != pflNow) {
            // Headphone button pressed by hand (skin, keyboard, controller).
            memory.mode = modeAfterPress(pflNow, autoWantsPfl);
        }
        const bool wantPfl = memory.mode == Mode::Auto
                ? autoWantsPfl
                : memory.mode == Mode::ManualOn;
        if (wantPfl != pflNow) {
            pDeck->pfl.set(wantPfl ? 1.0 : 0.0);
            pflNow = wantPfl;
        }
        memory.lastPfl = pflNow;
    }
}

void AutoHeadphones::trackLoaded(int deckIndex) {
    if (deckIndex >= 0 && deckIndex < static_cast<int>(m_decks.size())) {
        m_decks[deckIndex]->memory = {};
    }
}
