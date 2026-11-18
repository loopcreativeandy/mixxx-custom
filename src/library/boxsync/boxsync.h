#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include "preferences/usersettings.h"

class QDialog;
class QFile;
class QLabel;
class QMenu;
class QMenuBar;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class TrackCollectionManager;

/// andy-custom CP134 (IDEA-21): pull Andy's tags (and new songs) from the tag
/// database on his box at startup.
///
/// Off unless <settings>/boxsync.json exists — box addresses + token live ONLY
/// in that local file, never in code (public repo):
///   {"urls": ["http://<lan>:<port>", "http://<tailscale>:<port>"],
///    "token": "...", "library_root": "C:/Users/andy/Music/music/myLibrary"}
///
/// Startup: tries each URL once (in order), then pages /api/changes since the
/// last synced revision and
///  - writes comment / grouping / composer of every changed song into the
///    Mixxx library rows of its files that exist locally (box wins; an empty
///    box value never clears a local one),
///  - downloads files that don't exist locally into library_root (never
///    overwrites: written as *.boxsync-part, renamed only if the size matches
///    and the target still doesn't exist).
/// A dialog shows progress ("Run in background" hides it); the "Box" menu
/// shows the state and offers "Sync with box now" (= retry).
/// Sync state: <settings>/boxsync_state.json ({"last_rev": N}).
class BoxSync : public QObject {
    Q_OBJECT
  public:
    BoxSync(TrackCollectionManager* pTrackCollectionManager,
            UserSettingsPointer pConfig,
            QWidget* pParentWindow);
    ~BoxSync() override;

    bool isConfigured() const;
    /// Safe to call repeatedly (same menu bar → no-op).
    void attachToMenuBar(QMenuBar* pMenuBar);

  public slots:
    /// Starts a sync (or just shows the dialog if one is running).
    void start();
    void showDialog();

  private:
    enum class State {
        Idle,
        Connecting,
        Fetching,
        Applying,
        Downloading,
        Done,
        Unreachable,
        Failed,
    };
    struct Download {
        int fileId;
        QString localPath;
        qint64 size;
        QJsonObject song;
        qint64 songRev;
    };

    bool loadConfig();
    void loadState();
    void saveState(qint64 lastRev);
    void tryNextUrl();
    void fetchChanges(qint64 since);
    void applyBatch();
    /// Returns true if the Mixxx row changed.
    bool applySongToLocalFile(const QJsonObject& song, const QString& localPath);
    void downloadNext();
    void finish();
    void setState(State state, const QString& message);
    void updateUi();
    void ensureDialog();
    QString localPathFor(const QString& relpath) const;

    TrackCollectionManager* const m_pTrackCollectionManager;
    const UserSettingsPointer m_pConfig;
    QPointer<QWidget> m_pParentWindow;
    QNetworkAccessManager* m_pNetwork;

    // config
    QStringList m_urls;
    QByteArray m_token;
    QString m_libraryRoot;

    // run state
    State m_state;
    QString m_message;
    int m_urlIndex;
    bool m_sawUnauthorized;
    QString m_baseUrl;
    qint64 m_lastRev;
    qint64 m_serverRev;
    qint64 m_minFailedRev; // songs at/after this rev must be re-sent next time
    QList<QJsonObject> m_songs;
    int m_applyIndex;
    QList<Download> m_downloads;
    int m_downloadIndex;
    QPointer<QNetworkReply> m_pDownloadReply;
    QFile* m_pDownloadFile;
    int m_updatedTracks;
    int m_downloaded;
    int m_skippedExisting;
    int m_failedDownloads;

    // ui
    QPointer<QDialog> m_pDialog;
    QPointer<QLabel> m_pStatusLabel;
    QPointer<QLabel> m_pDetailLabel;
    QPointer<QProgressBar> m_pProgress;
    QPointer<QPushButton> m_pBackgroundButton;
    QPointer<QPushButton> m_pRetryButton;
    QPointer<QMenuBar> m_pMenuBar;
    QPointer<QMenu> m_pMenu;
};
