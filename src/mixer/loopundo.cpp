#include "mixer/loopundo.h"

#include <QLabel>
#include <QStringList>
#include <QWidget>
#include <algorithm>

#include "control/controlproxy.h"
#include "control/controlpushbutton.h"
#include "moc_loopundo.cpp"
#include "track/track.h"

namespace {

constexpr double kNoPosition = -1.0;
constexpr int kToastMs = 1500;

bool isValidPosition(double position) {
    return position >= 0.0;
}

// The cues undo covers: hotcues and saved loops on a hotcue slot, intro and
// outro. Not the main cue (CUE sets it all the time) or hidden ones.
bool isUndoableCue(const Cue& cue) {
    switch (cue.getType()) {
    case mixxx::CueType::Intro:
    case mixxx::CueType::Outro:
        return true;
    case mixxx::CueType::HotCue:
    case mixxx::CueType::Loop:
        return cue.getHotCue() != Cue::kNoHotCue;
    default:
        return false;
    }
}

QString cueName(const LoopUndo::CueState& cue) {
    switch (cue.type) {
    case mixxx::CueType::Intro:
        return LoopUndo::tr("intro marker");
    case mixxx::CueType::Outro:
        return LoopUndo::tr("outro marker");
    default:
        return LoopUndo::tr("hotcue %1").arg(cue.hotCue + 1);
    }
}

const LoopUndo::CueState* findCue(
        const LoopUndo::CueStates& cues, const CuePointer& pCue) {
    for (const auto& cue : cues) {
        if (cue.pCue == pCue) {
            return &cue;
        }
    }
    return nullptr;
}

} // namespace

bool LoopUndo::CueState::sameValues(const CueState& other) const {
    return type == other.type && hotCue == other.hotCue &&
            start == other.start && end == other.end &&
            label == other.label && color == other.color;
}

LoopUndo::LoopUndo(QWidget* pMainWindow, int settleMs)
        : m_pMainWindow(pMainWindow),
          m_settleMs(settleMs),
          m_pNumDecks(std::make_unique<ControlProxy>(
                  QStringLiteral("[App]"), QStringLiteral("num_decks"), this)),
          m_pUndo(std::make_unique<ControlPushButton>(
                  ConfigKey(QStringLiteral("[LoopUndo]"), QStringLiteral("undo")))) {
    connect(m_pUndo.get(), &ControlObject::valueChanged, this, [this](double value) {
        if (value > 0) {
            slotUndo();
        }
    });
    m_pNumDecks->connectValueChanged(this, [this](double count) {
        addDecks(static_cast<int>(count));
    });
    addDecks(static_cast<int>(m_pNumDecks->get()));

    m_toastTimer.setSingleShot(true);
    m_toastTimer.setInterval(kToastMs);
    connect(&m_toastTimer, &QTimer::timeout, this, [this]() {
        if (m_pToast) {
            m_pToast->hide();
        }
    });
}

LoopUndo::~LoopUndo() {
    delete m_pToast;
}

void LoopUndo::addDecks(int count) {
    for (int i = static_cast<int>(m_decks.size()); i < count; ++i) {
        auto pDeck = std::make_unique<Deck>();
        pDeck->group = QStringLiteral("[Channel%1]").arg(i + 1);
        pDeck->pStart = std::make_unique<ControlProxy>(
                pDeck->group, QStringLiteral("loop_start_position"), this);
        pDeck->pEnd = std::make_unique<ControlProxy>(
                pDeck->group, QStringLiteral("loop_end_position"), this);
        pDeck->pTrackLoaded = std::make_unique<ControlProxy>(pDeck->group,
                QStringLiteral("track_loaded"),
                this,
                ControlFlag::NoAssertIfMissing);
        pDeck->pRoll = std::make_unique<ControlProxy>(pDeck->group,
                QStringLiteral("beatlooproll_activate"),
                this,
                ControlFlag::NoAssertIfMissing);
        pDeck->pSettleTimer = std::make_unique<QTimer>();
        pDeck->pSettleTimer->setSingleShot(true);
        pDeck->pSettleTimer->setInterval(m_settleMs);
        pDeck->stable = currentState(*pDeck);
        pDeck->ignoreChanges = false;

        const int index = i;
        pDeck->pStart->connectValueChanged(this, [this, index](double) {
            onLoopChanged(index);
        });
        pDeck->pEnd->connectValueChanged(this, [this, index](double) {
            onLoopChanged(index);
        });
        if (pDeck->pTrackLoaded->valid()) {
            pDeck->pTrackLoaded->connectValueChanged(this, [this, index](double) {
                // A different track: its loop has nothing to do with the old one.
                dropSteps(index);
                startIgnoring(index);
            });
        }
        if (pDeck->pRoll->valid()) {
            pDeck->pRoll->connectValueChanged(this, [this, index](double) {
                // Press and release: the roll sets a loop, the release puts
                // the old one back. Neither is a step.
                startIgnoring(index);
            });
        }
        connect(pDeck->pSettleTimer.get(), &QTimer::timeout, this, [this, index]() {
            onSettled(index);
        });
        m_decks.push_back(std::move(pDeck));
    }
}

