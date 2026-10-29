#pragma once

#include <QHash>
#include <QObject>
#include <QTimer>
#include <memory>

#include "library/settracklist.h"
#include "preferences/usersettings.h"
#include "track/track_decl.h"

class PollingControlProxy;
class TrackCollectionManager;

/// andy-custom CP130: while a recorded set plays on a deck, each song in it
/// is marked played (flag only) once the play position is
/// settracklist::kReachedMillis into that song - so stopping a set halfway
/// leaves its second half unmarked. Only while the deck is playing with its
/// volume fader up (not a headphone-only preview).
class SetTracklistWatcher : public QObject {
    Q_OBJECT
  public:
    SetTracklistWatcher(TrackCollectionManager* pTrackCollectionManager,
            UserSettingsPointer pConfig,
            QObject* pParent = nullptr);
    ~SetTracklistWatcher() override;

    /// Call for every deck track change (PlayerInfo::trackChanged).
    void setTrack(const QString& group, const TrackPointer& pTrack);

  private slots:
    void slotTick();

  private:
    struct DeckSet {
        QList<mixxx::settracklist::Entry> pending;
        std::unique_ptr<PollingControlProxy> pPlay;
        std::unique_ptr<PollingControlProxy> pVolume;
        std::unique_ptr<PollingControlProxy> pPosition;
        std::unique_ptr<PollingControlProxy> pDuration;
    };

    TrackCollectionManager* const m_pTrackCollectionManager;
    const UserSettingsPointer m_pConfig;
    QHash<QString, std::shared_ptr<DeckSet>> m_decks;
    QTimer m_timer;
};
