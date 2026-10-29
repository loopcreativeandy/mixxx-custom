#include "recording/recordingmanager.h"

#include <QDateTime>
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QMutex>
#include <QStorageInfo>

#include "control/controlpushbutton.h"
#include "engine/channels/enginechannel.h"
#include "engine/enginemixer.h"
#include "engine/enginexfader.h"
#include "engine/sidechain/enginerecord.h"
#include "engine/sidechain/enginesidechain.h"
#include "errordialoghandler.h"
#include "mixer/playerinfo.h"
#include "mixer/playermanager.h"
#include "moc_recordingmanager.cpp"
#include "recording/defs_recording.h"
#include "recording/recordingname.h"
#include "track/track.h"

#define MIN_DISK_FREE 1024 * 1024 * 1024ll // one gibibyte

namespace {
// A track counts for the auto name once it was audible on the main mix for
// this long in total (same rule as the live crowd-rating session) — skips
// tracks that only flashed up or were only cued in the headphones.
constexpr qint64 kMinPlayedMillisForName = 30 * 1000;
// Below this main-mix gain (volume fader x crossfader) a deck is not audible.
constexpr double kAudibleGain = 0.1;
// Same pregain cut-off PlayerInfo uses for the "currently playing" deck.
constexpr double kMinPregain = 0.25;
constexpr int kPlayedTickMillis = 1000;

bool isSameTrack(const TrackPointer& a, const TrackPointer& b) {
    if (a == b) {
        return true;
    }
    if (!a || !b) {
        return false;
    }
    if (a->getId().isValid() && b->getId().isValid()) {
        return a->getId() == b->getId();
    }
    return a->getLocation() == b->getLocation();
}
} // namespace

RecordingManager::RecordingManager(UserSettingsPointer pConfig, EngineMixer* pEngine)
        : m_pConfig(pConfig),
          m_recordingDir(""),
          m_recording_base_file(""),
          m_recordingFile(""),
          m_recordingLocation(""),
          m_bRecording(false),
          m_iNumberOfBytesRecorded(0),
          m_iNumberOfBytesRecordedSplit(0),
          m_split_size(0),
          m_split_time(0),
          m_iNumberSplits(0),
          m_secondsRecorded(0),
          m_secondsRecordedSplit(0) {
    m_pToggleRecording = std::make_unique<ControlPushButton>(
            ConfigKey(RECORDING_PREF_KEY, "toggle_recording"));
    connect(m_pToggleRecording.get(),
            &ControlPushButton::valueChanged,
            this,
            &RecordingManager::slotToggleRecording);
    m_pCoRecStatus = std::make_unique<ControlObject>(ConfigKey(RECORDING_PREF_KEY, "status"));
    connect(&m_playedTimer, &QTimer::timeout, this, &RecordingManager::slotPlayedTick);

    m_split_size = getFileSplitSize();
    m_split_time = getFileSplitSeconds();

    // Register EngineRecord with the engine sidechain.
    EngineSideChain* pSidechain = pEngine->getSideChain();
    if (pSidechain) {
        EngineRecord* pEngineRecord = new EngineRecord(m_pConfig);
        connect(pEngineRecord,
                &EngineRecord::isRecording,
                this,
                &RecordingManager::slotIsRecording);
        connect(pEngineRecord,
                &EngineRecord::bytesRecorded,
                this,
                &RecordingManager::slotBytesRecorded);
        connect(pEngineRecord,
                &EngineRecord::durationRecorded,
                this,
                &RecordingManager::slotDurationRecorded);
        pSidechain->addSideChainWorker(pEngineRecord);
    }
}

QString RecordingManager::formatDateTimeForFilename(const QDateTime& dateTime) const {
    // Use a format based on ISO 8601. Windows does not support colons in
    // filenames so we can't use them anywhere.
    QString formatted = dateTime.toString("yyyy-MM-dd_hh'h'mm'm'ss's'");
    return formatted;
}

