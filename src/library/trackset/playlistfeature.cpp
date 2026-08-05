#include "library/trackset/playlistfeature.h"

#include <QHash>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QSqlTableModel>
#include <QtDebug>

#include "library/library.h"
#include "library/parser.h"
#include "library/playlisttablemodel.h"
#include "library/queryutil.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "library/treeitem.h"
#include "moc_playlistfeature.cpp"
#include "sources/soundsourceproxy.h"
#include "track/track.h"
#include "util/db/dbconnection.h"
#include "util/dnd.h"
#include "util/duration.h"
#include "widget/wlibrary.h"
#include "widget/wlibrarysidebar.h"
#include "widget/wtracktableview.h"

PlaylistFeature::PlaylistFeature(Library* pLibrary, UserSettingsPointer pConfig)
        : BasePlaylistFeature(pLibrary,
                  pConfig,
                  new PlaylistTableModel(nullptr,
                          pLibrary->trackCollectionManager(),
                          "mixxx.db.model.playlist"),
                  QStringLiteral("PLAYLISTHOME"),
                  QStringLiteral("playlist"),
                  QStringLiteral("PlaylistsCountsDurations")) {
    // Restore which sidebar folders were expanded. Folder names cannot
    // contain '/' (it is the folder separator inside playlist names), so
    // it doubles as a safe list separator here.
    const QString expandedFolders = m_pConfig->getValueString(
            ConfigKey("[PlaylistFeature]", "ExpandedFolders"));
    if (!expandedFolders.isEmpty()) {
        const QStringList folders =
                expandedFolders.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        m_expandedFolders = QSet<QString>(folders.begin(), folders.end());
    }

    // construct child model
    std::unique_ptr<TreeItem> pRootItem = TreeItem::newRoot(this);
    m_pSidebarModel->setRootItem(std::move(pRootItem));
    constructChildModel(kInvalidPlaylistId);

    m_pOpenInSidePaneAction = make_parented<QAction>(tr("Open in side pane"), this);
    connect(m_pOpenInSidePaneAction,
            &QAction::triggered,
            this,
            &PlaylistFeature::slotOpenInSidePane);

    m_pShufflePlaylistAction = make_parented<QAction>(tr("Shuffle Playlist"), this);
    connect(m_pShufflePlaylistAction,
            &QAction::triggered,
            this,
            &PlaylistFeature::slotShufflePlaylist);

    m_pMarkAllPlayedAction = make_parented<QAction>(tr("Mark all tracks played"), this);
    connect(m_pMarkAllPlayedAction,
            &QAction::triggered,
            this,
            &PlaylistFeature::slotMarkAllTracksPlayed);

    m_pMarkAllUnplayedAction =
            make_parented<QAction>(tr("Mark all tracks unplayed"), this);
    connect(m_pMarkAllUnplayedAction,
            &QAction::triggered,
            this,
            &PlaylistFeature::slotMarkAllTracksUnplayed);

    m_pUnlockPlaylistsAction =
            make_parented<QAction>(tr("Unlock all playlists"), this);
    connect(m_pUnlockPlaylistsAction,
            &QAction::triggered,
            this,
            &PlaylistFeature::slotUnlockAllPlaylists);

    m_pDeleteAllUnlockedPlaylistsAction =
            make_parented<QAction>(tr("Delete all unlocked playlists"), this);
    connect(m_pDeleteAllUnlockedPlaylistsAction,
            &QAction::triggered,
            this,
            &PlaylistFeature::slotDeleteAllUnlockedPlaylists);

    m_pRenameFolderAction = make_parented<QAction>(tr("Rename Folder"), this);
    connect(m_pRenameFolderAction,
            &QAction::triggered,
            this,
            &PlaylistFeature::slotRenameFolder);
}

void PlaylistFeature::bindSidebarWidget(WLibrarySidebar* pSidebarWidget) {
    BasePlaylistFeature::bindSidebarWidget(pSidebarWidget);
    // The child model was constructed before the sidebar widget existed;
    // apply the persisted folder expansion now.
    restoreExpandedFolders();
}

void PlaylistFeature::slotOpenInSidePane() {
    if (!m_lastRightClickedIndex.isValid()) {
        return;
    }
    const int playlistId = playlistIdFromIndex(m_lastRightClickedIndex);
    if (playlistId >= 0) {
        m_pLibrary->requestOpenPlaylistInSidePane(playlistId);
    }
}

