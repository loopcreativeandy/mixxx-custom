#include "library/settracklistwatcher.h"

#include "control/pollingcontrolproxy.h"
#include "moc_settracklistwatcher.cpp"
#include "track/track.h"
#include "util/logger.h"

namespace {

const mixxx::Logger kLogger("SetTracklistWatcher");

constexpr int kTickMillis = 1000;
// Fader up: same threshold as the recording's audibility rule (CP127).
constexpr double kMinVolume = 0.1;

} // namespace

SetTracklistWatcher::SetTracklistWatcher(TrackCollectionManager* pTrackCollectionManager,
        UserSettingsPointer pConfig,
        QObject* pParent)
        : QObject(pParent),
          m_pTrackCollectionManager(pTrackCollectionManager),
          m_pConfig(std::move(pConfig)) {
    connect(&m_timer, &QTimer::timeout, this, &SetTracklistWatcher::slotTick);
}

SetTracklistWatcher::~SetTracklistWatcher() = default;

void SetTracklistWatcher::setTrack(const QString& group, const TrackPointer& pTrack) {
    m_decks.remove(group);
    if (pTrack) {
        QList<mixxx::settracklist::Entry> entries =
                mixxx::settracklist::read(pTrack->getLocation());
        if (!entries.isEmpty()) {
            kLogger.info() << group << "loaded a set with" << entries.size() << "songs";
            auto pDeck = std::make_shared<DeckSet>();
            pDeck->pending = std::move(entries);
            pDeck->pPlay = std::make_unique<PollingControlProxy>(group, QStringLiteral("play"));
            pDeck->pVolume = std::make_unique<PollingControlProxy>(group, QStringLiteral("volume"));
            pDeck->pPosition = std::make_unique<PollingControlProxy>(
                    group, QStringLiteral("playposition"));
            pDeck->pDuration = std::make_unique<PollingControlProxy>(
                    group, QStringLiteral("duration"));
            m_decks.insert(group, pDeck);
        }
    }
    if (m_decks.isEmpty()) {
        m_timer.stop();
    } else if (!m_timer.isActive()) {
        m_timer.start(kTickMillis);
    }
}

void SetTracklistWatcher::slotTick() {
    for (auto it = m_decks.begin(); it != m_decks.end(); ++it) {
        DeckSet& deck = *it.value();
        if (deck.pending.isEmpty() || deck.pPlay->get() == 0.0 ||
                deck.pVolume->get() < kMinVolume) {
            continue;
        }
        const double positionMillis =
                deck.pPosition->get() * deck.pDuration->get() * 1000.0;
        QList<mixxx::settracklist::Entry> reached;
        for (auto entry = deck.pending.begin(); entry != deck.pending.end();) {
            if (mixxx::settracklist::reachedAtMillis(*entry) <= positionMillis) {
                reached.append(*entry);
                entry = deck.pending.erase(entry);
            } else {
                ++entry;
            }
        }
        if (!reached.isEmpty()) {
            const int marked = mixxx::settracklist::markEntries(
                    m_pTrackCollectionManager, m_pConfig, reached, true);
            kLogger.info() << it.key() << "set reached" << reached.size()
                           << "song(s), marked played:" << marked;
        }
    }
}