void RecordingManager::slotSetRecording(bool recording) {
    if (recording && !isRecordingActive()) {
        startRecording();
    } else if (!recording && isRecordingActive()) {
        stopRecording();
    }
}

void RecordingManager::slotToggleRecording(double value) {
    bool toggle = static_cast<bool>(value);
    if (toggle) {
        if (isRecordingActive()) {
            stopRecording();
        } else {
            startRecording();
        }
    }
}

qint64 RecordingManager::getFreeSpace() {
    // returns the free space on the recording location in bytes
    // return -1 if the free space could not be determined
    qint64 rv = -1;
    QStorageInfo storage(m_recordingDir);
    if (storage.isValid()) {
        rv = storage.bytesAvailable();
    }
    return rv;
}

void RecordingManager::startRecording() {
    QString encodingType = m_pConfig->getValueString(
            ConfigKey(RECORDING_PREF_KEY, "Encoding"));
    QString fileExtension = EncoderFactory::getFactory()
                                    .getFormatFor(encodingType)
                                    .fileExtension;

    m_iNumberOfBytesRecordedSplit = 0;
    m_secondsRecordedSplit=0;
    m_iNumberOfBytesRecorded = 0;
    m_secondsRecorded=0;
    m_dfSilence = false;
    m_dfCounter=0;
    m_split_size = getFileSplitSize();
    m_split_time = getFileSplitSeconds();
    if (m_split_time < INT_MAX) {
        qDebug() << "Split time is:" << m_split_time;
    }
    else {
        qDebug() << "Split size is:" << m_split_size;
    }

    m_iNumberSplits = 1;
    // Append file extension.
    QString date_time_str = formatDateTimeForFilename(QDateTime::currentDateTime());
    m_recordingFile = QString("%1.%2")
                              .arg(date_time_str, fileExtension);

    // Storing the absolutePath of the recording file without file extension.
    m_recording_base_file = getRecordingDir();
    m_recording_base_file.append("/").append(date_time_str);
    // Appending file extension to get the filelocation.
    m_recordingLocation = m_recording_base_file + QChar('.') + fileExtension;
    m_pConfig->set(ConfigKey(RECORDING_PREF_KEY, "Path"), m_recordingLocation);
    m_pConfig->set(ConfigKey(RECORDING_PREF_KEY, "CuePath"), ConfigValue(m_recording_base_file + QStringLiteral(".cue")));

    m_dateTimeBase = date_time_str;
    m_playedTracks.clear();
    m_pendingRename = PendingRename();
    m_lastPlayedTick.start();
    m_recordingClock.start();
    m_playedTimer.start(kPlayedTickMillis);

    m_pCoRecStatus->set(RECORD_READY);
}

void RecordingManager::splitContinueRecording()
{
    ++m_iNumberSplits;
    m_secondsRecorded+=m_secondsRecordedSplit;

    m_iNumberOfBytesRecordedSplit = 0;
    m_secondsRecordedSplit=0;

    QString encodingType = m_pConfig->getValueString(ConfigKey(RECORDING_PREF_KEY, "Encoding"));
    QString fileExtension = EncoderFactory::getFactory()
                                    .getFormatFor(encodingType)
                                    .fileExtension;

    QString new_base_filename = m_recording_base_file + QStringLiteral("part") + QString::number(m_iNumberSplits);
    m_recordingLocation = new_base_filename + QChar('.') + fileExtension;

    m_pConfig->set(ConfigKey(RECORDING_PREF_KEY, "Path"), m_recordingLocation);
    m_pConfig->set(ConfigKey(RECORDING_PREF_KEY, "CuePath"), ConfigValue(new_base_filename + QStringLiteral(".cue")));
    m_recordingFile = QFileInfo(m_recordingLocation).fileName();

    m_pCoRecStatus->set(RECORD_SPLIT_CONTINUE);
}

