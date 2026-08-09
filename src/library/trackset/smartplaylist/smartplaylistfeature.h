#pragma once

#include <QList>
#include <QModelIndex>
#include <QPointer>
#include <QVariant>

#include "library/trackset/basetracksetfeature.h"
#include "library/trackset/smartplaylist/smartplayliststorage.h"
#include "library/trackset/smartplaylist/smartplaylisttablemodel.h"
#include "preferences/usersettings.h"
#include "util/parented_ptr.h"

class Library;
class WLibrary;
class WLibraryTextBrowser;

/// Sidebar root for smart playlists (andy-custom).
///
/// A smart playlist is a named library search query that re-evaluates itself
/// whenever it is opened, so tracks that gain (or lose) the matching tags come
/// and go on their own. Definitions live in a plain text file, not in
/// mixxxdb.sqlite - see SmartPlaylistStorage.
class SmartPlaylistFeature : public BaseTrackSetFeature {
    Q_OBJECT
  public:
    SmartPlaylistFeature(Library* pLibrary, UserSettingsPointer pConfig);
    ~SmartPlaylistFeature() override = default;

    QVariant title() override;

    void bindLibraryWidget(WLibrary* pLibraryWidget,
            KeyboardEventFilter* pKeyboard) override;

    TreeItemModel* sidebarModel() const override;

    bool hasTrackTable() override {
        return true;
    }

  public slots:
    void activateChild(const QModelIndex& index) override;
    void onRightClick(const QPoint& globalPos) override;
    void onRightClickChild(const QPoint& globalPos, const QModelIndex& index) override;
    void renameItem(const QModelIndex& index) override;
    void deleteItem(const QModelIndex& index) override;

  private slots:
    void slotCreateSmartPlaylist();
    void slotEditSmartPlaylistQuery();
    void slotRenameSmartPlaylist();
    void slotDeleteSmartPlaylist();
    void htmlLinkClicked(const QUrl& link);

  private:
    void initActions();
    void rebuildChildModel();
    /// Index into m_playlists for a sidebar index, or -1.
    int playlistIndexFromModelIndex(const QModelIndex& index) const;
    /// Asks for a query string; returns false if the dialog was cancelled.
    bool askForQuery(const QString& title, const QString& initialQuery, QString* pQuery);
    /// Asks for a name that is not yet taken; returns false if cancelled.
    /// `skipIndex` is the entry allowed to keep its own name (-1 when creating).
    bool askForName(const QString& title,
            const QString& initialName,
            int skipIndex,
            QString* pName);
    bool nameExists(const QString& name, int skipIndex) const;
    void saveAndRebuild();
    QString getRootViewHtml() const;

    SmartPlaylistTableModel m_smartPlaylistTableModel;
    QList<SmartPlaylistDefinition> m_playlists;

    parented_ptr<QAction> m_pCreateSmartPlaylistAction;
    parented_ptr<QAction> m_pEditQueryAction;
    parented_ptr<QAction> m_pRenameAction;
    parented_ptr<QAction> m_pDeleteAction;

    QModelIndex m_lastRightClickedIndex;
    QPointer<WLibrary> m_pLibraryWidget;
};