LoopUndo::LoopState LoopUndo::currentState(const Deck& deck) const {
    return LoopState{deck.pStart->get(), deck.pEnd->get()};
}

LoopUndo::CueStates LoopUndo::currentCues(const Deck& deck) {
    CueStates cues;
    if (!deck.pTrack) {
        return cues;
    }
    const QList<CuePointer> cuePoints = deck.pTrack->getCuePoints();
    for (const auto& pCue : cuePoints) {
        if (!pCue || !isUndoableCue(*pCue)) {
            continue;
        }
        const Cue::StartAndEndPositions pos = pCue->getStartAndEndPosition();
        cues.push_back(CueState{pCue,
                pCue->getType(),
                pCue->getHotCue(),
                pos.startPosition,
                pos.endPosition,
                pCue->getLabel(),
                pCue->getColor()});
    }
    return cues;
}

bool LoopUndo::sameCues(const CueStates& a, const CueStates& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (const auto& cue : a) {
        const CueState* pOther = findCue(b, cue.pCue);
        if (!pOther || !cue.sameValues(*pOther)) {
            return false;
        }
    }
    return true;
}

QString LoopUndo::describeChange(const LoopState& loopBefore,
        const LoopState& loopAfter,
        const CueStates& before,
        const CueStates& after) {
    QStringList names;
    if (loopBefore != loopAfter) {
        names << tr("loop");
    }
    for (const auto& cue : before) {
        const CueState* pAfter = findCue(after, cue.pCue);
        if (!pAfter || !cue.sameValues(*pAfter)) {
            names << cueName(cue);
        }
    }
    for (const auto& cue : after) {
        if (!findCue(before, cue.pCue)) {
            names << cueName(cue);
        }
    }
    names.removeDuplicates();
    if (names.size() > 2) {
        return tr("%1 + %2 more").arg(names.first()).arg(names.size() - 1);
    }
    return names.join(QStringLiteral(" + "));
}

void LoopUndo::setTrack(int deckIndex, TrackPointer pTrack) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(m_decks.size())) {
        return;
    }
    Deck* pDeck = m_decks[deckIndex].get();
    if (pDeck->pTrack == pTrack) {
        return;
    }
    // Per deck, not disconnect(track, this): the same track can be on two decks.
    disconnect(pDeck->cuesConnection);
    pDeck->pTrack = std::move(pTrack);
    if (pDeck->pTrack) {
        // Emitted on whichever thread changed the cue (engine, controller,
        // GUI); queued to the GUI thread where all of this runs.
        pDeck->cuesConnection = connect(pDeck->pTrack.get(),
                &Track::cuesUpdated,
                this,
                [this, deckIndex]() {
                    onLoopChanged(deckIndex);
                });
    }
    dropSteps(deckIndex);
    pDeck->stableCues = currentCues(*pDeck);
    startIgnoring(deckIndex);
}

void LoopUndo::onLoopChanged(int deckIndex) {
    Deck* pDeck = m_decks[deckIndex].get();
    if (pDeck->pRoll->valid() && pDeck->pRoll->toBool()) {
        pDeck->ignoreChanges = true;
    }
    pDeck->pSettleTimer->start();
}

void LoopUndo::startIgnoring(int deckIndex) {
    Deck* pDeck = m_decks[deckIndex].get();
    pDeck->ignoreChanges = true;
    pDeck->pSettleTimer->start();
}

void LoopUndo::onSettled(int deckIndex) {
    Deck* pDeck = m_decks[deckIndex].get();
    const LoopState current = currentState(*pDeck);
    CueStates currentCueStates = currentCues(*pDeck);
    if (pDeck->ignoreChanges) {
        pDeck->ignoreChanges = false;
        pDeck->stable = current;
        pDeck->stableCues = std::move(currentCueStates);
        return;
    }
    if (current == pDeck->stable && sameCues(currentCueStates, pDeck->stableCues)) {
        return;
    }
    m_steps.push_back(Step{deckIndex,
            pDeck->stable,
            pDeck->stableCues,
            describeChange(pDeck->stable,
                    current,
                    pDeck->stableCues,
                    currentCueStates)});
    if (static_cast<int>(m_steps.size()) > kMaxSteps) {
        m_steps.erase(m_steps.begin());
    }
    pDeck->stable = current;
    pDeck->stableCues = std::move(currentCueStates);
}

void LoopUndo::flushPending() {
    for (int i = 0; i < static_cast<int>(m_decks.size()); ++i) {
        if (m_decks[i]->pSettleTimer->isActive()) {
            m_decks[i]->pSettleTimer->stop();
            onSettled(i);
        }
    }
}