void RecordingManager::stopRecording() {
    qDebug() << "Recording stopped";
    if (m_bRecording) {
        prepareAutoRename();
    }
    m_pCoRecStatus->set(RECORD_OFF);
    m_recordingFile = "";
    m_recordingLocation = "";
    m_iNumberOfBytesRecorded = 0;
    m_secondsRecorded = 0;
}

void RecordingManager::setRecordingDir() {
    QDir recordDir(m_pConfig->getValueString(
        ConfigKey(RECORDING_PREF_KEY, "Directory")));
    // Note: the default ConfigKey for recordDir is set in DlgPrefRecord::DlgPrefRecord.

    if (!recordDir.exists()) {
        if (recordDir.mkpath(recordDir.absolutePath())) {
            qDebug() << "Created folder" << recordDir.absolutePath() << "for recordings";
        } else {
            // Using qt_error_string() since QDir has not yet a wrapper for error strings.
            // https://bugreports.qt.io/browse/QTBUG-1483
            qDebug() << "Failed to create folder" << recordDir.absolutePath()
                     << "for recordings:" << qt_error_string();
        }
    }
    m_recordingDir = recordDir.absolutePath();
    qDebug() << "Recordings folder set to" << m_recordingDir;
}

QString& RecordingManager::getRecordingDir() {
    // Update current recording dir from preferences.
    setRecordingDir();
    return m_recordingDir;
}

// Only called when recording is active.
void RecordingManager::slotDurationRecorded(quint64 duration) {
    if (m_secondsRecordedSplit != duration) {
        m_secondsRecordedSplit = duration;
        if (duration >= m_split_time) {
            qDebug() << "Splitting after " << duration << " seconds";
            // This will reuse the previous filename but append a suffix.
            splitContinueRecording();
        }
        emit durationRecorded(getRecordedDurationStr(m_secondsRecorded+m_secondsRecordedSplit));
    }
}

// Copy from the implementation in enginerecord.cpp
QString RecordingManager::getRecordedDurationStr(unsigned int duration) {
    return QStringLiteral("%1:%2")
            .arg(duration / 60, 2, 10, QChar('0'))  // minutes
            .arg(duration % 60, 2, 10, QChar('0')); // seconds
}

// Only called when recording is active.
void RecordingManager::slotBytesRecorded(int bytes) {
    // auto conversion to quint64
    m_iNumberOfBytesRecorded += bytes;
    m_iNumberOfBytesRecordedSplit += bytes;

    //Split before reaching the max size. m_split_size has some headroom, as
    //seen in the constant definitions in defs_recording.h. Also, note that
    //bytes are increased in the order of 10s of KBs each call.
    if (m_iNumberOfBytesRecordedSplit >= m_split_size) {
        qDebug() << "Splitting after " << m_iNumberOfBytesRecorded << " bytes written";
        // This will reuse the previous filename but append a suffix.
        splitContinueRecording();
    }
    emit bytesRecorded(m_iNumberOfBytesRecorded);

    // check for free space

    // we only check every 1 MB of data to minimize syscalls
    m_dfCounter -= bytes;

    if (m_dfCounter > 0) {
        return;
    }

    qint64 dfree = getFreeSpace();
    // reset counter
    m_dfCounter = 1024 * 1024;
    if (dfree == -1) {
        qDebug() << "can't determine free space";
        return;
    }
    if (dfree > MIN_DISK_FREE) {
        m_dfSilence = false;
    } else if (m_dfSilence != true) {
        // suppress further warnings until the situation has cleared
        m_dfSilence = true;
        // we run out of diskspace and should warn the user.
        // FIXME(poelzi) temporary display a error message. Replace this with Message Infrastructure when ready
        warnFreespace();
    }
}

