#include "library/boxsync/boxsync.h"

#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <limits>

#include "library/trackcollectionmanager.h"
#include "moc_boxsync.cpp"
#include "track/track.h"
#include "track/trackref.h"
#include "util/logger.h"

namespace {

const mixxx::Logger kLogger("BoxSync");

constexpr int kConnectTimeoutMs = 3000;   // per URL, tried once at startup
constexpr int kStallTimeoutMs = 30000;    // no bytes for this long → abort
constexpr int kPageSize = 2000;
constexpr int kApplyBatch = 40;           // songs per event-loop tick (keeps the UI alive)
constexpr int kAutoCloseMs = 4000;        // dialog closes itself when nothing needs attention
const QString kPartSuffix = QStringLiteral(".boxsync-part");

QString configPath(const UserSettingsPointer& pConfig) {
    return QDir(pConfig->getSettingsPath()).filePath(QStringLiteral("boxsync.json"));
}

QString statePath(const UserSettingsPointer& pConfig) {
    return QDir(pConfig->getSettingsPath()).filePath(QStringLiteral("boxsync_state.json"));
}

// Box → Mixxx fields (Andy 2026-11-18). BPM/key go the other way (phase 2).
struct FieldBinding {
    const char* name;
    QString (Track::*get)() const;
    void (Track::*set)(const QString&);
};
const FieldBinding kFields[] = {
        {"comment", &Track::getComment, &Track::setComment},
        {"grouping", &Track::getGrouping, &Track::setGrouping},
        {"composer", &Track::getComposer, &Track::setComposer},
};

} // namespace

BoxSync::BoxSync(TrackCollectionManager* pTrackCollectionManager,
        UserSettingsPointer pConfig,
        QWidget* pParentWindow)
        : QObject(pParentWindow),
          m_pTrackCollectionManager(pTrackCollectionManager),
          m_pConfig(std::move(pConfig)),
          m_pParentWindow(pParentWindow),
          m_pNetwork(new QNetworkAccessManager(this)),
          m_state(State::Idle),
          m_urlIndex(0),
          m_sawUnauthorized(false),
          m_lastRev(0),
          m_serverRev(0),
          m_minFailedRev(std::numeric_limits<qint64>::max()),
          m_applyIndex(0),
          m_downloadIndex(0),
          m_pDownloadFile(nullptr),
          m_updatedTracks(0),
          m_downloaded(0),
          m_skippedExisting(0),
          m_failedDownloads(0),
          m_declinedDownloads(0) {
}

BoxSync::~BoxSync() {
    if (m_pDownloadReply) {
        m_pDownloadReply->abort();
    }
    if (m_pDownloadFile) {
        // unfinished download: drop the part file, the target was never touched
        m_pDownloadFile->close();
        m_pDownloadFile->remove();
        delete m_pDownloadFile;
        m_pDownloadFile = nullptr;
    }
    if (m_pDialog) {
        delete m_pDialog;
    }
}

bool BoxSync::isConfigured() const {
    return QFileInfo::exists(configPath(m_pConfig));
}

bool BoxSync::loadConfig() {
    QFile file(configPath(m_pConfig));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QJsonParseError err;
    const QJsonObject obj = QJsonDocument::fromJson(file.readAll(), &err).object();
    if (err.error != QJsonParseError::NoError) {
        setState(State::Failed, tr("boxsync.json is not valid JSON: %1").arg(err.errorString()));
        return false;
    }
    m_urls.clear();
    for (const auto& url : obj.value(QStringLiteral("urls")).toArray()) {
        QString u = url.toString().trimmed();
        while (u.endsWith('/')) {
            u.chop(1);
        }
        if (!u.isEmpty()) {
            m_urls.append(u);
        }
    }
    m_token = obj.value(QStringLiteral("token")).toString().toUtf8();
    m_libraryRoot = QDir::fromNativeSeparators(
            obj.value(QStringLiteral("library_root")).toString());
    while (m_libraryRoot.endsWith('/')) {
        m_libraryRoot.chop(1);
    }
    if (m_urls.isEmpty() || m_token.isEmpty() || m_libraryRoot.isEmpty()) {
        setState(State::Failed, tr("boxsync.json needs \"urls\", \"token\" and \"library_root\"."));
        return false;
    }
    return true;
}