void LoopUndo::dropSteps(int deckIndex) {
    m_steps.erase(std::remove_if(m_steps.begin(),
                          m_steps.end(),
                          [deckIndex](const Step& step) {
                              return step.deck == deckIndex;
                          }),
            m_steps.end());
}

void LoopUndo::slotUndo() {
    // A change made just before the key press is still settling: it is the
    // one to undo.
    flushPending();
    if (m_steps.empty()) {
        showToast(tr("Nothing to undo"));
        return;
    }
    const Step step = m_steps.back();
    m_steps.pop_back();
    Deck* pDeck = m_decks[step.deck].get();
    if (currentState(*pDeck) != step.before) {
        applyState(pDeck, step.before);
    }
    applyCues(pDeck, step.beforeCues);
    // The engine's own reactions to these writes (e.g. dropping an end that
    // is now before the start) arrive as changes too; none of them is a step.
    pDeck->ignoreChanges = true;
    pDeck->pSettleTimer->start();
    pDeck->stable = currentState(*pDeck);
    pDeck->stableCues = currentCues(*pDeck);
    showToast(tr("Deck %1: %2 change undone").arg(step.deck + 1).arg(step.what));
}

void LoopUndo::applyCues(Deck* pDeck, const CueStates& cues) {
    if (!pDeck->pTrack) {
        return;
    }
    const CueStates current = currentCues(*pDeck);
    // Removals first: a hotcue slot or the one intro/outro has to be free
    // before the old cue goes back into it.
    for (const auto& cue : current) {
        if (!findCue(cues, cue.pCue)) {
            pDeck->pTrack->removeCue(cue.pCue);
        }
    }
    for (const auto& cue : cues) {
        const CueState* pNow = findCue(current, cue.pCue);
        if (pNow) {
            if (cue.sameValues(*pNow)) {
                continue;
            }
            // Still there, but moved / renamed / recoloured / re-slotted.
            if (pNow->type != cue.type) {
                cue.pCue->setType(cue.type);
            }
            if (pNow->hotCue != cue.hotCue) {
                cue.pCue->setHotCue(cue.hotCue);
            }
            if (pNow->start != cue.start || pNow->end != cue.end) {
                cue.pCue->setStartAndEndPosition(cue.start, cue.end);
            }
            if (pNow->label != cue.label) {
                cue.pCue->setLabel(cue.label);
            }
            if (pNow->color != cue.color) {
                cue.pCue->setColor(cue.color);
            }
            continue;
        }
        // Deleted: a new cue with the old values (the old object may already
        // be gone from the database).
        if (!cue.start.isValid() && !cue.end.isValid()) {
            continue;
        }
        CuePointer pNew = pDeck->pTrack->createAndAddCue(
                cue.type, cue.hotCue, cue.start, cue.end, cue.color);
        if (pNew && !cue.label.isEmpty()) {
            pNew->setLabel(cue.label);
        }
    }
}

void LoopUndo::applyState(Deck* pDeck, const LoopState& state) {
    if (!isValidPosition(state.start)) {
        // There was no loop: clearing the start also clears the end.
        pDeck->pStart->set(kNoPosition);
        pDeck->pEnd->set(kNoPosition);
        return;
    }
    // LoopingControl drops the end when a new start is not before it, and
    // rejects an end that is not after the start, so the write order matters.
    const double currentEnd = pDeck->pEnd->get();
    if (!isValidPosition(currentEnd) || state.start < currentEnd) {
        pDeck->pStart->set(state.start);
        pDeck->pEnd->set(state.end);
    } else {
        // The old loop starts at or after the current end: writing the start
        // first would drop the end. The old end is after the current start,
        // so it is accepted first.
        pDeck->pEnd->set(state.end);
        pDeck->pStart->set(state.start);
    }
}

void LoopUndo::showToast(const QString& text) {
    if (!m_pMainWindow) {
        return;
    }
    if (!m_pToast) {
        // Owned by the main window; floats over the skin, ignores the mouse.
        m_pToast = new QLabel(m_pMainWindow);
        m_pToast->setObjectName(QStringLiteral("LoopUndoToast"));
        m_pToast->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_pToast->setFocusPolicy(Qt::NoFocus);
        m_pToast->setAlignment(Qt::AlignCenter);
        m_pToast->setTextFormat(Qt::PlainText);
        m_pToast->setStyleSheet(QStringLiteral(
                "#LoopUndoToast { background-color: rgba(20, 20, 20, 230);"
                " color: #FFFFFF; font-size: 16px; font-weight: bold;"
                " border: 2px solid #3FA9F5; border-radius: 6px;"
                " padding: 10px 18px; }"));
    }
    m_pToast->setText(text);
    m_pToast->adjustSize();
    m_pToast->move((m_pMainWindow->width() - m_pToast->width()) / 2,
            m_pMainWindow->height() / 3 - m_pToast->height() / 2);
    m_pToast->raise();
    m_pToast->show();
    m_toastTimer.start();
}