void RecordingManager::warnFreespace() {
    qWarning() << "RecordingManager: less than 1 GiB free space";
    ErrorDialogProperties* props = ErrorDialogHandler::instance()->newDialogProperties();
    props->setType(DLG_WARNING);
    props->setTitle(tr("Low Disk Space Warning"));
    props->setText(tr("There is less than 1 GiB of usable space in the recording folder"));
    props->setKey("RecordingManager::warnFreespace");   // To prevent multiple windows for the same error

    props->addButton(QMessageBox::Ok);
    props->setDefaultButton(QMessageBox::Ok);
    props->setEscapeButton(QMessageBox::Ok);
    props->setModal(false);

    ErrorDialogHandler::instance()->requestErrorDialog(props);
}

void RecordingManager::slotIsRecording(bool isRecordingActive, bool error) {
    //qDebug() << "SlotIsRecording " << isRecording << error;

    // CP124: the engine has closed the file by now (it emits this after
    // closeFile()), so the recording is saved — rename it before the
    // Recordings view refreshes on isRecording(false).
    if (!isRecordingActive && !error && !m_pendingRename.location.isEmpty()) {
        performAutoRename();
    }
    m_pendingRename = PendingRename();

    // Notify the GUI controls, see dlgrecording.cpp.
    m_bRecording = isRecordingActive;
    emit isRecording(isRecordingActive);

    if (error) {
        ErrorDialogProperties* props = ErrorDialogHandler::instance()->newDialogProperties();
        props->setType(DLG_WARNING);
        props->setTitle(tr("Recording"));
        props->setText("<html>"+tr("Could not create audio file for recording!")
                       +"<p>"+tr("Ensure there is enough free disk space and you have write permission for the Recordings folder.")
                       +"<p>"+tr("You can change the location of the Recordings folder in Preferences -> Recording.")
                       +"</p></html>");
        ErrorDialogHandler::instance()->requestErrorDialog(props);
    }
}

bool RecordingManager::isDeckAudible(int deckIndex) {
    while (static_cast<int>(m_deckProxies.size()) <= deckIndex) {
        const QString group = PlayerManager::groupForDeck(
                static_cast<int>(m_deckProxies.size()));
        DeckProxies deck;
        deck.pPlay = std::make_unique<PollingControlProxy>(group, QStringLiteral("play"));
        deck.pVolume = std::make_unique<PollingControlProxy>(group, QStringLiteral("volume"));
        deck.pPregain = std::make_unique<PollingControlProxy>(group, QStringLiteral("pregain"));
        deck.pOrientation = std::make_unique<PollingControlProxy>(
                group, QStringLiteral("orientation"));
        deck.pMute = std::make_unique<PollingControlProxy>(group, QStringLiteral("mute"));
        m_deckProxies.push_back(std::move(deck));
    }
    if (!m_pXfader) {
        m_pXfader = std::make_unique<PollingControlProxy>(
                QStringLiteral("[Master]"), QStringLiteral("crossfader"));
    }
    const DeckProxies& deck = m_deckProxies[deckIndex];
    if (deck.pPlay->get() == 0.0 || deck.pPregain->get() <= kMinPregain ||
            deck.pMute->get() > 0.0) {
        return false;
    }
    CSAMPLE_GAIN xfl, xfr;
    // Same simplification as PlayerInfo::updateCurrentPlayingDeck().
    EngineXfader::getXfadeGains(m_pXfader->get(), 1.0, 0.0, MIXXX_XFADER_ADDITIVE, false, &xfl, &xfr);
    const auto orient = static_cast<int>(deck.pOrientation->get());
    double xfvol = 1.0;
    if (orient == EngineChannel::LEFT) {
        xfvol = xfl;
    } else if (orient == EngineChannel::RIGHT) {
        xfvol = xfr;
    }
    return deck.pVolume->get() * xfvol >= kAudibleGain;
}