void BoxSync::loadState() {
    m_lastRev = 0;
    QFile file(statePath(m_pConfig));
    if (file.open(QIODevice::ReadOnly)) {
        m_lastRev = QJsonDocument::fromJson(file.readAll())
                            .object()
                            .value(QStringLiteral("last_rev"))
                            .toInteger(0);
    }
}

void BoxSync::saveState(qint64 lastRev) {
    QFile file(statePath(m_pConfig));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        kLogger.warning() << "cannot write" << file.fileName();
        return;
    }
    QJsonObject obj;
    obj.insert(QStringLiteral("last_rev"), lastRev);
    obj.insert(QStringLiteral("box"), m_baseUrl);
    file.write(QJsonDocument(obj).toJson());
}

QString BoxSync::localPathFor(const QString& relpath) const {
    return QDir::cleanPath(m_libraryRoot + QChar('/') + relpath);
}

void BoxSync::start() {
    if (m_state == State::Connecting || m_state == State::Fetching ||
            m_state == State::Applying || m_state == State::Downloading ||
            m_state == State::AwaitingConsent) {
        showDialog();
        return;
    }
    if (!loadConfig()) {
        showDialog();
        return;
    }
    loadState();
    m_songs.clear();
    m_downloads.clear();
    m_applyIndex = 0;
    m_downloadIndex = 0;
    m_updatedTracks = 0;
    m_downloaded = 0;
    m_skippedExisting = 0;
    m_failedDownloads = 0;
    m_declinedDownloads = 0;
    m_minFailedRev = std::numeric_limits<qint64>::max();
    m_urlIndex = 0;
    m_sawUnauthorized = false;
    m_baseUrl.clear();
    showDialog();
    tryNextUrl();
}

void BoxSync::tryNextUrl() {
    if (m_urlIndex >= m_urls.size()) {
        if (m_sawUnauthorized) {
            setState(State::Failed, tr("The box rejected the token in boxsync.json."));
            return;
        }
        // Andy: try once, then wait for the "Sync with box now" button.
        setState(State::Unreachable, tr("Box not reachable."));
        QTimer::singleShot(kAutoCloseMs, this, [this] {
            if (m_state == State::Unreachable && m_pDialog) {
                m_pDialog->hide();
            }
        });
        return;
    }
    const QString base = m_urls.at(m_urlIndex);
    setState(State::Connecting, tr("Connecting to the box (%1 of %2)…").arg(m_urlIndex + 1).arg(m_urls.size()));
    QNetworkRequest request(QUrl(base + QStringLiteral("/api/info")));
    request.setRawHeader("Authorization", "Bearer " + m_token);
    request.setTransferTimeout(kConnectTimeoutMs);
    QNetworkReply* pReply = m_pNetwork->get(request);
    connect(pReply, &QNetworkReply::finished, this, [this, pReply, base] {
        pReply->deleteLater();
        const int status = pReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (pReply->error() == QNetworkReply::NoError && status == 200) {
            m_baseUrl = base;
            kLogger.info() << "box reachable at url #" << m_urlIndex + 1;
            fetchChanges(m_lastRev);
            return;
        }
        if (status == 401) {
            // could be some other service on that address — keep trying the rest
            m_sawUnauthorized = true;
        }
        kLogger.info() << "url #" << m_urlIndex + 1 << "not reachable:" << pReply->errorString();
        ++m_urlIndex;
        tryNextUrl();
    });
}

