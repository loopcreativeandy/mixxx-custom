#include "mixer/loopundo.h"

#include <QLabel>
#include <QWidget>
#include <algorithm>

#include "control/controlproxy.h"
#include "control/controlpushbutton.h"
#include "moc_loopundo.cpp"

namespace {

constexpr double kNoPosition = -1.0;
constexpr int kToastMs = 1500;

bool isValidPosition(double position) {
    return position >= 0.0;
}

} // namespace

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
    if (pDeck->ignoreChanges) {
        pDeck->ignoreChanges = false;
        pDeck->stable = current;
        return;
    }
    if (current == pDeck->stable) {
        return;
    }
    m_steps.push_back(Step{deckIndex, pDeck->stable});
    if (static_cast<int>(m_steps.size()) > kMaxSteps) {
        m_steps.erase(m_steps.begin());
    }
    pDeck->stable = current;
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
        showToast(tr("No loop change to undo"));
        return;
    }
    const Step step = m_steps.back();
    m_steps.pop_back();
    Deck* pDeck = m_decks[step.deck].get();
    applyState(pDeck, step.before);
    // The engine's own reactions to these writes (e.g. dropping an end that
    // is now before the start) arrive as changes too; none of them is a step.
    pDeck->ignoreChanges = true;
    pDeck->pSettleTimer->start();
    pDeck->stable = currentState(*pDeck);
    showToast(tr("Deck %1: loop change undone").arg(step.deck + 1));
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