void RecordingManager::accrueTrack(const TrackPointer& pTrack, qint64 elapsedMillis) {
    for (auto& played : m_playedTracks) {
        if (isSameTrack(played.pTrack, pTrack)) {
            played.playedMillis += elapsedMillis;
            return;
        }
    }
    // Ordered by first audible appearance.
    const qint64 firstAudible = m_recordingClock.isValid()
            ? std::max<qint64>(0, m_recordingClock.elapsed() - elapsedMillis)
            : 0;
    m_playedTracks.append(PlayedTrack{pTrack, elapsedMillis, firstAudible});
}

void RecordingManager::slotPlayedTick() {
    if (m_pCoRecStatus->get() == RECORD_OFF || !m_lastPlayedTick.isValid()) {
        m_playedTimer.stop();
        return;
    }
    // Real elapsed time, the timer may fire late under load.
    const qint64 elapsed = std::clamp<qint64>(
            m_lastPlayedTick.restart(), 0, 5 * kPlayedTickMillis);
    const int numDecks = PlayerInfo::instance().numDecks();
    for (int i = 0; i < numDecks; ++i) {
        if (!isDeckAudible(i)) {
            continue;
        }
        const TrackPointer pTrack =
                PlayerInfo::instance().getTrackInfo(PlayerManager::groupForDeck(i));
        if (pTrack) {
            accrueTrack(pTrack, elapsed);
        }
    }
}

void RecordingManager::prepareAutoRename() {
    slotPlayedTick();
    m_playedTimer.stop();
    m_lastPlayedTick.invalidate();
    m_recordingClock.invalidate();
    m_pendingRename = PendingRename();
    if (m_iNumberSplits > 1 || m_dateTimeBase.isEmpty()) {
        // Split recordings are long sets; their part files keep their names.
        // (Their tracks are spread over the parts, so no tracklist either.)
        m_playedTracks.clear();
        return;
    }
    QStringList titles;
    QList<mixxx::settracklist::Entry> tracklist;
    for (const auto& played : std::as_const(m_playedTracks)) {
        if (played.playedMillis < kMinPlayedMillisForName) {
            continue;
        }
        tracklist.append(mixxx::settracklist::Entry{played.pTrack->getLocation(),
                played.pTrack->getArtist(),
                played.pTrack->getTitle(),
                played.firstAudibleMillis,
                played.playedMillis});
        const QString title = played.pTrack->getTitle().trimmed().isEmpty()
                ? QFileInfo(played.pTrack->getLocation()).completeBaseName()
                : played.pTrack->getTitle();
        if (!recordingname::camelCaseTitle(title).isEmpty()) {
            titles.append(title);
        }
    }
    m_playedTracks.clear();
    qInfo() << "Recording: tracks played" << titles;
    if (tracklist.isEmpty()) {
        return;
    }
    m_pendingRename.location = m_recordingLocation;
    m_pendingRename.tracklist = tracklist;
    if (titles.isEmpty() || titles.size() > recordingname::kMaxTracksForAutoName) {
        return;
    }
    m_pendingRename.cueLocation = m_recording_base_file + QStringLiteral(".cue");
    m_pendingRename.dateTimeBase = m_dateTimeBase;
    m_pendingRename.titles = titles;
    m_pendingRename.zoukMode =
            ControlObject::get(ConfigKey(QStringLiteral("[Controls]"),
                    QStringLiteral("zouk_mode"))) > 0.0;
}

void RecordingManager::performAutoRename() {
    const QFileInfo oldInfo(m_pendingRename.location);
    if (!oldInfo.exists()) {
        qWarning() << "Recording: auto-rename skipped, file not found"
                   << m_pendingRename.location;
        return;
    }
    // CP130: the tracklist goes with the file under its final name.
    QString finalLocation = renameRecording(oldInfo);
    if (!m_pendingRename.tracklist.isEmpty() &&
            mixxx::settracklist::write(finalLocation, m_pendingRename.tracklist)) {
        qInfo() << "Recording: tracklist of" << m_pendingRename.tracklist.size()
                << "tracks stored with" << QFileInfo(finalLocation).fileName();
    }
}

