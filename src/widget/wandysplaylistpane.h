#pragma once

#include <QWidget>

#include "preferences/usersettings.h"
#include "widget/wbasewidget.h"

class QComboBox;
class Library;
class KeyboardEventFilter;
class WTrackTableView;
class PlaylistTableModel;

/// Andy's second playlist pane: a standalone track table bound to one
/// playlist, selectable via a dropdown, living next to the main library so
/// two playlists can be open at once and tracks can be dragged between them.
class WAndysPlaylistPane : public QWidget, public WBaseWidget {
    Q_OBJECT
  public:
    WAndysPlaylistPane(QWidget* pParent,
            UserSettingsPointer pConfig,
            Library* pLibrary,
            KeyboardEventFilter* pKeyboard,
            double backgroundColorOpacity);

  private slots:
    void slotComboActivated(int comboIndex);
    void slotPlaylistsChanged();

  private:
    void populatePlaylists();
    void openPlaylist(int playlistId);

    UserSettingsPointer m_pConfig;
    Library* m_pLibrary;
    QComboBox* m_pPlaylistCombo;
    WTrackTableView* m_pTrackTableView;
    PlaylistTableModel* m_pModel;
};
