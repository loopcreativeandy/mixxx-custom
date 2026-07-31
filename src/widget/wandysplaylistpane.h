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
    void slotToggleSpoilerMode();

  private:
    void openPlaylist(int playlistId);
    void updateHeader();
    /// Spoiler-free mode: when enabled, only rows whose track has already been
    /// played this session plus the first not-yet-played row (the next song)
    /// are visible; everything further down is hidden so the set has no
    /// spoilers on camera. The model keeps every track — this only hides rows
    /// in the view, so the decks still see the full playlist.
    void applySpoilerFilter();

    UserSettingsPointer m_pConfig;
    Library* m_pLibrary;
    QWidget* m_pHeaderRow;
    QLabel* m_pHeader;
    QToolButton* m_pEjectButton;
    QToolButton* m_pSpoilerButton;
    WTrackTableView* m_pTrackTableView;
    PlaylistTableModel* m_pModel;
    int m_currentPlaylistId;
    bool m_spoilerMode;
};
