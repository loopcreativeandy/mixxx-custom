#include "widget/wandysplaylistpane.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
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
          m_pHeaderRow(new QWidget(this)),
          m_pHeader(new QLabel(m_pHeaderRow)),
          m_pEjectButton(new QToolButton(m_pHeaderRow)),
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

    // Eject: clears the pane so it's pure dead space again (Andy stands in
    // front of it on video). The whole header row hides with it.
    m_pEjectButton->setObjectName(QStringLiteral("AndysPaneEject"));
    m_pEjectButton->setText(QStringLiteral("✕"));
    m_pEjectButton->setToolTip(tr("Unload playlist from this pane"));
    m_pEjectButton->setAutoRaise(true);
    m_pEjectButton->setCursor(Qt::PointingHandCursor);
    m_pEjectButton->setStyleSheet(QStringLiteral(
            "QToolButton#AndysPaneEject {"
            " background-color: #1e1e26; color: #ddba71;"
            " border: none; font-weight: bold; padding: 3px 8px; }"
            "QToolButton#AndysPaneEject:hover { color: #ffffff; }"));
    connect(m_pEjectButton,
            &QToolButton::clicked,
            this,
            &WAndysPlaylistPane::slotUnloadPlaylist);

    QHBoxLayout* pHeaderLayout = new QHBoxLayout(m_pHeaderRow);
    pHeaderLayout->setContentsMargins(0, 0, 0, 0);
    pHeaderLayout->setSpacing(0);
    pHeaderLayout->addWidget(m_pHeader, 1);
    pHeaderLayout->addWidget(m_pEjectButton);
    updateHeader();

    QVBoxLayout* pLayout = new QVBoxLayout(this);
    pLayout->setContentsMargins(0, 0, 0, 0);
    pLayout->setSpacing(2);
    pLayout->addWidget(m_pHeaderRow);
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
        // Current playlist was deleted: clear the table too, not just the
        // header, so no stale rows linger.
        slotUnloadPlaylist();
        return;
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

void WAndysPlaylistPane::slotUnloadPlaylist() {
    if (m_currentPlaylistId < 0) {
        return;
    }
    // Not kInvalidPlaylistId (-1): the model names its temp view
    // "playlist_<id>" and later uses that unquoted in SQL, so a negative id
    // breaks the query. Id 0 never exists (SQLite ids start at 1) and yields
    // a clean empty view.
    m_pModel->selectPlaylist(0);
    m_pModel->select();
    m_currentPlaylistId = -1;
    m_pConfig->setValue(kLastPlaylistConfigKey, -1);
    updateHeader();
}

void WAndysPlaylistPane::updateHeader() {
    if (m_currentPlaylistId < 0) {
        // Nothing loaded: no text, no bar — clean dead space (Andy's
        // green-screen face sits here on video).
        m_pHeader->setText(QString());
        m_pHeaderRow->setVisible(false);
        return;
    }
    m_pHeaderRow->setVisible(true);
    const QString name = m_pLibrary->trackCollectionManager()
                                 ->internalCollection()
                                 ->getPlaylistDAO()
                                 .getPlaylistName(m_currentPlaylistId);
    m_pHeader->setText(name);
}