QVariant PlaylistFeature::title() {
    return tr("Playlists");
}

void PlaylistFeature::onRightClick(const QPoint& globalPos) {
    m_lastRightClickedIndex = QModelIndex();
    QMenu menu(m_pSidebarWidget);
    menu.addAction(m_pCreatePlaylistAction);
    menu.addSeparator();
    menu.addAction(m_pUnlockPlaylistsAction);
    menu.addAction(m_pDeleteAllUnlockedPlaylistsAction);
    menu.addSeparator();
    menu.addAction(m_pCreateImportPlaylistAction);
#ifdef __ENGINEPRIME__
    menu.addSeparator();
    menu.addAction(m_pExportAllPlaylistsToEngineAction);
#endif
    menu.exec(globalPos);
}

void PlaylistFeature::onRightClickChild(
        const QPoint& globalPos, const QModelIndex& index) {
    //Save the model index so we can get it in the action slots...
    m_lastRightClickedIndex = index;
    int playlistId = playlistIdFromIndex(index);
    if (playlistId == kInvalidPlaylistId) {
        // Folder node: none of the per-playlist actions apply. Offer the
        // folder actions plus the root create/import actions.
        QMenu menu(m_pSidebarWidget);
        menu.addAction(m_pRenameFolderAction);
        menu.addSeparator();
        menu.addAction(m_pCreatePlaylistAction);
        menu.addAction(m_pCreateImportPlaylistAction);
        menu.exec(globalPos);
        return;
    }

    bool locked = m_playlistDao.isPlaylistLocked(playlistId);
    m_pDeletePlaylistAction->setEnabled(!locked);
    m_pRenamePlaylistAction->setEnabled(!locked);
    m_pShufflePlaylistAction->setEnabled(!locked);
    m_pImportPlaylistAction->setEnabled(!locked);

    m_pLockPlaylistAction->setText(locked ? tr("Unlock") : tr("Lock"));

    QMenu menu(m_pSidebarWidget);
    menu.addAction(m_pOpenInSidePaneAction);
    menu.addSeparator();
    menu.addAction(m_pCreatePlaylistAction);
    menu.addSeparator();
    // TODO If playlist is selected and has more than one track selected
    // show "Shuffle selected tracks", else show "Shuffle playlist"?
    menu.addAction(m_pShufflePlaylistAction);
    menu.addAction(m_pMarkAllPlayedAction);
    menu.addAction(m_pMarkAllUnplayedAction);
    menu.addSeparator();
    menu.addAction(m_pRenamePlaylistAction);
    // Move a playlist between sidebar folders by renaming it per the
    // "Folder/Playlist" naming convention.
    QMenu* pMoveToFolderMenu = menu.addMenu(tr("Move to Folder"));
    pMoveToFolderMenu->setEnabled(!locked);
    if (!locked) {
        const QString currentFolder =
                sidebarFolderOfName(m_playlistDao.getPlaylistName(playlistId));
        const QStringList folders = currentFolders();
        for (const QString& folder : folders) {
            QAction* pFolderAction = pMoveToFolderMenu->addAction(folder);
            if (folder == currentFolder) {
                pFolderAction->setCheckable(true);
                pFolderAction->setChecked(true);
                pFolderAction->setEnabled(false);
            } else {
                connect(pFolderAction, &QAction::triggered, this, [this, folder] {
                    slotMoveToFolder(folder);
                });
            }
        }
        if (!folders.isEmpty()) {
            pMoveToFolderMenu->addSeparator();
        }
        QAction* pNewFolderAction = pMoveToFolderMenu->addAction(tr("New Folder..."));
        connect(pNewFolderAction,
                &QAction::triggered,
                this,
                &PlaylistFeature::slotMoveToNewFolder);
        if (!currentFolder.isEmpty()) {
            QAction* pRemoveAction =
                    pMoveToFolderMenu->addAction(tr("Remove from Folder"));
            connect(pRemoveAction, &QAction::triggered, this, [this] {
                slotMoveToFolder(QString());
            });
        }
    }
    menu.addAction(m_pDuplicatePlaylistAction);
    menu.addAction(m_pDeletePlaylistAction);
    menu.addAction(m_pLockPlaylistAction);
    menu.addSeparator();
    menu.addAction(m_pAddToAutoDJAction);
    menu.addAction(m_pAddToAutoDJTopAction);
    menu.addAction(m_pAddToAutoDJReplaceAction);
    menu.addSeparator();
    menu.addAction(m_pAnalyzePlaylistAction);
    menu.addSeparator();
    menu.addAction(m_pImportPlaylistAction);
    menu.addAction(m_pExportPlaylistAction);
    menu.addAction(m_pExportTrackFilesAction);
#ifdef __ENGINEPRIME__
    menu.addAction(m_pExportPlaylistToEngineAction);
#endif
    menu.exec(globalPos);
}

