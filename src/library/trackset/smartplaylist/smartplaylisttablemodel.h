#pragma once

#include <QString>

#include "library/librarytablemodel.h"

/// Track model behind a smart playlist (andy-custom).
///
/// It is the plain library model with the smart playlist's saved query pushed
/// down as an "extra filter". BaseTrackCache appends that filter to whatever
/// the user types into the search bar, so searching *within* a smart playlist
/// works exactly like searching within a regular playlist - the saved query is
/// simply always ANDed in.
class SmartPlaylistTableModel : public LibraryTableModel {
    Q_OBJECT
  public:
    SmartPlaylistTableModel(QObject* parent,
            TrackCollectionManager* pTrackCollectionManager);
    ~SmartPlaylistTableModel() override = default;

    /// Applies the saved query and re-evaluates it against the library. This is
    /// the only refresh point: a smart playlist updates when it is opened, not
    /// while it is being displayed.
    void selectSmartPlaylist(const QString& query);

    /// Like the library, minus ReceiveDrops: track membership is defined by the
    /// query, so dropping tracks onto a smart playlist is rejected.
    TrackModel::Capabilities getCapabilities() const override;
};
