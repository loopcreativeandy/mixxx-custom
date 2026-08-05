#pragma once

#include <QModelIndex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariant>

#include "library/stemoriginal.h"
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

    void bindSidebarWidget(WLibrarySidebar* pSidebarWidget) override;

  public slots:
    void onRightClick(const QPoint& globalPos) override;
    void onRightClickChild(const QPoint& globalPos, const QModelIndex& index) override;
    // Track folder expansion state so it survives sidebar rebuilds and
    // restarts. Expansion arrives via the lazy-childmodel hook (the sidebar
    // routes QTreeView::expanded there), collapse via onChildCollapse.
    void onLazyChildExpandation(const QModelIndex& index) override;
    void onChildCollapse(const QModelIndex& index) override;
    // F2 on a folder node renames the folder instead of a playlist.
    void renameItem(const QModelIndex& index) override;

  private slots:
    void slotPlaylistTableChanged(int playlistId) override;
    void slotPlaylistContentOrLockChanged(const QSet<int>& playlistIds) override;
    void slotPlaylistTableRenamed(int playlistId, const QString& newName) override;
    void slotShufflePlaylist();
    void slotOpenInSidePane();
    void slotMarkAllTracksPlayed();
    void slotMarkAllTracksUnplayed();
    // Swap a whole playlist between stem files and their originals
    void slotSwapToStemTracks();
    void slotSwapToOriginalTracks();
    void slotUnlockAllPlaylists();
    void slotDeleteAllUnlockedPlaylists();
    void slotRenameFolder();
    void slotMoveToFolder(const QString& folder);
    void slotMoveToNewFolder();

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
    // True if the index is a sidebar folder node (has children, no
    // playlist id).
    bool isFolderIndex(const QModelIndex& index) const;
    // Labels of all folder nodes currently in the sidebar, sorted
    // case-insensitively.
    QStringList currentFolders() const;
    // Renames every "oldFolder/x" playlist to "newFolder/x". Refuses (with
    // a message box) when a member is locked or a target name is taken.
    void renameFolderMembers(const QString& oldFolder, const QString& newFolder);
    void restoreExpandedFolders();
    void saveExpandedFolders();
    // Set the played status of every track in the right-clicked playlist,
    // without changing the play count. Backs the "Mark all tracks
    // played/unplayed" context-menu actions.
    void setAllTracksPlayedStatus(bool played);

    // Replace every entry of the right-clicked playlist with its
    // stem/non-stem counterpart, keeping the position. Entries
    // without a counterpart in the library are left alone.
    void swapPlaylistTracks(mixxx::stemoriginal::Counterpart counterpart);

    parented_ptr<QAction> m_pShufflePlaylistAction;
    parented_ptr<QAction> m_pOpenInSidePaneAction;
    parented_ptr<QAction> m_pMarkAllPlayedAction;
    parented_ptr<QAction> m_pMarkAllUnplayedAction;
    parented_ptr<QAction> m_pSwapToStemsAction;
    parented_ptr<QAction> m_pSwapToOriginalsAction;
    parented_ptr<QAction> m_pUnlockPlaylistsAction;
    parented_ptr<QAction> m_pDeleteAllUnlockedPlaylistsAction;
    parented_ptr<QAction> m_pRenameFolderAction;

    // Folder labels the user has expanded; persisted in
    // [PlaylistFeature],ExpandedFolders and re-applied after every sidebar
    // rebuild.
    QSet<QString> m_expandedFolders;
    // Suppresses collapse tracking while the child model is rebuilt.
    bool m_rebuildingChildModel = false;
};