bool PlaylistFeature::dropAcceptChild(
        const QModelIndex& index, const QList<QUrl>& urls, QObject* pSource) {
    int playlistId = playlistIdFromIndex(index);
    if (playlistId == kInvalidPlaylistId) {
        // Folder nodes have no playlist id; tracks can't be dropped there.
        return false;
    }
    VERIFY_OR_DEBUG_ASSERT(!m_playlistDao.isPlaylistLocked(playlistId)) {
        return false;
    }
    // If a track is dropped onto a playlist's name, but the track isn't in the
    // library, then add the track to the library before adding it to the
    // playlist.
    // pSource != nullptr it is a drop from inside Mixxx and indicates all
    // tracks already in the DB
    const QList<mixxx::FileInfo> fileInfos =
            // collect all tracks, accept playlist files
            DragAndDropHelper::supportedTracksFromUrls(urls, false, true);
    const QList<TrackId> trackIds =
            m_pLibrary->trackCollectionManager()->resolveTrackIds(fileInfos, pSource);
    if (trackIds.isEmpty()) {
        return false;
    }

    // Return whether appendTracksToPlaylist succeeded.
    return m_playlistDao.appendTracksToPlaylist(trackIds, playlistId);
}

bool PlaylistFeature::dragMoveAcceptChild(const QModelIndex& index, const QList<QUrl>& urls) {
    int playlistId = playlistIdFromIndex(index);
    if (playlistId == kInvalidPlaylistId) {
        return false;
    }
    if (m_playlistDao.isPlaylistLocked(playlistId)) {
        return false;
    }

    return DragAndDropHelper::urlsContainSupportedTrackFiles(urls, true);
}

QList<BasePlaylistFeature::IdAndLabel> PlaylistFeature::createPlaylistLabels() {
    QSqlDatabase database =
            m_pLibrary->trackCollectionManager()->internalCollection()->database();

    QList<BasePlaylistFeature::IdAndLabel> playlistLabels;
    QString queryString = QStringLiteral(
            "CREATE TEMPORARY VIEW IF NOT EXISTS %1 "
            "AS SELECT "
            "  Playlists.id AS id, "
            "  Playlists.name AS name, "
            "  LOWER(Playlists.name) AS sort_name, "
            "  COUNT(case library.mixxx_deleted when 0 then 1 else null end) "
            "    AS count, "
            "  SUM(case library.mixxx_deleted "
            "    when 0 then library.duration else 0 end) AS durationSeconds "
            "FROM Playlists "
            "LEFT JOIN PlaylistTracks "
            "  ON PlaylistTracks.playlist_id = Playlists.id "
            "LEFT JOIN library "
            "  ON PlaylistTracks.track_id = library.id "
            "  WHERE Playlists.hidden = %2 "
            "  GROUP BY Playlists.id")
                                  .arg(m_countsDurationTableName,
                                          QString::number(
                                                  PlaylistDAO::PLHT_NOT_HIDDEN));
    // andy-custom: sort sidebar playlists by leading-underscore tier (more
    // underscores = pinned higher, capped at 5), then creation date newest on
    // top. substr() instead of LIKE because '_' is a LIKE wildcard.
    queryString.append(QStringLiteral(
            " ORDER BY "
            " CASE"
            "  WHEN substr(Playlists.name, 1, 5) = '_____' THEN 5"
            "  WHEN substr(Playlists.name, 1, 4) = '____' THEN 4"
            "  WHEN substr(Playlists.name, 1, 3) = '___' THEN 3"
            "  WHEN substr(Playlists.name, 1, 2) = '__' THEN 2"
            "  WHEN substr(Playlists.name, 1, 1) = '_' THEN 1"
            "  ELSE 0"
            " END DESC,"
            " Playlists.date_created DESC"));
    QSqlQuery query(database);
    if (!query.exec(queryString)) {
        LOG_FAILED_QUERY(query);
    }

    // Setup the sidebar playlist model
    QSqlTableModel playlistTableModel(this, database);
    playlistTableModel.setTable("PlaylistsCountsDurations");
    playlistTableModel.select();
    while (playlistTableModel.canFetchMore()) {
        playlistTableModel.fetchMore();
    }
    QSqlRecord record = playlistTableModel.record();
    int nameColumn = record.indexOf("name");
    int idColumn = record.indexOf("id");
    int countColumn = record.indexOf("count");
    int durationColumn = record.indexOf("durationSeconds");

    for (int row = 0; row < playlistTableModel.rowCount(); ++row) {
        int id =
                playlistTableModel
                        .data(playlistTableModel.index(row, idColumn))
                        .toInt();
        QString name =
                playlistTableModel
                        .data(playlistTableModel.index(row, nameColumn))
                        .toString();
        int count =
                playlistTableModel
                        .data(playlistTableModel.index(row, countColumn))
                        .toInt();
        int duration =
                playlistTableModel
                        .data(playlistTableModel.index(row, durationColumn))
                        .toInt();
        BasePlaylistFeature::IdAndLabel idAndLabel;
        idAndLabel.id = id;
        idAndLabel.label = createPlaylistLabel(name, count, duration);
        idAndLabel.name = name;
        playlistLabels.append(idAndLabel);
    }
    return playlistLabels;
}

