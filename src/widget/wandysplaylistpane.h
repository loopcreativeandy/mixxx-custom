#pragma once

#include <QWidget>

#include "preferences/usersettings.h"
#include "widget/wbasewidget.h"

class QLabel;
class QTimer;
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
    /// @param paneId distinguishes several panes living in the same skin.
    /// Everything this pane persists — column layout, the open playlist, the
    /// spoiler-mode flag — and its drag/drop identity are keyed by it, so two
    /// panes never overwrite each other's state. Empty = the original pane.
    WAndysPlaylistPane(QWidget* pParent,
            UserSettingsPointer pConfig,
            Library* pLibrary,
            KeyboardEventFilter* pKeyboard,
            double backgroundColorOpacity,
            const QString& paneId = QString());

  public slots:
    void slotOpenPlaylist(int playlistId);

  private slots:
    void slotPlaylistsChanged();
    void slotUnloadPlaylist();
    void slotToggleSpoilerMode();
    /// Kick the coalescing timer that (re-)runs the spoiler filter. Every model
    /// signal goes through here instead of calling applySpoilerFilter()
    /// directly — see the comment on applySpoilerFilter().
    void scheduleSpoilerFilter();
    /// Refresh the "summed duration (count)" readout in the header from the
    /// current table selection. Mirrors Auto DJ's selection-info label.
    void updateSelectionInfo();
    /// A header section was moved/resized/hidden or the sort changed. Kicks the
    /// debounce timer so we persist the layout without hammering the DB during
    /// a live drag.
    void slotHeaderLayoutChanged();

  private:
    void openPlaylist(int playlistId);
    void updateHeader();
    /// Connect the current track-table header's change signals to the debounced
    /// save. Safe to call repeatedly (uses unique connections); the header is
    /// recreated whenever a different model is loaded.
    void wireHeaderPersistence();
    /// Spoiler-free mode: when enabled, only rows whose track has already been
    /// played this session plus the first not-yet-played row (the next song)
    /// are visible; everything further down is hidden so the set has no
    /// spoilers on camera. The model keeps every track — this only hides rows
    /// in the view, so the decks still see the full playlist.
    ///
    /// MUST NOT be invoked directly from a model signal — that re-entrancy is
    /// what froze Mixxx on the splash screen (measured: 64k track re-imports
    /// and a pegged GUI thread in 90 s). Always go through
    /// scheduleSpoilerFilter().
    void applySpoilerFilter();

    UserSettingsPointer m_pConfig;
    Library* m_pLibrary;
    const ConfigKey m_lastPlaylistConfigKey;
    const ConfigKey m_spoilerModeConfigKey;
    /// Owns the bytes behind the `const char*` settings namespace we hand to
    /// PlaylistTableModel — it keeps the pointer, not a copy.
    const QByteArray m_settingsNamespace;
    QWidget* m_pHeaderRow;
    QLabel* m_pHeader;
    QLabel* m_pSelectionInfo;
    QToolButton* m_pEjectButton;
    QToolButton* m_pSpoilerButton;
    WTrackTableView* m_pTrackTableView;
    PlaylistTableModel* m_pModel;
    QTimer* m_pHeaderSaveTimer;
    QTimer* m_pSpoilerFilterTimer;
    int m_currentPlaylistId;
    bool m_spoilerMode;
    bool m_inSpoilerFilter;
};
