#pragma once

#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <memory>
#include <vector>

#include "control/pollingcontrolproxy.h"
#include "preferences/usersettings.h"
#include "track/track_decl.h"
#include "track/trackid.h"

class ControlObject;
class ControlPushButton;
class QColor;
class QLabel;
class QWidget;

// andy-custom: live "worked on the floor" crowd rating.
//
// A live session is started from Options -> "Start Live DJing..." and asks for
// the event name and the style (Fusion / Zouk / other). While it runs, every track that is audible on the main mix
// for >= 30 s is logged as played, and the DJ can rate the audible track with
// [CrowdRating],rate_plus / rate_minus / rate_neutral (keyboard Ctrl++ / Ctrl+-
// / Ctrl+0 by default, mappable to controller buttons) or any track from the
// track context menu. Every entry goes with its timestamp into one JSONL file
// per session in <settings>/live_sessions/, e.g. 261016_fusion-waves.jsonl.
// Nothing is written to the library database or to the track's own rating.
//
// While a deck holds a track rated in this session, [ChannelN],crowd_rating is
// its latest rating (1 worked, 2 didn't work, 3 neutral, 0 none); skins bind
// it to WWidgetGroup's "tint" property.
class CrowdRatingSession : public QObject {
    Q_OBJECT
  public:
    enum class Kind {
        Plus,
        Minus,
        Neutral,
    };

    CrowdRatingSession(UserSettingsPointer pConfig, QWidget* pMainWindow);
    ~CrowdRatingSession() override;

    // nullptr before the main window created it and after shutdown.
    static CrowdRatingSession* instance();

    bool isActive() const {
        return m_active;
    }
    QString eventName() const {
        return m_eventName;
    }
    // What is being DJed: "Fusion", "Zouk" or free text. Unlike the event
    // name, this is what later goes into the mp3 tags.
    QString style() const {
        return m_style;
    }

    // Rating from the track context menu (any track, not only the audible one).
    void rateTracks(const TrackPointerList& tracks, Kind kind);

    // Called once after startup: if the last session saw activity less than
    // 6 h ago (crash or restart mid-gig), offer to continue it.
    void offerResume();

    // Starts logging to filePath (appends if it exists). resumed = continue
    // an earlier session: restores its ratings and logs session_resume.
    // Public for tests; the UI goes through slotStartOrEndSession()/offerResume().
    bool startSession(const QString& eventName,
            const QString& style,
            const QString& filePath,
            bool resumed);
    void endSession();
    // Counts elapsedMillis of audible time for every audible deck and logs
    // tracks that reach 30 s. Driven by a 1 s timer; public for tests.
    void advance(qint64 elapsedMillis);

  public slots:
    // Menu entry: asks for the event name and starts, or ends the running one.
    void slotStartOrEndSession();
    // Rates the track that is audible on the main mix right now (Kind as int).
    void slotRateAudible(int kind);

  signals:
    void activeChanged(bool active, const QString& eventName);

  private slots:
    void slotTick();
    void slotTrackChanged(const QString& group, TrackPointer pNewTrack, TrackPointer pOldTrack);

  private:
    struct DeckState {
        QString group;
        std::unique_ptr<ControlObject> pRatingCO;
        std::unique_ptr<PollingControlProxy> pPlay;
        std::unique_ptr<PollingControlProxy> pVolume;
        std::unique_ptr<PollingControlProxy> pPregain;
        std::unique_ptr<PollingControlProxy> pOrientation;
        std::unique_ptr<PollingControlProxy> pPlayPosition;
        std::unique_ptr<PollingControlProxy> pDuration;
        // "Played" accounting for the track currently loaded
        TrackId trackId;
        qint64 audibleMillis = 0;
        QDateTime audibleSince;
        bool playLogged = false;
    };

    void rateAudible(Kind kind);
    void writeEntry(const QJsonObject& entry);
    QJsonObject trackEntry(const QString& kind, const TrackPointer& pTrack) const;
    // The deck's controls only exist once the deck does (4 decks enabled).
    static void ensureProxies(DeckState* pDeck);
    // Gain of the deck on the main mix (volume x crossfader), 0 if not playing.
    double deckGain(DeckState* pDeck);
    int loudestDeck();
    void updateDeckTints(const TrackId& trackId);
    void clearDeckTints();
    void showToast(const QString& text, Kind kind);
    void showToast(const QString& text, const QColor& accent);

    static QString kindName(Kind kind);
    static double tintValue(Kind kind);

    UserSettingsPointer m_pConfig;
    QPointer<QWidget> m_pMainWindow;

    std::unique_ptr<ControlPushButton> m_pRatePlus;
    std::unique_ptr<ControlPushButton> m_pRateMinus;
    std::unique_ptr<ControlPushButton> m_pRateNeutral;
    std::unique_ptr<ControlObject> m_pSessionActive;
    PollingControlProxy m_xfader;
    std::vector<DeckState> m_decks;

    bool m_active;
    QString m_eventName;
    QString m_style;
    QFile m_file;
    QTimer m_tickTimer;
    QDateTime m_lastTick;
    // Latest rating per track in this session; drives the deck tint.
    QHash<TrackId, Kind> m_latestRating;

    QLabel* m_pToast;
    QTimer m_toastTimer;
};