void PlaylistFeature::slotShufflePlaylist() {
    int playlistId = playlistIdFromIndex(m_lastRightClickedIndex);
    if (playlistId == kInvalidPlaylistId) {
        return;
    }

    if (m_playlistDao.isPlaylistLocked(playlistId)) {
        qDebug() << "Can't shuffle locked playlist" << playlistId
                 << m_playlistDao.getPlaylistName(playlistId);
        return;
    }

    // Shuffle all tracks
    // If the playlist is loaded/visible shuffle only selected tracks
    QModelIndexList selection;
    if (isChildIndexSelectedInSidebar(m_lastRightClickedIndex) &&
            m_pPlaylistTableModel->getPlaylist() == playlistId) {
        if (m_pLibraryWidget) {
            WTrackTableView* view = dynamic_cast<WTrackTableView*>(
                    m_pLibraryWidget->getActiveView());
            if (view != nullptr) {
                selection = view->selectionModel()->selectedIndexes();
            }
        }
        m_pPlaylistTableModel->shuffleTracks(selection, QModelIndex());
    } else {
        // Create a temp model so we don't need to select the playlist
        // in the persistent model in order to shuffle it
        std::unique_ptr<PlaylistTableModel> pPlaylistTableModel =
                std::make_unique<PlaylistTableModel>(this,
                        m_pLibrary->trackCollectionManager(),
                        "mixxx.db.model.playlist_shuffle");
        pPlaylistTableModel->selectPlaylist(playlistId);
        pPlaylistTableModel->setSort(
                pPlaylistTableModel->fieldIndex(
                        ColumnCache::COLUMN_PLAYLISTTRACKSTABLE_POSITION),
                Qt::AscendingOrder);
        pPlaylistTableModel->select();

        pPlaylistTableModel->shuffleTracks(selection, QModelIndex());
    }
}

void PlaylistFeature::slotMarkAllTracksPlayed() {
    setAllTracksPlayedStatus(true);
}

void PlaylistFeature::slotMarkAllTracksUnplayed() {
    setAllTracksPlayedStatus(false);
}

void PlaylistFeature::setAllTracksPlayedStatus(bool played) {
    const int playlistId = playlistIdFromIndex(m_lastRightClickedIndex);
    if (playlistId == kInvalidPlaylistId) {
        return;
    }
    // Temporary model so we don't disturb the persistent selection/table view.
    std::unique_ptr<PlaylistTableModel> pPlaylistTableModel =
            std::make_unique<PlaylistTableModel>(this,
                    m_pLibrary->trackCollectionManager(),
                    "mixxx.db.model.playlist_mark_played");
    pPlaylistTableModel->selectPlaylist(playlistId);
    pPlaylistTableModel->select();
    const int rows = pPlaylistTableModel->rowCount();
    for (int i = 0; i < rows; ++i) {
        const QModelIndex index = pPlaylistTableModel->index(i, 0);
        if (index.isValid()) {
            TrackPointer pTrack = pPlaylistTableModel->getTrack(index);
            if (pTrack) {
                // Only set played status; leave the play count untouched.
                pTrack->updatePlayedStatusKeepPlayCount(played);
            }
        }
    }
}

