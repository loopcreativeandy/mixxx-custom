#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <memory>
#include <vector>

#include "audio/frame.h"
#include "track/cue.h"
#include "track/track_decl.h"
#include "util/color/rgbcolor.h"

class ControlProxy;
class ControlPushButton;
class QLabel;
class QWidget;

// andy-custom: undo for loop changes (Andy, 2026-10-16: "I sometimes move the
// loop by accident and want to un-move it").
//
// Watches loop_start_position / loop_end_position of every deck. Changes that
// arrive within kSettleMs of each other count as one step (loop in + loop out,
// a burst of loop_move presses). [LoopUndo],undo (Ctrl+Z from the Options menu
// by default) puts back the loop as it was before the latest step, on
// whichever deck changed last; pressing it again walks further back.
//
// Since CP129 (Andy, 2026-10-26: "I clicked the end marker of the wrong track
// ... and there's no undo") a step also covers the loaded track's hotcues
// (incl. saved loops) and its intro/outro markers: set, moved, deleted, label
// or colour changed. Undo puts every one of them back as it was, a deleted
// hotcue comes back with its label and colour. The main cue is not covered
// (the CUE button sets it all the time). Loop and cue changes of one deck
// within kSettleMs are one step, so a moved saved loop is undone in one go.
//
// Not recorded: loading a track (that deck's history is dropped), loop changes
// while beatlooproll_activate is held (the roll puts the loop back itself),
// and the changes the undo itself causes. Only the positions are restored, not
// whether the loop is switched on.
//
// Runs on the GUI thread only: control reads/writes and a short timer, never
// anything on the audio engine thread.
class LoopUndo : public QObject {
    Q_OBJECT
  public:
    static constexpr int kSettleMs = 300;
    static constexpr int kMaxSteps = 50;

    struct LoopState {
        double start;
        double end;
        bool operator==(const LoopState& other) const {
            return start == other.start && end == other.end;
        }
        bool operator!=(const LoopState& other) const {
            return !(*this == other);
        }
    };

    /// One hotcue / saved loop / intro / outro of the loaded track. pCue
    /// identifies it; the rest is what it looked like.
    struct CueState {
        CuePointer pCue;
        mixxx::CueType type;
        int hotCue;
        mixxx::audio::FramePos start;
        mixxx::audio::FramePos end;
        QString label;
        mixxx::RgbColor color;
        bool sameValues(const CueState& other) const;
    };
    using CueStates = std::vector<CueState>;

    explicit LoopUndo(QWidget* pMainWindow, int settleMs = kSettleMs);
    ~LoopUndo() override;

    /// Number of undo steps currently stored (all decks).
    int stepCount() const {
        return static_cast<int>(m_steps.size());
    }

    int deckCount() const {
        return static_cast<int>(m_decks.size());
    }

    /// Commits changes that are still settling. For tests and before undo.
    void flushPending();

    /// The track now loaded on deck deckIndex (0-based), or null when ejected.
    /// Its cues are watched from now on; the deck's steps are dropped.
    void setTrack(int deckIndex, TrackPointer pTrack);

  public slots:
    /// Restores the loop before the latest step. Returns via toast.
    void slotUndo();

  private:
    struct Deck {
        QString group;
        std::unique_ptr<ControlProxy> pStart;
        std::unique_ptr<ControlProxy> pEnd;
        std::unique_ptr<ControlProxy> pTrackLoaded;
        std::unique_ptr<ControlProxy> pRoll;
        std::unique_ptr<QTimer> pSettleTimer;
        TrackPointer pTrack;
        QMetaObject::Connection cuesConnection;
        // The loop and cues as of the last committed step.
        LoopState stable;
        CueStates stableCues;
        // Set while changes come from a track load, a roll or our own undo:
        // the next settle only re-reads the loop, it records no step.
        bool ignoreChanges;
    };
    struct Step {
        int deck;
        LoopState before;
        CueStates beforeCues;
        QString what; // for the toast, e.g. "hotcue 3"
    };

    void addDecks(int count);
    LoopState currentState(const Deck& deck) const;
    static CueStates currentCues(const Deck& deck);
    static bool sameCues(const CueStates& a, const CueStates& b);
    static QString describeChange(const LoopState& loopBefore,
            const LoopState& loopAfter,
            const CueStates& before,
            const CueStates& after);
    static void applyCues(Deck* pDeck, const CueStates& cues);
    void onLoopChanged(int deckIndex);
    void onSettled(int deckIndex);
    void startIgnoring(int deckIndex);
    void dropSteps(int deckIndex);
    void applyState(Deck* pDeck, const LoopState& state);
    void showToast(const QString& text);

    QPointer<QWidget> m_pMainWindow;
    const int m_settleMs;
    std::vector<std::unique_ptr<Deck>> m_decks;
    std::vector<Step> m_steps;
    std::unique_ptr<ControlProxy> m_pNumDecks;
    std::unique_ptr<ControlPushButton> m_pUndo;
    QPointer<QLabel> m_pToast;
    QTimer m_toastTimer;
};
