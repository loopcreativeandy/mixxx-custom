#pragma once

#include <QModelIndex>
#include <QObject>
#include <QUrl>
#include <QVariant>

#include "library/trackset/baseplaylistfeature.h"
#include "preferences/usersettings.h"
#include "util/parented_ptr.h"

class TreeItem;
class QPoint;

class PlaylistFeature : public BasePlaylistFeature {
    Q_OBJECT

  public:
    PlaylistFeature(
            Library* pLibrary,
            UserSettingsPointer pConfig);
    ~PlaylistFeature() override = default;

    QVariant title() override;

    bool dropAcceptChild(const QModelIndex& index,
            const QList<QUrl>& urls,
            QObject* pSource) override;
    bool dragMoveAcceptChild(const QModelIndex& index, const QList<QUrl>& urls) override;

  public slots:
    void onRightClick(const QPoint& globalPos) override;
    void onRightClickChild(const QPoint& globalPos, const QModelIndex& index) override;

  private slots:
    void slotPlaylistTableChanged(int playlistId) override;
    void slotPlaylistContentOrLockChanged(const QSet<int>& playlistIds) override;
    void slotPlaylistTableRenamed(int playlistId, const QString& newName) override;
    void slotShufflePlaylist();
    void slotOpenInSidePane();
    void slotMarkAllTracksPlayed();
    void slotMarkAllTracksUnplayed();
    void slotUnlockAllPlaylists();
    void slotDeleteAllUnlockedPlaylists();

  protected:
    void decorateChild(TreeItem* pChild, int playlistId) override;
    QList<IdAndLabel> createPlaylistLabels();
    QModelIndex constructChildModel(int selectedId);
    // Renders the leaf name only for playlists grouped into a sidebar
    // folder via the "Folder/Playlist" naming convention.
    QString createPlaylistLabel(const QString& name, int count, int duration) const override;

  private:
    QString getRootViewHtml() const override;
    // Returns the sidebar folder a playlist belongs to per the
    // "Folder/Playlist" naming convention, or an empty string for
    // top-level playlists.
    static QString sidebarFolderOfName(const QString& name);
    // Set the played status of every track in the right-clicked playlist,
    // without changing the play count. Backs the "Mark all tracks
    // played/unplayed" context-menu actions.
    void setAllTracksPlayedStatus(bool played);

    parented_ptr<QAction> m_pShufflePlaylistAction;
    parented_ptr<QAction> m_pOpenInSidePaneAction;
    parented_ptr<QAction> m_pMarkAllPlayedAction;
    parented_ptr<QAction> m_pMarkAllUnplayedAction;
    parented_ptr<QAction> m_pUnlockPlaylistsAction;
    parented_ptr<QAction> m_pDeleteAllUnlockedPlaylistsAction;
};