void PlaylistFeature::slotUnlockAllPlaylists() {
    m_playlistDao.setPlaylistsLockedByType(PlaylistDAO::PLHT_NOT_HIDDEN, false);
}

void PlaylistFeature::slotDeleteAllUnlockedPlaylists() {
    // Collect playlists to display the count
    const QList<QPair<int, QString>> playlists =
            m_playlistDao.getUnlockedPlaylists(PlaylistDAO::PLHT_NOT_HIDDEN);
    if (playlists.size() <= 0) {
        return;
    }

    QMessageBox::StandardButton btn = QMessageBox::question(nullptr,
            tr("Confirm Deletion"),
            tr("Do you really want to delete all unlocked playlists?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
    if (btn != QMessageBox::Yes) {
        return;
    }

    btn = QMessageBox::question(nullptr,
            tr("Confirm Deletion"),
            tr("Deleting %1 unlocked playlists.<br>"
               "This operation can not be undone!")
                    .arg(playlists.size()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
    if (btn != QMessageBox::Yes) {
        return;
    }

    QStringList ids;
    for (const auto& playlist : std::as_const(playlists)) {
        ids << QString::number(playlist.first);
    }

    m_playlistDao.deleteUnlockedPlaylists(std::move(ids));
}

/// Purpose: When inserting or removing playlists,
/// we require the sidebar model not to reset.
/// This method queries the database and does dynamic insertion
/// @param selectedId entry which should be selected
// andy-custom: sidebar folder support. A playlist named "Folder/Playlist"
// is grouped under an expandable "Folder" node (one level only, split at
// the first '/'). Rename a playlist to move it in or out of a folder.
QString PlaylistFeature::sidebarFolderOfName(const QString& name) {
    const int slash = name.indexOf(QLatin1Char('/'));
    if (slash <= 0) {
        return QString();
    }
    const QString folder = name.left(slash).trimmed();
    const QString leaf = name.mid(slash + 1).trimmed();
    if (folder.isEmpty() || leaf.isEmpty()) {
        return QString();
    }
    return folder;
}

QString PlaylistFeature::createPlaylistLabel(
        const QString& name, int count, int duration) const {
    if (!sidebarFolderOfName(name).isEmpty()) {
        const QString leaf = name.mid(name.indexOf(QLatin1Char('/')) + 1).trimmed();
        return BasePlaylistFeature::createPlaylistLabel(leaf, count, duration);
    }
    return BasePlaylistFeature::createPlaylistLabel(name, count, duration);
}

bool PlaylistFeature::isFolderIndex(const QModelIndex& index) const {
    if (!index.isValid()) {
        return false;
    }
    TreeItem* pItem = static_cast<TreeItem*>(index.internalPointer());
    return pItem != nullptr && pItem->hasChildren() &&
            playlistIdFromIndex(index) == kInvalidPlaylistId;
}

QStringList PlaylistFeature::currentFolders() const {
    QStringList folders;
    TreeItem* pRootItem = m_pSidebarModel->getRootItem();
    if (pRootItem == nullptr) {
        return folders;
    }
    for (int row = 0; row < pRootItem->childRows(); ++row) {
        TreeItem* pChild = pRootItem->child(row);
        if (pChild->hasChildren()) {
            folders.append(pChild->getLabel());
        }
    }
    folders.sort(Qt::CaseInsensitive);
    return folders;
}

void PlaylistFeature::onLazyChildExpandation(const QModelIndex& index) {
    // The sidebar routes QTreeView::expanded here; record expanded folder
    // nodes so their state survives rebuilds and restarts.
    if (!isFolderIndex(index)) {
        return;
    }
    TreeItem* pItem = static_cast<TreeItem*>(index.internalPointer());
    const QString folder = pItem->getLabel();
    if (folder.isEmpty() || m_expandedFolders.contains(folder)) {
        return;
    }
    m_expandedFolders.insert(folder);
    saveExpandedFolders();
}

void PlaylistFeature::onChildCollapse(const QModelIndex& index) {
    if (m_rebuildingChildModel || !isFolderIndex(index)) {
        return;
    }
    TreeItem* pItem = static_cast<TreeItem*>(index.internalPointer());
    if (m_expandedFolders.remove(pItem->getLabel())) {
        saveExpandedFolders();
    }
}

void PlaylistFeature::renameItem(const QModelIndex& index) {
    if (isFolderIndex(index)) {
        m_lastRightClickedIndex = index;
        slotRenameFolder();
        return;
    }
    BasePlaylistFeature::renameItem(index);
}

void PlaylistFeature::slotRenameFolder() {
    if (!isFolderIndex(m_lastRightClickedIndex)) {
        return;
    }
    TreeItem* pItem =
            static_cast<TreeItem*>(m_lastRightClickedIndex.internalPointer());
    const QString oldFolder = pItem->getLabel();

    QString newFolder;
    while (true) {
        bool ok = false;
        newFolder = QInputDialog::getText(nullptr,
                tr("Rename Folder"),
                tr("Enter new name for folder:"),
                QLineEdit::Normal,
                oldFolder,
                &ok)
                            .trimmed();
        if (!ok || newFolder == oldFolder) {
            return;
        }
        if (newFolder.isEmpty()) {
            QMessageBox::warning(nullptr,
                    tr("Renaming Folder Failed"),
                    tr("A folder cannot have a blank name."));
        } else if (newFolder.contains(QLatin1Char('/'))) {
            QMessageBox::warning(nullptr,
                    tr("Renaming Folder Failed"),
                    tr("A folder name cannot contain '/'."));
        } else {
            break;
        }
    }
    renameFolderMembers(oldFolder, newFolder);
}

void PlaylistFeature::renameFolderMembers(
        const QString& oldFolder, const QString& newFolder) {
    // Collect the members first and check every rename before applying any,
    // so a conflict cannot split the folder halfway through.
    QList<QPair<int, QString>> renames;
    const QList<IdAndLabel> playlistLabels = createPlaylistLabels();
    for (const auto& idAndLabel : playlistLabels) {
        if (sidebarFolderOfName(idAndLabel.name) != oldFolder) {
            continue;
        }
        const QString leaf =
                idAndLabel.name.mid(idAndLabel.name.indexOf(QLatin1Char('/')) + 1)
                        .trimmed();
        const QString newName = newFolder + QLatin1Char('/') + leaf;
        if (m_playlistDao.isPlaylistLocked(idAndLabel.id)) {
            QMessageBox::warning(nullptr,
                    tr("Renaming Folder Failed"),
                    tr("The folder contains the locked playlist \"%1\". "
                       "Unlock it first.")
                            .arg(leaf));
            return;
        }
        const int existingId = m_playlistDao.getPlaylistIdFromName(newName);
        if (existingId != kInvalidPlaylistId && existingId != idAndLabel.id) {
            QMessageBox::warning(nullptr,
                    tr("Renaming Folder Failed"),
                    tr("A playlist named \"%1\" already exists.").arg(newName));
            return;
        }
        renames.append(qMakePair(idAndLabel.id, newName));
    }
    if (renames.isEmpty()) {
        return;
    }

    // Keep the expansion state with the renamed folder.
    if (m_expandedFolders.remove(oldFolder)) {
        m_expandedFolders.insert(newFolder);
        saveExpandedFolders();
    }
    for (const auto& [playlistId, newName] : std::as_const(renames)) {
        m_playlistDao.renamePlaylist(playlistId, newName);
    }
}

void PlaylistFeature::slotMoveToFolder(const QString& folder) {
    const int playlistId = playlistIdFromIndex(m_lastRightClickedIndex);
    if (playlistId == kInvalidPlaylistId ||
            m_playlistDao.isPlaylistLocked(playlistId)) {
        return;
    }
    const QString name = m_playlistDao.getPlaylistName(playlistId);
    QString leaf = name;
    if (!sidebarFolderOfName(name).isEmpty()) {
        leaf = name.mid(name.indexOf(QLatin1Char('/')) + 1).trimmed();
    }
    const QString newName =
            folder.isEmpty() ? leaf : folder + QLatin1Char('/') + leaf;
    if (newName == name) {
        return;
    }
    const int existingId = m_playlistDao.getPlaylistIdFromName(newName);
    if (existingId != kInvalidPlaylistId && existingId != playlistId) {
        QMessageBox::warning(nullptr,
                tr("Moving Playlist Failed"),
                tr("A playlist named \"%1\" already exists.").arg(newName));
        return;
    }
    // Expand the target folder so the moved playlist stays visible.
    if (!folder.isEmpty() && !m_expandedFolders.contains(folder)) {
        m_expandedFolders.insert(folder);
        saveExpandedFolders();
    }
    m_playlistDao.renamePlaylist(playlistId, newName);
}

void PlaylistFeature::slotMoveToNewFolder() {
    QString folder;
    while (true) {
        bool ok = false;
        folder = QInputDialog::getText(nullptr,
                tr("New Folder"),
                tr("Enter name for the new folder:"),
                QLineEdit::Normal,
                QString(),
                &ok)
                         .trimmed();
        if (!ok) {
            return;
        }
        if (folder.isEmpty()) {
            QMessageBox::warning(nullptr,
                    tr("Creating Folder Failed"),
                    tr("A folder cannot have a blank name."));
        } else if (folder.contains(QLatin1Char('/'))) {
            QMessageBox::warning(nullptr,
                    tr("Creating Folder Failed"),
                    tr("A folder name cannot contain '/'."));
        } else {
            break;
        }
    }
    slotMoveToFolder(folder);
}

void PlaylistFeature::restoreExpandedFolders() {
    if (!m_pSidebarWidget || m_expandedFolders.isEmpty()) {
        return;
    }
    TreeItem* pRootItem = m_pSidebarModel->getRootItem();
    if (pRootItem == nullptr) {
        return;
    }
    for (int row = 0; row < pRootItem->childRows(); ++row) {
        TreeItem* pChild = pRootItem->child(row);
        if (pChild->hasChildren() &&
                m_expandedFolders.contains(pChild->getLabel())) {
            m_pSidebarWidget->expandChildIndex(m_pSidebarModel->index(row, 0));
        }
    }
}

void PlaylistFeature::saveExpandedFolders() {
    QStringList folders(m_expandedFolders.begin(), m_expandedFolders.end());
    folders.sort(Qt::CaseInsensitive);
    // Folder names cannot contain '/', so it is a safe list separator.
    m_pConfig->setValue(ConfigKey("[PlaylistFeature]", "ExpandedFolders"),
            folders.join(QLatin1Char('/')));
}

QModelIndex PlaylistFeature::constructChildModel(int selectedId) {
    // qDebug() << "PlaylistFeature::constructChildModel() id:" << selectedId;
    std::vector<std::unique_ptr<TreeItem>> childrenToAdd;
    // Folder nodes carry no playlist id (invalid data), so activating them
    // or dropping tracks on them is a no-op. A folder sits at the position
    // of its first member in the tier/date sort; member order inside the
    // folder keeps the global sort.
    QHash<QString, TreeItem*> folders;

    const QList<IdAndLabel> playlistLabels = createPlaylistLabels();
    for (const auto& idAndLabel : playlistLabels) {
        int playlistId = idAndLabel.id;
        const QString folder = sidebarFolderOfName(idAndLabel.name);

        TreeItem* pItem;
        if (folder.isEmpty()) {
            // Create the TreeItem whose parent is the invisible root item
            auto pNewItem = std::make_unique<TreeItem>(idAndLabel.label, playlistId);
            pItem = pNewItem.get();
            childrenToAdd.push_back(std::move(pNewItem));
        } else {
            TreeItem* pFolderItem = folders.value(folder, nullptr);
            if (pFolderItem == nullptr) {
                auto pNewFolderItem = std::make_unique<TreeItem>(folder);
                pFolderItem = pNewFolderItem.get();
                folders.insert(folder, pFolderItem);
                childrenToAdd.push_back(std::move(pNewFolderItem));
            }
            pItem = pFolderItem->appendChild(idAndLabel.label, playlistId);
        }
        pItem->setBold(m_playlistIdsOfSelectedTrack.contains(playlistId));
        decorateChild(pItem, playlistId);
    }

    // Append all the newly created TreeItems in a dynamic way to the childmodel
    m_pSidebarModel->insertTreeItemRows(std::move(childrenToAdd), 0);
    // The rebuild collapsed all folder nodes; re-apply the stored state.
    restoreExpandedFolders();
    return indexFromPlaylistId(selectedId);
}

void PlaylistFeature::decorateChild(TreeItem* item, int playlistId) {
    if (m_playlistDao.isPlaylistLocked(playlistId)) {
        item->setIcon(
                QIcon(":/images/library/ic_library_locked_tracklist.svg"));
    } else {
        item->setIcon(QIcon());
    }
}

void PlaylistFeature::slotPlaylistTableChanged(int playlistId) {
    // qDebug() << "PlaylistFeature::slotPlaylistTableChanged() playlistId:" << playlistId;
    enum PlaylistDAO::HiddenType type = m_playlistDao.getHiddenType(playlistId);
    if (type != PlaylistDAO::PLHT_NOT_HIDDEN &&  // not a regular playlist
            type != PlaylistDAO::PLHT_UNKNOWN) { // not a deleted playlist
        return;
    }

    // Store current selection
    int selectedPlaylistId = kInvalidPlaylistId;
    if (isChildIndexSelectedInSidebar(m_lastClickedIndex)) {
        if (playlistId == playlistIdFromIndex(m_lastClickedIndex) &&
                type == PlaylistDAO::PLHT_UNKNOWN) {
            // if the selected playlist was deleted, find a sibling to select
            selectedPlaylistId = getSiblingPlaylistIdOf(m_lastClickedIndex);
        } else {
            // just restore the current selection
            selectedPlaylistId = playlistIdFromIndex(m_lastClickedIndex);
        }
    }

    // Collapse events fired while rows are torn down must not clear the
    // stored folder expansion state.
    m_rebuildingChildModel = true;
    clearChildModel();
    QModelIndex newIndex = constructChildModel(selectedPlaylistId);
    m_rebuildingChildModel = false;
    if (selectedPlaylistId != kInvalidPlaylistId && newIndex.isValid()) {
        // If a child index was selected and we got a new valid index select that.
        // Else (root item was selected or for some reason no index could be created)
        // there's nothing to do: either no child was selected earlier, or the root
        // was selected and will remain selected after the child model was rebuilt.
        activateChild(newIndex);
        emit featureSelect(this, newIndex);
    }
}

void PlaylistFeature::slotPlaylistContentOrLockChanged(const QSet<int>& playlistIds) {
    // qDebug() << "PlaylistFeature::slotPlaylistContentOrLockChanged() playlistId:" << playlistIds;
    QSet<int> idsToBeUpdated;
    for (const auto playlistId : std::as_const(playlistIds)) {
        if (m_playlistDao.getHiddenType(playlistId) == PlaylistDAO::PLHT_NOT_HIDDEN) {
            idsToBeUpdated.insert(playlistId);
        }
    }
    // Update the playlists set to allow toggling bold correctly after
    // tracks have been dropped on sidebar items
    m_playlistDao.getPlaylistsTrackIsIn(m_selectedTrackId, &m_playlistIdsOfSelectedTrack);
    updateChildModel(idsToBeUpdated);
}

void PlaylistFeature::slotPlaylistTableRenamed(int playlistId, const QString& newName) {
    Q_UNUSED(newName);
    // qDebug() << "PlaylistFeature::slotPlaylistTableRenamed() playlistId:" << playlistId;
    if (m_playlistDao.getHiddenType(playlistId) == PlaylistDAO::PLHT_NOT_HIDDEN) {
        // Maybe we need to re-sort the sidebar items, so call slotPlaylistTableChanged()
        // in order to rebuild the model, not just updateChildModel()
        slotPlaylistTableChanged(playlistId);
    }
}

QString PlaylistFeature::getRootViewHtml() const {
    QString playlistsTitle = tr("Playlists");
    QString playlistsSummary =
            tr("Playlists are ordered lists of tracks that allow you to plan "
               "your DJ sets.");
    QString playlistsSummary2 =
            tr("Some DJs construct playlists before they perform live, but "
               "others prefer to build them on-the-fly.");
    QString playlistsSummary3 =
            tr("When using a playlist during a live DJ set, remember to always "
               "pay close attention to how your audience reacts to the music "
               "you've chosen to play.");
    QString playlistsSummary4 =
            tr("It may be necessary to skip some tracks in your prepared "
               "playlist or add some different tracks in order to maintain the "
               "energy of your audience.");
    QString createPlaylistLink = tr("Create New Playlist");

    QString html;
    html.append(QStringLiteral("<h2>%1</h2>").arg(playlistsTitle));
    html.append(QStringLiteral("<p>%1</p>").arg(playlistsSummary));
    html.append(QStringLiteral("<p>%1</p>").arg(playlistsSummary2));
    html.append(QStringLiteral("<p>%1<br>%2</p>").arg(playlistsSummary3, playlistsSummary4));
    html.append(QStringLiteral("<a style=\"color:#0496FF;\" href=\"create\">%1</a>")
                        .arg(createPlaylistLink));
    return html;
}
