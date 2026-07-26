#include "widget/wandysplaylistpane.h"

#include <QComboBox>
#include <QSqlError>
#include <QSqlQuery>
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
const QString kNoPlaylistLabel = QStringLiteral("— playlist —");
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
          m_pPlaylistCombo(new QComboBox(this)),
          m_pTrackTableView(new WTrackTableView(this,
                  pConfig,
                  pLibrary,
                  backgroundColorOpacity)),
          m_pModel(new PlaylistTableModel(this,
                  pLibrary->trackCollectionManager(),
                  "mixxx.db.model.andys_pane")) {
    setObjectName(QStringLiteral("AndysPlaylistPane"));
    m_pPlaylistCombo->setObjectName(QStringLiteral("AndysPlaylistSelector"));
    m_pPlaylistCombo->setMaxVisibleItems(30);

    QVBoxLayout* pLayout = new QVBoxLayout(this);
    pLayout->setContentsMargins(0, 0, 0, 0);
    pLayout->setSpacing(2);
    pLayout->addWidget(m_pPlaylistCombo);
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

    PlaylistDAO& playlistDao =
            m_pLibrary->trackCollectionManager()->internalCollection()->getPlaylistDAO();
    connect(&playlistDao, &PlaylistDAO::added, this, &WAndysPlaylistPane::slotPlaylistsChanged);
    connect(&playlistDao, &PlaylistDAO::deleted, this, &WAndysPlaylistPane::slotPlaylistsChanged);
    connect(&playlistDao, &PlaylistDAO::renamed, this, &WAndysPlaylistPane::slotPlaylistsChanged);

    connect(m_pPlaylistCombo,
            QOverload<int>::of(&QComboBox::activated),
            this,
            &WAndysPlaylistPane::slotComboActivated);

    populatePlaylists();

    // Restore the playlist that was open last time.
    const int lastPlaylistId = m_pConfig->getValue(kLastPlaylistConfigKey, -1);
    if (lastPlaylistId >= 0) {
        const int comboIndex = m_pPlaylistCombo->findData(lastPlaylistId);
        if (comboIndex >= 0) {
            m_pPlaylistCombo->setCurrentIndex(comboIndex);
            openPlaylist(lastPlaylistId);
        }
    }
}

void WAndysPlaylistPane::populatePlaylists() {
    const int selectedId =
            m_pPlaylistCombo->currentData().isValid()
            ? m_pPlaylistCombo->currentData().toInt()
            : -1;
    m_pPlaylistCombo->blockSignals(true);
    m_pPlaylistCombo->clear();
    m_pPlaylistCombo->addItem(kNoPlaylistLabel, -1);

    // Same tier ordering as the sidebar patch: leading '_' count (capped at
    // 5) pins playlists on top, newest first within a tier. substr() instead
    // of LIKE because '_' is a LIKE wildcard.
    QSqlQuery query(m_pLibrary->trackCollectionManager()
                            ->internalCollection()
                            ->database());
    query.prepare(QStringLiteral(
            "SELECT id, name FROM Playlists WHERE hidden = 0"
            " ORDER BY "
            " CASE"
            "  WHEN substr(name, 1, 5) = '_____' THEN 5"
            "  WHEN substr(name, 1, 4) = '____' THEN 4"
            "  WHEN substr(name, 1, 3) = '___' THEN 3"
            "  WHEN substr(name, 1, 2) = '__' THEN 2"
            "  WHEN substr(name, 1, 1) = '_' THEN 1"
            "  ELSE 0"
            " END DESC,"
            " date_created DESC"));
    if (query.exec()) {
        while (query.next()) {
            m_pPlaylistCombo->addItem(
                    query.value(1).toString(), query.value(0).toInt());
        }
    } else {
        qWarning() << "WAndysPlaylistPane: playlist query failed"
                   << query.lastError();
    }

    const int comboIndex = m_pPlaylistCombo->findData(selectedId);
    m_pPlaylistCombo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);
    m_pPlaylistCombo->blockSignals(false);
}

void WAndysPlaylistPane::slotPlaylistsChanged() {
    populatePlaylists();
}

void WAndysPlaylistPane::slotComboActivated(int comboIndex) {
    const int playlistId = m_pPlaylistCombo->itemData(comboIndex).toInt();
    if (playlistId < 0) {
        return;
    }
    openPlaylist(playlistId);
    m_pConfig->setValue(kLastPlaylistConfigKey, playlistId);
}

void WAndysPlaylistPane::openPlaylist(int playlistId) {
    m_pModel->selectPlaylist(playlistId);
    m_pModel->setSort(
            m_pModel->fieldIndex(
                    ColumnCache::COLUMN_PLAYLISTTRACKSTABLE_POSITION),
            Qt::AscendingOrder);
    m_pModel->select();
    m_pTrackTableView->loadTrackModel(m_pModel);
}
