#include "library/trackset/smartplaylist/smartplaylistfeature.h"

#include <QAction>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QUrl>
#include <algorithm>
#include <memory>
#include <vector>

#include "library/library.h"
#include "library/trackcollectionmanager.h"
#include "library/treeitem.h"
#include "moc_smartplaylistfeature.cpp"
#include "widget/wlibrary.h"
#include "widget/wlibrarytextbrowser.h"

namespace {

const QString kRootViewName = QStringLiteral("SMARTPLAYLISTHOME");

} // anonymous namespace

SmartPlaylistFeature::SmartPlaylistFeature(Library* pLibrary, UserSettingsPointer pConfig)
        : BaseTrackSetFeature(pLibrary,
                  pConfig,
                  kRootViewName,
                  QStringLiteral("playlist")),
          m_smartPlaylistTableModel(this, pLibrary->trackCollectionManager()) {
    initActions();

    m_playlists = SmartPlaylistStorage::load();

    m_pSidebarModel->setRootItem(TreeItem::newRoot(this));
    rebuildChildModel();
}

void SmartPlaylistFeature::initActions() {
    m_pCreateSmartPlaylistAction =
            make_parented<QAction>(tr("New Smart Playlist"), this);
    connect(m_pCreateSmartPlaylistAction.get(),
            &QAction::triggered,
            this,
            &SmartPlaylistFeature::slotCreateSmartPlaylist);

    m_pEditQueryAction = make_parented<QAction>(tr("Edit Search Query"), this);
    connect(m_pEditQueryAction.get(),
            &QAction::triggered,
            this,
            &SmartPlaylistFeature::slotEditSmartPlaylistQuery);

    m_pRenameAction = make_parented<QAction>(tr("Rename"), this);
    connect(m_pRenameAction.get(),
            &QAction::triggered,
            this,
            &SmartPlaylistFeature::slotRenameSmartPlaylist);

    m_pDeleteAction = make_parented<QAction>(tr("Remove"), this);
    connect(m_pDeleteAction.get(),
            &QAction::triggered,
            this,
            &SmartPlaylistFeature::slotDeleteSmartPlaylist);
}

QVariant SmartPlaylistFeature::title() {
    return tr("Smart Playlists");
}

TreeItemModel* SmartPlaylistFeature::sidebarModel() const {
    return m_pSidebarModel;
}

void SmartPlaylistFeature::bindLibraryWidget(WLibrary* pLibraryWidget,
        KeyboardEventFilter* pKeyboard) {
    Q_UNUSED(pKeyboard);
    WLibraryTextBrowser* pEdit = new WLibraryTextBrowser(pLibraryWidget);
    pEdit->setHtml(getRootViewHtml());
    pEdit->setOpenLinks(false);
    connect(pEdit,
            &WLibraryTextBrowser::anchorClicked,
            this,
            &SmartPlaylistFeature::htmlLinkClicked);
    m_pLibraryWidget = QPointer(pLibraryWidget);
    m_pLibraryWidget->registerView(kRootViewName, pEdit);
}

void SmartPlaylistFeature::htmlLinkClicked(const QUrl& link) {
    if (link.path() == QLatin1String("create")) {
        slotCreateSmartPlaylist();
    } else {
        qDebug() << "Unknown smart playlist link clicked" << link.path();
    }
}

QString SmartPlaylistFeature::getRootViewHtml() const {
    const QString title = tr("Smart Playlists");
    const QString description =
            tr("Smart playlists are saved searches. Instead of holding a fixed "
               "list of tracks they store a search query and re-run it every "
               "time you open them, so tracks appear and disappear as your "
               "tags change.");
    const QString searchHint =
            tr("They use the same search syntax as the search bar, for example "
               "<i>Zouk | Zoukable</i> for either tag, or <i>bpm:80-95 "
               "-Dembow</i>. Typing in the search bar filters within the smart "
               "playlist.");
    const QString createLink = tr("Create New Smart Playlist");

    return QStringLiteral(
            "<html><body><h2>%1</h2><p>%2</p><p>%3</p>"
            "<a href=\"create\">%4</a></body></html>")
            .arg(title, description, searchHint, createLink);
}