void BoxSync::fetchChanges(qint64 since) {
    setState(State::Fetching, tr("Fetching tag changes from the box…"));
    QUrl url(m_baseUrl + QStringLiteral("/api/changes"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("since"), QString::number(since));
    query.addQueryItem(QStringLiteral("limit"), QString::number(kPageSize));
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + m_token);
    request.setTransferTimeout(kStallTimeoutMs);
    QNetworkReply* pReply = m_pNetwork->get(request);
    connect(pReply, &QNetworkReply::finished, this, [this, pReply] {
        pReply->deleteLater();
        if (pReply->error() != QNetworkReply::NoError) {
            setState(State::Failed, tr("Lost the box while fetching: %1").arg(pReply->errorString()));
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(pReply->readAll()).object();
        m_serverRev = obj.value(QStringLiteral("rev")).toInteger();
        for (const auto& song : obj.value(QStringLiteral("songs")).toArray()) {
            m_songs.append(song.toObject());
        }
        if (obj.value(QStringLiteral("more")).toBool()) {
            fetchChanges(obj.value(QStringLiteral("next")).toInteger());
            return;
        }
        kLogger.info() << m_songs.size() << "changed songs since rev" << m_lastRev
                       << "→ box rev" << m_serverRev;
        setState(State::Applying, tr("Applying tags…"));
        QTimer::singleShot(0, this, &BoxSync::applyBatch);
    });
}

bool BoxSync::applySongToLocalFile(const QJsonObject& song, const QString& localPath) {
    const TrackPointer pTrack = m_pTrackCollectionManager->getOrAddTrack(
            TrackRef::fromFilePath(localPath));
    if (!pTrack) {
        return false;
    }
    bool changed = false;
    for (const auto& field : kFields) {
        const QString boxValue = song.value(QLatin1String(field.name)).toString();
        if (boxValue.isEmpty()) {
            continue; // the box knows nothing → keep what Mixxx has
        }
        if (((*pTrack).*(field.get))() != boxValue) {
            ((*pTrack).*(field.set))(boxValue);
            changed = true;
        }
    }
    if (changed) {
        m_pTrackCollectionManager->saveTrack(pTrack);
    }
    return changed;
}

void BoxSync::applyBatch() {
    const int end = std::min(m_applyIndex + kApplyBatch, static_cast<int>(m_songs.size()));
    for (; m_applyIndex < end; ++m_applyIndex) {
        const QJsonObject& song = m_songs.at(m_applyIndex);
        const qint64 songRev = song.value(QStringLiteral("rev")).toInteger();
        for (const auto& fileValue : song.value(QStringLiteral("files")).toArray()) {
            const QJsonObject file = fileValue.toObject();
            if (file.value(QStringLiteral("missing")).toInt() != 0) {
                continue;
            }
            const QString localPath = localPathFor(file.value(QStringLiteral("relpath")).toString());
            if (QFileInfo::exists(localPath)) {
                if (applySongToLocalFile(song, localPath)) {
                    ++m_updatedTracks;
                }
            } else {
                m_downloads.append(Download{file.value(QStringLiteral("id")).toInt(),
                        localPath,
                        file.value(QStringLiteral("size")).toInteger(),
                        song,
                        songRev});
            }
        }
    }
    updateUi();
    if (m_applyIndex < m_songs.size()) {
        QTimer::singleShot(0, this, &BoxSync::applyBatch);
        return;
    }
    if (m_downloads.isEmpty()) {
        finish();
        return;
    }
    // Andy 2026-11-18: tags always sync; downloads only after he says yes.
    qint64 totalBytes = 0;
    for (const auto& dl : m_downloads) {
        totalBytes += std::max<qint64>(dl.size, 0);
    }
    kLogger.info() << m_downloads.size() << "songs to download," << totalBytes << "bytes — asking";
    setState(State::AwaitingConsent,
            tr("Tags are synced (%n track(s) updated).", "", m_updatedTracks) + QChar('\n') +
                    tr("%n new song(s) on the box, %1 GB in total. Download them now?", "", m_downloads.size())
                            .arg(totalBytes / 1073741824.0, 0, 'f', 2));
    showDialog();
}

void BoxSync::acceptDownloads() {
    if (m_state != State::AwaitingConsent) {
        return;
    }
    setState(State::Downloading, tr("Downloading new songs…"));
    downloadNext();
}

void BoxSync::onSecondaryButton() {
    if (m_state == State::AwaitingConsent) {
        // "Not now": keep the tags, re-offer these songs on the next sync.
        m_declinedDownloads = static_cast<int>(m_downloads.size());
        for (const auto& dl : m_downloads) {
            m_minFailedRev = std::min(m_minFailedRev, dl.songRev);
        }
        m_downloads.clear();
        finish();
    }
    if (m_pDialog) {
        m_pDialog->hide();
    }
}

void BoxSync::downloadNext() {
    if (m_downloadIndex >= m_downloads.size()) {
        finish();
        return;
    }
    const Download& dl = m_downloads.at(m_downloadIndex);
    if (QFileInfo::exists(dl.localPath)) { // appeared meanwhile → never overwrite
        ++m_skippedExisting;
        ++m_downloadIndex;
        QTimer::singleShot(0, this, &BoxSync::downloadNext);
        return;
    }
    QDir().mkpath(QFileInfo(dl.localPath).absolutePath());
    m_pDownloadFile = new QFile(dl.localPath + kPartSuffix);
    if (!m_pDownloadFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        kLogger.warning() << "cannot write" << m_pDownloadFile->fileName();
        delete m_pDownloadFile;
        m_pDownloadFile = nullptr;
        ++m_failedDownloads;
        m_minFailedRev = std::min(m_minFailedRev, dl.songRev);
        ++m_downloadIndex;
        QTimer::singleShot(0, this, &BoxSync::downloadNext);
        return;
    }
    QNetworkRequest request(QUrl(m_baseUrl + QStringLiteral("/api/file/") + QString::number(dl.fileId)));
    request.setRawHeader("Authorization", "Bearer " + m_token);
    request.setTransferTimeout(kStallTimeoutMs);
    m_pDownloadReply = m_pNetwork->get(request);
    QNetworkReply* pReply = m_pDownloadReply;
    connect(pReply, &QNetworkReply::readyRead, this, [this, pReply] {
        if (m_pDownloadFile) {
            m_pDownloadFile->write(pReply->readAll());
        }
    });
    connect(pReply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (m_pDetailLabel && total > 0) {
            m_pDetailLabel->setText(tr("Song %1 of %2 — %3 of %4 MB")
                            .arg(m_downloadIndex + 1)
                            .arg(m_downloads.size())
                            .arg(received / 1048576.0, 0, 'f', 1)
                            .arg(total / 1048576.0, 0, 'f', 1));
        }
    });
    connect(pReply, &QNetworkReply::finished, this, [this, pReply] {
        pReply->deleteLater();
        const Download dl = m_downloads.at(m_downloadIndex);
        m_pDownloadFile->write(pReply->readAll());
        m_pDownloadFile->close();
        const bool ok = pReply->error() == QNetworkReply::NoError &&
                (dl.size <= 0 || m_pDownloadFile->size() == dl.size);
        if (ok && !QFileInfo::exists(dl.localPath) && m_pDownloadFile->rename(dl.localPath)) {
            ++m_downloaded;
            applySongToLocalFile(dl.song, dl.localPath);
        } else {
            if (QFileInfo::exists(dl.localPath)) {
                ++m_skippedExisting; // someone put a file there meanwhile — leave it alone
            } else {
                ++m_failedDownloads;
                m_minFailedRev = std::min(m_minFailedRev, dl.songRev);
                kLogger.warning() << "download failed:" << dl.localPath << pReply->errorString();
            }
            m_pDownloadFile->remove();
        }
        delete m_pDownloadFile;
        m_pDownloadFile = nullptr;
        ++m_downloadIndex;
        updateUi();
        downloadNext();
    });
}

