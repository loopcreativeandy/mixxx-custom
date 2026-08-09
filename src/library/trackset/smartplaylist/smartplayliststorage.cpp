#include "library/trackset/smartplaylist/smartplayliststorage.h"

#include <QFile>
#include <QSaveFile>
#include <QTextStream>
#include <algorithm>

#include "util/cmdlineargs.h"
#include "util/logger.h"

namespace {

const mixxx::Logger kLogger("SmartPlaylistStorage");

const QString kFileName = QStringLiteral("andys_smart_playlists.ini");

const QString kQueryKey = QStringLiteral("query");

} // anonymous namespace

QString SmartPlaylistStorage::filePath() {
    const QString settingsPath = CmdlineArgs::Instance().getSettingsPath();
    if (settingsPath.isEmpty()) {
        return QString();
    }
    return settingsPath + QChar('/') + kFileName;
}

QList<SmartPlaylistDefinition> SmartPlaylistStorage::load() {
    QList<SmartPlaylistDefinition> playlists;
    const QString path = filePath();
    if (path.isEmpty()) {
        return playlists;
    }
    QFile file(path);
    if (!file.exists()) {
        // Nothing saved yet - that is not an error.
        return playlists;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        kLogger.warning() << "Cannot read" << path;
        return playlists;
    }

    QTextStream in(&file);
    // Section headers are written verbatim, so a name may contain anything
    // except a newline. Only the first '=' of a key line is a separator, which
    // keeps '=' usable inside a query.
    bool inSection = false;
    SmartPlaylistDefinition current;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QChar('#'))) {
            continue;
        }
        if (line.startsWith(QChar('[')) && line.endsWith(QChar(']'))) {
            if (inSection && !current.name.isEmpty() && !current.query.isEmpty()) {
                playlists.append(current);
            }
            current = SmartPlaylistDefinition();
            current.name = line.mid(1, line.size() - 2).trimmed();
            inSection = true;
            continue;
        }
        if (!inSection) {
            kLogger.warning() << "Ignoring line outside of a [name] section:" << line;
            continue;
        }
        const int sep = line.indexOf(QChar('='));
        if (sep < 0) {
            kLogger.warning() << "Ignoring malformed line:" << line;
            continue;
        }
        const QString key = line.left(sep).trimmed();
        const QString value = line.mid(sep + 1).trimmed();
        if (key == kQueryKey) {
            current.query = value;
        } else {
            kLogger.warning() << "Unknown key" << key << "in section" << current.name;
        }
    }
    if (inSection && !current.name.isEmpty() && !current.query.isEmpty()) {
        playlists.append(current);
    }

    std::sort(playlists.begin(),
            playlists.end(),
            [](const SmartPlaylistDefinition& lhs, const SmartPlaylistDefinition& rhs) {
                return lhs.name.compare(rhs.name, Qt::CaseInsensitive) < 0;
            });
    return playlists;
}

bool SmartPlaylistStorage::save(const QList<SmartPlaylistDefinition>& playlists) {
    const QString path = filePath();
    if (path.isEmpty()) {
        kLogger.warning() << "No settings path, cannot save smart playlists";
        return false;
    }
    // Write via a temporary file so a crash mid-write cannot truncate the
    // existing definitions.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        kLogger.warning() << "Cannot write" << path;
        return false;
    }
    QTextStream out(&file);
    out << "# Andy's Mixxx smart playlists - edit freely, no recompile needed.\n"
        << "# One [name] section per smart playlist. 'query' is a library search\n"
        << "# string using the normal search grammar, e.g.\n"
        << "#   Zouk | Zoukable          (either token)\n"
        << "#   comment:44444 -Dembow    (has the tag, but not the other)\n"
        << "#   bpm:80-95 key:8A         (field filters and ranges)\n"
        << "# The query is re-evaluated every time the smart playlist is opened,\n"
        << "# and whatever you type into the search bar filters within it.\n"
        << "# Sections without a query are ignored. Changes made here show up\n"
        << "# after restarting Mixxx.\n";
    for (const SmartPlaylistDefinition& playlist : playlists) {
        out << "\n[" << playlist.name << "]\n"
            << kQueryKey << "=" << playlist.query << "\n";
    }
    out.flush();
    if (!file.commit()) {
        kLogger.warning() << "Failed to commit" << path;
        return false;
    }
    return true;
}
