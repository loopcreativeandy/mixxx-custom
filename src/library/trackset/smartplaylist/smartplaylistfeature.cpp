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

#include "control/controlobject.h"
#include "library/library.h"
#include "library/trackset/smartplaylist/keyreference.h"
#include "track/keyutils.h"
#include "library/trackcollectionmanager.h"
#include "library/treeitem.h"
#include "moc_smartplaylistfeature.cpp"
#include "widget/wlibrary.h"
#include "widget/wlibrarytextbrowser.h"

namespace {

const QString kRootViewName = QStringLiteral("SMARTPLAYLISTHOME");

// CP133: sidebar data of the three built-in key lists. Regular smart playlists
// carry their int index, so these strings never collide with them.
const QString kKeyLeverPrefix = QStringLiteral("andykey:");
// Library::m_sTrackViewName (private there).
const QString kTrackViewName = QStringLiteral("WTrackTableView");
// LateNight has at most four decks; the ring controls must exist before the
// skin is parsed, so they are created up front.
constexpr int kKeyReferenceDecks = 4;
// Matches no track: the key column holds key names, never this.
const QString kNoReferenceQuery = QStringLiteral("key:\"andy-no-reference-deck\"");

} // anonymous namespace

SmartPlaylistFeature::SmartPlaylistFeature(Library* pLibrary, UserSettingsPointer pConfig)
        : BaseTrackSetFeature(pLibrary,
                  pConfig,
                  kRootViewName,
                  QStringLiteral("playlist")),
          m_smartPlaylistTableModel(this, pLibrary->trackCollectionManager()) {
    initActions();

    m_playlists = SmartPlaylistStorage::load();
    seedCuePlaylistsOnce(pConfig);

    for (int i = 0; i < kKeyReferenceDecks; ++i) {
        m_keyReferenceControls.push_back(std::make_unique<ControlObject>(
                ConfigKey(QStringLiteral("[Channel%1]").arg(i + 1),
                        QStringLiteral("andy_key_reference"))));
    }
    connect(pLibrary,
            &Library::showTrackModel,
            this,
            &SmartPlaylistFeature::slotLibraryShowTrackModel);
    connect(pLibrary,
            &Library::switchToView,
            this,
            &SmartPlaylistFeature::slotLibrarySwitchToView);

    m_pSidebarModel->setRootItem(TreeItem::newRoot(this));
    rebuildChildModel();
}

SmartPlaylistFeature::~SmartPlaylistFeature() = default;

