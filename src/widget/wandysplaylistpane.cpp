#include "widget/wandysplaylistpane.h"

#include <QLabel>
#include <QVBoxLayout>

#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/columncache.h"
#include "library/dao/playlistdao.h"
#include "library/library.h"
#include "library/playlisttablemodel.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "moc_wandysplaylistpane.cpp"
#include "widget/wtracktableview.h"

namespace {
const ConfigKey kLastPlaylistConfigKey("[AndysPlaylistPane]", "playlist_id");
const QString kNoPlaylistLabel = QStringLiteral("no playlist — right-click one in the sidebar");
} // anonymous namespace

WAndysPlaylistPane::WAndysPlaylistPane(QWidget* pParent,
        UserSettingsPointer pConfig,
        Library* pLibrary,
        KeyboardEventFilter* pKeyboard,
        double backgroundColorOpacity)
        : QWidget(pParent),
          WBaseWidget(this),
          m_pConfig(pConfig),
          m_pLibrary(pLibrary),
          m_pHeader(new QLabel(this)),
          m_pTrackTableView(new WTrackTableView(this,
                  pConfig,
                  pLibrary,
                  backgroundColorOpacity)),
          m_pModel(new PlaylistTableModel(this,
                  pLibrary->trackCollectionManager(),
                  "mixxx.db.model.andys_pane")),
          m_currentPlaylistId(-1) {
    setObjectName(QStringLiteral("AndysPlaylistPane"));
    m_pHeader->setObjectName(QStringLiteral("AndysPaneHeader"));
    // Inline fallback style so the header never renders as a white system
    // widget; skins can still override via the ObjectName.
    m_pHeader->setStyleSheet(QStringLiteral(
            "QLabel#AndysPaneHeader {"
            " background-color: #1e1e26; color: #ddba71;"
            " font-weight: bold; padding: 3px 6px; }"));
    updateHeader();

    QVBoxLayout* pLayout = new QVBoxLayout(this);
    pLayout->setContentsMargins(0, 0, 0, 0);
    pLayout->setSpacing(2);
    pLayout->addWidget(m_pHeader);
    pLayout->addWidget(m_pTrackTableView, 1);

    m_pTrackTableView->installEventFilter(pKeyboard);

    // Route double-click / deck-load requests through the Library like the
    // main track table does.
    connect(m_pTrackTableView,
            &WTrackTableView::loadTrack,
            m_pLibrary,
            &Library::slotLoadTrack);
    connect(m_pTrackTableView,
            &WTrackTableView::loadTrackToPlayer,
            m_pLibrary,
            &Library::slotLoadTrackToPlayer);

    // Live font / row height / click-behavior updates from the preferences.
    connect(m_pLibrary,
            &Library::setTrackTableFont,
            m_pTrackTableView,
            &WTrackTableView::setTrackTableFont);
    connect(m_pLibrary,
            &Library::setTrackTableRowHeight,
            m_pTrackTableView,
            &WTrackTableView::setTrackTableRowHeight);
    connect(m_pLibrary,
            &Library::setSelectedClick,
            m_pTrackTableView,
            &WTrackTableView::setSelectedClick);

    // This pane is created after Library::bindLibraryWidget() already emitted
    // the initial font/row-height signals, so replicate the initial values
    // from the config directly.
    QFont trackTableFont;
    const QString fontStr =
            m_pConfig->getValueString(ConfigKey("[Library]", "Font"));
    if (!fontStr.isEmpty() && trackTableFont.fromString(fontStr)) {
        m_pTrackTableView->setTrackTableFont(trackTableFont);
    }
    const int rowHeight = m_pConfig->getValue(
            ConfigKey("[Library]", "RowHeight"), Library::kDefaultRowHeightPx);
    m_pTrackTableView->setTrackTableRowHeight(rowHeight);

    // Sidebar right-click "Open in side pane" lands here.
    connect(m_pLibrary,
            &Library::openPlaylistInSidePane,
            this,
            &WAndysPlaylistPane::slotOpenPlaylist);

    PlaylistDAO& playlistDao =
            m_pLibrary->trackCollectionManager()->internalCollection()->getPlaylistDAO();
    connect(&playlistDao, &PlaylistDAO::deleted, this, &WAndysPlaylistPane::slotPlaylistsChanged);
    connect(&playlistDao, &PlaylistDAO::renamed, this, &WAndysPlaylistPane::slotPlaylistsChanged);

    // Restore the playlist that was open last time.
    const int lastPlaylistId = m_pConfig->getValue(kLastPlaylistConfigKey, -1);
    if (lastPlaylistId >= 0 &&
            !playlistDao.getPlaylistName(lastPlaylistId).isEmpty()) {
        openPlaylist(lastPlaylistId);
    }
}

void WAndysPlaylistPane::slotOpenPlaylist(int playlistId) {
    if (playlistId < 0) {
        return;
    }
    openPlaylist(playlistId);
    m_pConfig->setValue(kLastPlaylistConfigKey, playlistId);
}

void WAndysPlaylistPane::slotPlaylistsChanged() {
    if (m_currentPlaylistId < 0) {
        return;
    }
    PlaylistDAO& playlistDao =
            m_pLibrary->trackCollectionManager()->internalCollection()->getPlaylistDAO();
    if (playlistDao.getPlaylistName(m_currentPlaylistId).isEmpty()) {
        // Current playlist was deleted.
        m_currentPlaylistId = -1;
    }
    updateHeader();
}

void WAndysPlaylistPane::openPlaylist(int playlistId) {
    m_pModel->selectPlaylist(playlistId);
    m_pModel->setSort(
            m_pModel->fieldIndex(
                    ColumnCache::COLUMN_PLAYLISTTRACKSTABLE_POSITION),
            Qt::AscendingOrder);
    m_pModel->select();
    m_pTrackTableView->loadTrackModel(m_pModel);
    m_currentPlaylistId = playlistId;
    updateHeader();
}

void WAndysPlaylistPane::updateHeader() {
    if (m_currentPlaylistId < 0) {
        m_pHeader->setText(kNoPlaylistLabel);
        return;
    }
    const QString name = m_pLibrary->trackCollectionManager()
                                 ->internalCollection()
                                 ->getPlaylistDAO()
                                 .getPlaylistName(m_currentPlaylistId);
    m_pHeader->setText(name);
}