void BoxSync::finish() {
    // Failed downloads: resume just before the oldest affected song next time,
    // so it gets re-sent (re-applying tags is a no-op).
    const qint64 newRev = (m_failedDownloads > 0 || m_declinedDownloads > 0)
            ? m_minFailedRev - 1
            : m_serverRev;
    saveState(std::max(m_lastRev, newRev));
    QStringList parts;
    parts << tr("%n track(s) updated", "", m_updatedTracks);
    if (m_downloaded > 0) {
        parts << tr("%n song(s) downloaded", "", m_downloaded);
    }
    if (m_skippedExisting > 0) {
        parts << tr("%n file(s) already there, left untouched", "", m_skippedExisting);
    }
    if (m_failedDownloads > 0) {
        parts << tr("%n download(s) failed, retried next sync", "", m_failedDownloads);
    }
    if (m_declinedDownloads > 0) {
        parts << tr("%n new song(s) not downloaded, offered again next sync", "", m_declinedDownloads);
    }
    kLogger.info() << "sync done:" << parts.join(QStringLiteral(", "));
    setState(State::Done, tr("Synced with the box.") + QChar('\n') + parts.join(QStringLiteral(", ")));
    if (m_updatedTracks == 0 && m_downloaded == 0 && m_failedDownloads == 0 &&
            m_skippedExisting == 0 && m_declinedDownloads == 0) {
        QTimer::singleShot(kAutoCloseMs, this, [this] {
            if (m_state == State::Done && m_pDialog) {
                m_pDialog->hide();
            }
        });
    }
}

void BoxSync::setState(State state, const QString& message) {
    m_state = state;
    m_message = message;
    if (state == State::Failed) {
        kLogger.warning() << message;
    }
    updateUi();
}