// andy-custom CP131 (IDEA-15/16/18): the cue-based smart playlists are added
// once. A flag in mixxx.cfg remembers that, so a playlist Andy deletes or
// renames later does not come back; existing names are never overwritten.
void SmartPlaylistFeature::seedCuePlaylistsOnce(UserSettingsPointer pConfig) {
    const ConfigKey seededKey(QStringLiteral("[Library]"),
            QStringLiteral("AndyCueSmartPlaylistsSeeded"));
    if (!pConfig || pConfig->getValue(seededKey, 0) >= 1) {
        return;
    }
    const QList<SmartPlaylistDefinition> seeds = {
            {QStringLiteral("Cues: long loops"), QStringLiteral("cues:longloop")},
            {QStringLiteral("Cues: untagged"), QStringLiteral("cues:none")},
            {QStringLiteral("Cues: tagged"), QStringLiteral("cues:any")},
            {QStringLiteral("Cues: properly tagged"), QStringLiteral("cues:proper")},
            {QStringLiteral("Cues: to tag (S/A)"),
                    QStringLiteral("comment:SSSSS -cues:proper | "
                                   "comment:AAAAA -cues:proper")},
    };
    bool changed = false;
    for (const SmartPlaylistDefinition& seed : seeds) {
        const bool exists = std::any_of(m_playlists.cbegin(),
                m_playlists.cend(),
                [&seed](const SmartPlaylistDefinition& playlist) {
                    return playlist.name.compare(seed.name, Qt::CaseInsensitive) == 0;
                });
        if (!exists) {
            m_playlists.append(seed);
            changed = true;
        }
    }
    if (changed) {
        std::sort(m_playlists.begin(),
                m_playlists.end(),
                [](const SmartPlaylistDefinition& lhs, const SmartPlaylistDefinition& rhs) {
                    return lhs.name.compare(rhs.name, Qt::CaseInsensitive) < 0;
                });
        if (!SmartPlaylistStorage::save(m_playlists)) {
            // Keep them for this session; try again next start.
            return;
        }
    }
    pConfig->setValue(seededKey, 1);
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
    rows.reserve(andykey::kLeverCount + m_playlists.size());
    for (int lever = 0; lever < andykey::kLeverCount; ++lever) {
        rows.push_back(std::make_unique<TreeItem>(
                andykey::leverLabel(static_cast<andykey::Lever>(lever)),
                QVariant(kKeyLeverPrefix + QString::number(lever))));
    }
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

int SmartPlaylistFeature::keyLeverFromModelIndex(const QModelIndex& index) const {
    if (!index.isValid()) {
        return -1;
    }
    TreeItem* pItem = m_pSidebarModel->getItem(index);
    if (pItem == nullptr || pItem->isRoot()) {
        return -1;
    }
    const QVariant data = pItem->getData();
    if (data.typeId() != QMetaType::QString) {
        return -1;
    }
    const QString text = data.toString();
    if (!text.startsWith(kKeyLeverPrefix)) {
        return -1;
    }
    bool ok = false;
    const int lever = text.mid(kKeyLeverPrefix.size()).toInt(&ok);
    if (!ok || lever < 0 || lever >= andykey::kLeverCount) {
        return -1;
    }
    return lever;
}

void SmartPlaylistFeature::setKeyReferenceDeck(int deckIndex) {
    for (int i = 0; i < static_cast<int>(m_keyReferenceControls.size()); ++i) {
        m_keyReferenceControls[i]->set(i == deckIndex ? 1.0 : 0.0);
    }
}

void SmartPlaylistFeature::activateKeyLever(int lever, const QModelIndex& index) {
    const auto keyLever = static_cast<andykey::Lever>(lever);
    const int numDecks = static_cast<int>(
            ControlObject::get(ConfigKey(QStringLiteral("[App]"), QStringLiteral("num_decks"))));
    QList<andykey::DeckState> decks;
    for (int i = 0; i < numDecks; ++i) {
        const QString group = QStringLiteral("[Channel%1]").arg(i + 1);
        andykey::DeckState deck;
        deck.loaded = ControlObject::get(ConfigKey(group, QStringLiteral("track_loaded"))) > 0.0;
        deck.playing = ControlObject::get(ConfigKey(group, QStringLiteral("play"))) > 0.0;
        deck.volume = ControlObject::get(ConfigKey(group, QStringLiteral("volume")));
        deck.orientation = static_cast<int>(
                ControlObject::get(ConfigKey(group, QStringLiteral("orientation"))));
        // "key" is the current key, pitch and key shift included.
        deck.key = KeyUtils::keyFromNumericValue(
                ControlObject::get(ConfigKey(group, QStringLiteral("key"))));
        decks.append(deck);
    }
    const double crossfader =
            ControlObject::get(ConfigKey(QStringLiteral("[Master]"), QStringLiteral("crossfader")));
    const int deckIndex = andykey::pickReferenceDeck(decks, crossfader);

    QString label = andykey::leverLabel(keyLever);
    QString query = kNoReferenceQuery;
    if (deckIndex < 0) {
        label += tr(" \u00B7 no deck loaded");
    } else {
        const auto key = decks.at(deckIndex).key;
        const auto keys = andykey::matchingKeys(key, keyLever);
        if (keys.isEmpty()) {
            label += tr(" \u00B7 Deck %1 has no key").arg(deckIndex + 1);
        } else {
            label += QStringLiteral(" \u00B7 %1 (Deck %2)")
                             .arg(KeyUtils::keyToString(key))
                             .arg(deckIndex + 1);
            query = andykey::searchQueryForKeys(keys);
        }
    }
    qInfo() << "andy-custom CP133 key smart playlist" << label << query;

    TreeItem* pItem = m_pSidebarModel->getItem(index);
    if (pItem != nullptr) {
        pItem->setLabel(label);
        m_pSidebarModel->triggerRepaint(index);
    }
    m_activeKeyLever = lever;
    setKeyReferenceDeck(deckIndex);
    m_smartPlaylistTableModel.selectSmartPlaylist(query);
}

void SmartPlaylistFeature::slotLibraryShowTrackModel(QAbstractItemModel* pModel) {
    if (pModel != &m_smartPlaylistTableModel || m_activeKeyLever < 0) {
        m_activeKeyLever = -1;
        setKeyReferenceDeck(-1);
    }
}

void SmartPlaylistFeature::slotLibrarySwitchToView(const QString& view) {
    // Every track model is followed by a switch to the track view; any other
    // view (a feature's root page, the browse pane, ...) hides the list.
    if (view != kTrackViewName) {
        m_activeKeyLever = -1;
        setKeyReferenceDeck(-1);
    }
}

void SmartPlaylistFeature::activateChild(const QModelIndex& index) {
    const int lever = keyLeverFromModelIndex(index);
    if (lever >= 0) {
        m_lastRightClickedIndex = QModelIndex();
        emit saveModelState();
        activateKeyLever(lever, index);
        emit showTrackModel(&m_smartPlaylistTableModel);
        emit enableCoverArtDisplay(true);
        return;
    }
    const int row = playlistIndexFromModelIndex(index);
    if (row < 0) {
        return;
    }
    m_lastRightClickedIndex = QModelIndex();
    m_activeKeyLever = -1;
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
