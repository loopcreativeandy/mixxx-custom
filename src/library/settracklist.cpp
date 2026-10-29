#include "library/settracklist.h"

#include <fileref.h>
#include <tpropertymap.h>

#include <algorithm>

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSqlQuery>

#include "library/relatedtracks.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "track/taglib/trackmetadata_file.h"
#include "track/track.h"
#include "util/assert.h"
#include "util/logger.h"

namespace mixxx {
namespace settracklist {

namespace {

const Logger kLogger("SetTracklist");

constexpr int kFormatVersion = 1;
const QString kFormatKey = QStringLiteral("andySetTracklist");

TagLib::String toTagLibString(const QString& str) {
    const QByteArray utf8 = str.toUtf8();
    return TagLib::String(utf8.constData(), TagLib::String::UTF8);
}

QString toQString(const TagLib::String& str) {
    return QString::fromStdString(str.to8Bit(true));
}

bool writeTag(const QString& audioLocation, const QByteArray& json) {
    TagLib::FileRef file(TAGLIB_FILENAME_FROM_QSTRING(audioLocation));
    if (file.isNull()) {
        return false;
    }
    TagLib::PropertyMap properties = file.properties();
    properties.replace(kTagKey, TagLib::StringList(toTagLibString(QString::fromUtf8(json))));
    const TagLib::PropertyMap rejected = file.setProperties(properties);
    if (rejected.contains(kTagKey)) {
        // e.g. a WAV file's RIFF INFO tag: only the sidecar holds the list.
        return false;
    }
    if (!file.save()) {
        return false;
    }
    // Some tag types take any key on set but drop it on save; trust only
    // what reads back.
    TagLib::FileRef check(TAGLIB_FILENAME_FROM_QSTRING(audioLocation));
    return !check.isNull() && check.properties().contains(kTagKey);
}

QByteArray readTag(const QString& audioLocation) {
    TagLib::FileRef file(TAGLIB_FILENAME_FROM_QSTRING(audioLocation), false);
    if (file.isNull()) {
        return {};
    }
    const TagLib::PropertyMap properties = file.properties();
    const auto it = properties.find(kTagKey);
    if (it == properties.end() || it->second.isEmpty()) {
        return {};
    }
    return toQString(it->second.front()).toUtf8();
}

TrackPointer findTrack(TrackCollectionManager* pTrackCollectionManager, const Entry& entry) {
    if (!entry.location.isEmpty()) {
        const TrackPointer pTrack = pTrackCollectionManager->getTrackByRef(
                TrackRef::fromFilePath(entry.location));
        if (pTrack) {
            return pTrack;
        }
    }
    // Moved or re-imported since the recording: same artist + title.
    if (entry.title.isEmpty()) {
        return {};
    }
    QSqlQuery query(pTrackCollectionManager->internalCollection()->database());
    query.prepare(QStringLiteral(
            "SELECT id FROM library WHERE mixxx_deleted=0 "
            "AND title=:title COLLATE NOCASE AND artist=:artist COLLATE NOCASE "
            "LIMIT 1"));
    query.bindValue(QStringLiteral(":title"), entry.title);
    query.bindValue(QStringLiteral(":artist"), entry.artist);
    if (!query.exec() || !query.next()) {
        return {};
    }
    return pTrackCollectionManager->getTrackById(TrackId(query.value(0)));
}

} // namespace

QByteArray toJson(const QList<Entry>& entries) {
    QJsonArray tracks;
    for (const Entry& entry : entries) {
        tracks.append(QJsonObject{
                {QStringLiteral("location"), entry.location},
                {QStringLiteral("artist"), entry.artist},
                {QStringLiteral("title"), entry.title},
                {QStringLiteral("startMs"), entry.startMillis},
                {QStringLiteral("playedMs"), entry.playedMillis},
        });
    }
    const QJsonObject root{
            {kFormatKey, kFormatVersion},
            {QStringLiteral("tracks"), tracks},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QList<Entry> fromJson(const QByteArray& json) {
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject() || !doc.object().contains(kFormatKey)) {
        return {};
    }
    QList<Entry> entries;
    const QJsonArray tracks = doc.object().value(QStringLiteral("tracks")).toArray();
    for (const QJsonValue& value : tracks) {
        const QJsonObject obj = value.toObject();
        Entry entry;
        entry.location = obj.value(QStringLiteral("location")).toString();
        entry.artist = obj.value(QStringLiteral("artist")).toString();
        entry.title = obj.value(QStringLiteral("title")).toString();
        entry.startMillis = obj.value(QStringLiteral("startMs")).toInteger();
        entry.playedMillis = obj.value(QStringLiteral("playedMs")).toInteger();
        if (!entry.location.isEmpty() || !entry.title.isEmpty()) {
            entries.append(entry);
        }
    }
    return entries;
}

QString sidecarPath(const QString& audioLocation) {
    const QFileInfo info(audioLocation);
    return info.dir().filePath(info.completeBaseName() + QString::fromLatin1(kSidecarSuffix));
}

bool write(const QString& audioLocation, const QList<Entry>& entries) {
    if (entries.isEmpty()) {
        return false;
    }
    const QByteArray json = toJson(entries);
    const bool tagWritten = writeTag(audioLocation, json);
    if (!tagWritten) {
        kLogger.info() << "Tracklist not stored in the tags of" << audioLocation;
    }
    QSaveFile sidecar(sidecarPath(audioLocation));
    bool sidecarWritten = false;
    if (sidecar.open(QIODevice::WriteOnly)) {
        sidecar.write(QJsonDocument::fromJson(json).toJson(QJsonDocument::Indented));
        sidecarWritten = sidecar.commit();
    }
    if (!sidecarWritten) {
        kLogger.warning() << "Could not write" << sidecarPath(audioLocation);
    }
    return tagWritten || sidecarWritten;
}

QList<Entry> read(const QString& audioLocation) {
    QList<Entry> entries = fromJson(readTag(audioLocation));
    if (!entries.isEmpty()) {
        return entries;
    }
    QFile sidecar(sidecarPath(audioLocation));
    if (sidecar.open(QIODevice::ReadOnly)) {
        entries = fromJson(sidecar.readAll());
    }
    return entries;
}

qint64 reachedAtMillis(const Entry& entry) {
    return entry.startMillis + std::min(kReachedMillis, entry.playedMillis);
}

int markEntries(TrackCollectionManager* pTrackCollectionManager,
        const UserSettingsPointer& pConfig,
        const QList<Entry>& entries,
        bool played) {
    VERIFY_OR_DEBUG_ASSERT(pTrackCollectionManager) {
        return 0;
    }
    QList<TrackPointer> marked;
    for (const Entry& entry : entries) {
        const TrackPointer pTrack = findTrack(pTrackCollectionManager, entry);
        if (!pTrack) {
            kLogger.info() << "Set song not in the library:" << entry.artist
                           << "-" << entry.title << entry.location;
            continue;
        }
        pTrack->updatePlayedStatusKeepPlayCount(played);
        marked.append(pTrack);
    }
    relatedtracks::propagatePlayedStateForTracks(pTrackCollectionManager,
            pConfig,
            marked,
            played,
            relatedtracks::PlayCountMode::FlagOnly);
    return static_cast<int>(marked.size());
}

int markSetsOf(TrackCollectionManager* pTrackCollectionManager,
        const UserSettingsPointer& pConfig,
        const QList<TrackPointer>& tracks,
        bool played) {
    int count = 0;
    for (const TrackPointer& pTrack : tracks) {
        if (!pTrack) {
            continue;
        }
        const QList<Entry> entries = read(pTrack->getLocation());
        if (!entries.isEmpty()) {
            count += markEntries(pTrackCollectionManager, pConfig, entries, played);
        }
    }
    return count;
}

} // namespace settracklist
} // namespace mixxx