int SmartPlaylistFeature::playlistIndexFromModelIndex(const QModelIndex& index) const {
    if (!index.isValid()) {
        return -1;
    }
    TreeItem* pItem = m_pSidebarModel->getItem(index);
    if (pItem == nullptr || pItem->isRoot()) {
        return -1;
    }
    bool ok = false;
    const int row = pItem->getData().toInt(&ok);
    if (!ok || row < 0 || row >= m_playlists.size()) {
        return -1;
    }
    return row;
}

void SmartPlaylistFeature::rebuildChildModel() {
    m_lastRightClickedIndex = QModelIndex();
    m_pSidebarModel->setRootItem(TreeItem::newRoot(this));

    std::vector<std::unique_ptr<TreeItem>> rows;
    rows.reserve(m_playlists.size());
    for (int i = 0; i < m_playlists.size(); ++i) {
        rows.push_back(std::make_unique<TreeItem>(m_playlists.at(i).name, QVariant(i)));
    }
    if (!rows.empty()) {
        m_pSidebarModel->insertTreeItemRows(std::move(rows), 0);
    }
}

void SmartPlaylistFeature::saveAndRebuild() {
    if (!SmartPlaylistStorage::save(m_playlists)) {
        QMessageBox::warning(nullptr,
                tr("Smart Playlists"),
                tr("Could not save the smart playlists to\n%1")
                        .arg(SmartPlaylistStorage::filePath()));
    }
    rebuildChildModel();
}