QString RecordingManager::renameRecording(const QFileInfo& oldInfo) {
    if (m_pendingRename.titles.isEmpty()) {
        return oldInfo.absoluteFilePath();
    }
    const QString base = recordingname::buildBaseName(m_pendingRename.dateTimeBase,
            m_pendingRename.zoukMode,
            m_pendingRename.titles);
    const QDir dir = oldInfo.absoluteDir();
    const QString suffix = oldInfo.suffix();
    QString newBase = base;
    for (int i = 2; QFileInfo::exists(dir.filePath(newBase + QChar('.') + suffix)); ++i) {
        newBase = base + QChar('_') + QString::number(i);
    }
    const QString newLocation = dir.filePath(newBase + QChar('.') + suffix);
    if (!QFile::rename(oldInfo.absoluteFilePath(), newLocation)) {
        // The recording itself is saved either way; only the name stays.
        qWarning() << "Recording: auto-rename failed, keeping"
                   << oldInfo.absoluteFilePath();
        return oldInfo.absoluteFilePath();
    }
    qInfo() << "Recording: renamed" << oldInfo.fileName() << "->"
            << QFileInfo(newLocation).fileName();
    m_pConfig->set(ConfigKey(RECORDING_PREF_KEY, "Path"), newLocation);
    if (QFileInfo::exists(m_pendingRename.cueLocation)) {
        const QString newCue = dir.filePath(newBase + QStringLiteral(".cue"));
        if (!QFileInfo::exists(newCue) &&
                QFile::rename(m_pendingRename.cueLocation, newCue)) {
            m_pConfig->set(ConfigKey(RECORDING_PREF_KEY, "CuePath"), ConfigValue(newCue));
        }
    }
    return newLocation;
}

bool RecordingManager::isRecordingActive() const {
    return m_bRecording;
}

const QString& RecordingManager::getRecordingFile() const {
    return m_recordingFile;
}

const QString& RecordingManager::getRecordingLocation() const {
    return m_recordingLocation;
}

quint64 RecordingManager::getFileSplitSize() {
    QString fileSizeStr = m_pConfig->getValueString(ConfigKey(RECORDING_PREF_KEY, "FileSize"));
    if (fileSizeStr == SPLIT_650MB) {
        return SIZE_650MB;
    } else if (fileSizeStr == SPLIT_700MB) {
        return SIZE_700MB;
    } else if (fileSizeStr == SPLIT_1024MB) {
        return SIZE_1GB;
    } else if (fileSizeStr == SPLIT_2048MB) {
        return SIZE_2GB;
    } else if (fileSizeStr == SPLIT_4096MB) {
        return SIZE_4GB;
    } else if (fileSizeStr == SPLIT_60MIN) {
        return SIZE_4GB; //Ignore size limit. use time limit
    } else if (fileSizeStr == SPLIT_74MIN) {
        return SIZE_4GB; //Ignore size limit. use time limit
    } else if (fileSizeStr == SPLIT_80MIN) {
        return SIZE_4GB; //Ignore size limit. use time limit
    } else if (fileSizeStr == SPLIT_120MIN) {
        return SIZE_4GB; //Ignore size limit. use time limit
    } else {
        return SIZE_4GB; // default
    }
}

unsigned int RecordingManager::getFileSplitSeconds() {
    QString fileSizeStr = m_pConfig->getValueString(ConfigKey(RECORDING_PREF_KEY, "FileSize"));
    if (fileSizeStr == SPLIT_60MIN) {
        return 60*60;
    } else if (fileSizeStr == SPLIT_74MIN) {
        return 74*60;
    } else if (fileSizeStr == SPLIT_80MIN) {
        return 80*60;
    } else if (fileSizeStr == SPLIT_120MIN) {
        return 120*60;
    } else { // Do not limit by time for the rest.
        return INT_MAX;
    }
}
