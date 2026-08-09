#pragma once

#include <QList>
#include <QString>

/// A smart playlist (andy-custom): a saved library search query that is
/// re-evaluated every time the playlist is opened, instead of a stored list of
/// tracks. The query uses the very same grammar as the library search bar.
struct SmartPlaylistDefinition {
    QString name;
    QString query;
};

/// Persistence for smart playlists.
///
/// Deliberately NOT stored in mixxxdb.sqlite: Andy runs official Mixxx against
/// the same database, so this build keeps the DB schema untouched. The
/// definitions live in a plain, hand-editable text file next to the other
/// andy-custom settings (see StemColorConfig for the same pattern).
namespace SmartPlaylistStorage {

/// Absolute path of the definition file, or an empty string if the settings
/// path is unknown.
QString filePath();

/// Reads all definitions, sorted case-insensitively by name. Returns an empty
/// list when the file does not exist yet.
QList<SmartPlaylistDefinition> load();

/// Writes all definitions, replacing the file. Returns false on I/O failure.
bool save(const QList<SmartPlaylistDefinition>& playlists);

} // namespace SmartPlaylistStorage
