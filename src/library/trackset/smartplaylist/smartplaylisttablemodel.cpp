#include "library/trackset/smartplaylist/smartplaylisttablemodel.h"

#include "moc_smartplaylisttablemodel.cpp"

SmartPlaylistTableModel::SmartPlaylistTableModel(QObject* parent,
        TrackCollectionManager* pTrackCollectionManager)
        : LibraryTableModel(parent,
                  pTrackCollectionManager,
                  "mixxx.db.model.smartplaylist") {
}

void SmartPlaylistTableModel::selectSmartPlaylist(const QString& query) {
    setExtraFilter(query);
    // Opening a smart playlist starts from an unfiltered view of it; the search
    // bar then filters within.
    setSearch(QString());
    select();
}

TrackModel::Capabilities SmartPlaylistTableModel::getCapabilities() const {
    return LibraryTableModel::getCapabilities() & ~Capabilities(Capability::ReceiveDrops);
}