void BoxSync::ensureDialog() {
    if (m_pDialog) {
        return;
    }
    auto* pDialog = new QDialog(m_pParentWindow);
    pDialog->setWindowTitle(tr("Box sync"));
    pDialog->setMinimumWidth(440);
    m_pStatusLabel = new QLabel(pDialog);
    m_pStatusLabel->setWordWrap(true);
    m_pDetailLabel = new QLabel(pDialog);
    m_pProgress = new QProgressBar(pDialog);
    m_pBackgroundButton = new QPushButton(tr("Run in background"), pDialog);
    m_pRetryButton = new QPushButton(tr("Try to connect again"), pDialog);
    m_pDownloadButton = new QPushButton(tr("Download"), pDialog);
    m_pDownloadButton->setDefault(true);
    connect(m_pBackgroundButton, &QPushButton::clicked, this, &BoxSync::onSecondaryButton);
    connect(m_pDownloadButton, &QPushButton::clicked, this, &BoxSync::acceptDownloads);
    connect(m_pRetryButton, &QPushButton::clicked, this, &BoxSync::start);
    auto* pButtons = new QHBoxLayout;
    pButtons->addStretch();
    pButtons->addWidget(m_pRetryButton);
    pButtons->addWidget(m_pDownloadButton);
    pButtons->addWidget(m_pBackgroundButton);
    auto* pLayout = new QVBoxLayout(pDialog);
    pLayout->addWidget(m_pStatusLabel);
    pLayout->addWidget(m_pProgress);
    pLayout->addWidget(m_pDetailLabel);
    pLayout->addLayout(pButtons);
    m_pDialog = pDialog;
}

void BoxSync::showDialog() {
    ensureDialog();
    updateUi();
    m_pDialog->show();
    m_pDialog->raise();
}

void BoxSync::updateUi() {
    const bool busy = m_state == State::Connecting || m_state == State::Fetching ||
            m_state == State::Applying || m_state == State::Downloading;
    int percent = -1;
    if (m_state == State::Applying && !m_songs.isEmpty()) {
        percent = static_cast<int>(100LL * m_applyIndex / m_songs.size());
    } else if (m_state == State::Downloading && !m_downloads.isEmpty()) {
        percent = static_cast<int>(100LL * m_downloadIndex / m_downloads.size());
    }
    if (m_pDialog) {
        m_pStatusLabel->setText(m_message);
        if (busy && percent < 0) {
            m_pProgress->setRange(0, 0); // busy indicator
        } else {
            m_pProgress->setRange(0, 100);
            m_pProgress->setValue(busy ? percent : 100);
        }
        m_pProgress->setVisible(busy || m_state == State::Done);
        if (m_state == State::Applying) {
            m_pDetailLabel->setText(tr("Song %1 of %2").arg(m_applyIndex).arg(m_songs.size()));
        } else if (!busy) {
            m_pDetailLabel->clear();
        }
        m_pRetryButton->setVisible(m_state == State::Unreachable || m_state == State::Failed);
        m_pDownloadButton->setVisible(m_state == State::AwaitingConsent);
        m_pBackgroundButton->setText(m_state == State::AwaitingConsent
                        ? tr("Not now")
                        : (busy ? tr("Run in background") : tr("Close")));
    }
    if (m_pMenu) {
        QString title;
        switch (m_state) {
        case State::Idle:
            title = tr("Box");
            break;
        case State::Connecting:
        case State::Fetching:
            title = tr("Box: connecting…");
            break;
        case State::Applying:
        case State::Downloading:
            title = tr("Box: syncing %1%").arg(std::max(percent, 0));
            break;
        case State::AwaitingConsent:
            title = tr("Box: %n new song(s)", "", m_downloads.size());
            break;
        case State::Done:
            title = tr("Box: synced");
            break;
        case State::Unreachable:
            title = tr("Box: not reachable");
            break;
        case State::Failed:
            title = tr("Box: sync failed");
            break;
        }
        m_pMenu->setTitle(title);
    }
}

void BoxSync::attachToMenuBar(QMenuBar* pMenuBar) {
    if (!pMenuBar || pMenuBar == m_pMenuBar) {
        return;
    }
    m_pMenuBar = pMenuBar;
    m_pMenu = new QMenu(tr("Box"), pMenuBar);
    QAction* pShow = m_pMenu->addAction(tr("Show sync progress"));
    connect(pShow, &QAction::triggered, this, &BoxSync::showDialog);
    QAction* pSync = m_pMenu->addAction(tr("Sync with box now"));
    connect(pSync, &QAction::triggered, this, &BoxSync::start);
    pMenuBar->addMenu(m_pMenu);
    updateUi();
}
