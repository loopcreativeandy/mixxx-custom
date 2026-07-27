#pragma once

#include <QWidget>

#include "preferences/usersettings.h"
#include "widget/wbasewidget.h"

class QLabel;
class QToolButton;
class Library;
class KeyboardEventFilter;
class WTrackTableView;
class PlaylistTableModel;

/// Andy's second playlist pane: a standalone track table bound to one
/// playlist, living next to the main library so two playlists can be open at
/// once and tracks can be dragged between them. Filled via the sidebar
/// right-click action "Open in side pane"; a plain header shows which
/// playlist is loaded.
class WAndysPlaylistPane : public QWidget, public WBaseWidget {
    Q_OBJECT
  public:
    WAndysPlaylistPane(QWidget* pParent,
            UserSettingsPointer pConfig,
            Library* pLibrary,
            KeyboardEventFilter* pKeyboard,
            double backgroundColorOpacity);

  public slots:
    void slotOpenPlaylist(int playlistId);

  private slots:
    void slotPlaylistsChanged();
    void slotUnloadPlaylist();

  private:
    void openPlaylist(int playlistId);
    void updateHeader();

    UserSettingsPointer m_pConfig;
    Library* m_pLibrary;
    QWidget* m_pHeaderRow;
    QLabel* m_pHeader;
    QToolButton* m_pEjectButton;
    WTrackTableView* m_pTrackTableView;
    PlaylistTableModel* m_pModel;
    int m_currentPlaylistId;
};