bool SmartPlaylistFeature::nameExists(const QString& name, int skipIndex) const {
    for (int i = 0; i < m_playlists.size(); ++i) {
        if (i == skipIndex) {
            continue;
        }
        if (m_playlists.at(i).name.compare(name, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

bool SmartPlaylistFeature::askForName(const QString& title,
        const QString& initialName,
        int skipIndex,
        QString* pName) {
    QString name = initialName;
    while (true) {
        bool ok = false;
        name = QInputDialog::getText(nullptr,
                title,
                tr("Enter name for the smart playlist:"),
                QLineEdit::Normal,
                name,
                &ok)
                       .trimmed();
        if (!ok) {
            return false;
        }
        if (name.isEmpty()) {
            QMessageBox::warning(nullptr,
                    title,
                    tr("A smart playlist cannot have a blank name."));
            continue;
        }
        // The name is written as a [section] header, so it must stay on one
        // line and must not be mistaken for one.
        if (name.startsWith(QChar('[')) && name.endsWith(QChar(']'))) {
            QMessageBox::warning(nullptr,
                    title,
                    tr("A smart playlist name cannot be wrapped in square "
                       "brackets."));
            continue;
        }
        if (nameExists(name, skipIndex)) {
            QMessageBox::warning(nullptr,
                    title,
                    tr("A smart playlist by that name already exists."));
            continue;
        }
        *pName = name;
        return true;
    }
}

bool SmartPlaylistFeature::askForQuery(const QString& title,
        const QString& initialQuery,
        QString* pQuery) {
    QString query = initialQuery;
    while (true) {
        bool ok = false;
        query = QInputDialog::getText(nullptr,
                title,
                tr("Enter the search query, e.g. Zouk | Zoukable"),
                QLineEdit::Normal,
                query,
                &ok)
                        .trimmed();
        if (!ok) {
            return false;
        }
        if (query.isEmpty()) {
            // An empty query would quietly match the whole library.
            QMessageBox::warning(nullptr,
                    title,
                    tr("A smart playlist needs a search query."));
            continue;
        }
        *pQuery = query;
        return true;
    }
}

void SmartPlaylistFeature::slotCreateSmartPlaylist() {
    const QString title = tr("New Smart Playlist");
    QString name;
    if (!askForName(title, tr("New Smart Playlist"), -1, &name)) {
        return;
    }
    QString query;
    if (!askForQuery(title, QString(), &query)) {
        return;
    }

    SmartPlaylistDefinition playlist;
    playlist.name = name;
    playlist.query = query;
    m_playlists.append(playlist);
    std::sort(m_playlists.begin(),
            m_playlists.end(),
            [](const SmartPlaylistDefinition& lhs, const SmartPlaylistDefinition& rhs) {
                return lhs.name.compare(rhs.name, Qt::CaseInsensitive) < 0;
            });
    saveAndRebuild();
}

void SmartPlaylistFeature::slotEditSmartPlaylistQuery() {
    const int row = playlistIndexFromModelIndex(m_lastRightClickedIndex);
    if (row < 0) {
        return;
    }
    QString query;
    if (!askForQuery(tr("Edit Search Query"), m_playlists.at(row).query, &query)) {
        return;
    }
    if (query == m_playlists.at(row).query) {
        return;
    }
    m_playlists[row].query = query;
    saveAndRebuild();
}

void SmartPlaylistFeature::slotRenameSmartPlaylist() {
    const int row = playlistIndexFromModelIndex(m_lastRightClickedIndex);
    if (row < 0) {
        return;
    }
    QString name;
    if (!askForName(tr("Rename Smart Playlist"), m_playlists.at(row).name, row, &name)) {
        return;
    }
    if (name == m_playlists.at(row).name) {
        return;
    }
    m_playlists[row].name = name;
    std::sort(m_playlists.begin(),
            m_playlists.end(),
            [](const SmartPlaylistDefinition& lhs, const SmartPlaylistDefinition& rhs) {
                return lhs.name.compare(rhs.name, Qt::CaseInsensitive) < 0;
            });
    saveAndRebuild();
}

void SmartPlaylistFeature::slotDeleteSmartPlaylist() {
    const int row = playlistIndexFromModelIndex(m_lastRightClickedIndex);
    if (row < 0) {
        return;
    }
    const QMessageBox::StandardButton btn = QMessageBox::question(nullptr,
            tr("Confirm Removal"),
            tr("Do you really want to remove the smart playlist <b>%1</b>?<br>"
               "The tracks it shows are not touched.")
                    .arg(m_playlists.at(row).name.toHtmlEscaped()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
    if (btn == QMessageBox::No) {
        return;
    }
    m_playlists.removeAt(row);
    saveAndRebuild();
}

void SmartPlaylistFeature::activateChild(const QModelIndex& index) {
    const int row = playlistIndexFromModelIndex(index);
    if (row < 0) {
        return;
    }
    m_lastRightClickedIndex = QModelIndex();
    emit saveModelState();
    // Re-evaluating here is what makes the playlist "smart": it is the single
    // refresh point, so the view never changes underneath a running set.
    m_smartPlaylistTableModel.selectSmartPlaylist(m_playlists.at(row).query);
    emit showTrackModel(&m_smartPlaylistTableModel);
    emit enableCoverArtDisplay(true);
}

void SmartPlaylistFeature::onRightClick(const QPoint& globalPos) {
    m_lastRightClickedIndex = QModelIndex();
    QMenu menu(nullptr);
    menu.addAction(m_pCreateSmartPlaylistAction.get());
    menu.exec(globalPos);
}

void SmartPlaylistFeature::onRightClickChild(const QPoint& globalPos, const QModelIndex& index) {
    m_lastRightClickedIndex = index;
    if (playlistIndexFromModelIndex(index) < 0) {
        return;
    }
    QMenu menu(nullptr);
    menu.addAction(m_pCreateSmartPlaylistAction.get());
    menu.addSeparator();
    menu.addAction(m_pEditQueryAction.get());
    menu.addAction(m_pRenameAction.get());
    menu.addSeparator();
    menu.addAction(m_pDeleteAction.get());
    menu.exec(globalPos);
}

void SmartPlaylistFeature::renameItem(const QModelIndex& index) {
    m_lastRightClickedIndex = index;
    slotRenameSmartPlaylist();
}

void SmartPlaylistFeature::deleteItem(const QModelIndex& index) {
    m_lastRightClickedIndex = index;
    slotDeleteSmartPlaylist();
}
