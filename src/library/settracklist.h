#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include "preferences/usersettings.h"
#include "track/track_decl.h"

class TrackCollectionManager;

/// Andy's set tracklists (andy-custom, CP130): a recording made in Mixxx
/// remembers exactly which library tracks were audible in it and when. When
/// the set is played later - or marked played by hand - every song in it is
/// marked played too (flag only, the play count stays untouched), so he does
/// not play them again that night.
///
/// The list travels with the file: it is stored in the recording's own tags
/// (key kTagKey - an ID3v2 TXXX frame, a Vorbis/FLAC comment, an MP4 freeform
/// atom) and, as a readable backup and for formats whose tags can't hold it,
/// in a sidecar "<recording base name>.tracks.json" next to the recording.
/// No database change.
namespace mixxx {
namespace settracklist {

inline constexpr const char* kTagKey = "ANDYSETTRACKLIST";
inline constexpr const char* kSidecarSuffix = ".tracks.json";

/// A song counts as reached while the set plays once the play position is
/// this far into it (or at its end, if it was audible for less).
inline constexpr qint64 kReachedMillis = 30 * 1000;

struct Entry {
    QString location;
    QString artist;
    QString title;
    /// Offset of the song's first audible moment within the recording.
    qint64 startMillis = 0;
    /// How long it was audible in total.
    qint64 playedMillis = 0;

    bool operator==(const Entry& other) const = default;
};

QByteArray toJson(const QList<Entry>& entries);
/// Empty on anything that isn't a tracklist written by toJson().
QList<Entry> fromJson(const QByteArray& json);

QString sidecarPath(const QString& audioLocation);

/// Writes the list into the file's tags and the sidecar. Returns true if at
/// least one of the two worked.
bool write(const QString& audioLocation, const QList<Entry>& entries);

/// Tags first, then the sidecar. Empty for anything that isn't a set.
QList<Entry> read(const QString& audioLocation);

/// The play position (ms into the set) at which `entry` counts as reached.
qint64 reachedAtMillis(const Entry& entry);

/// Marks (or unmarks) every song of the set played - flag only, play count
/// untouched - and propagates that to related copies (CP66, FlagOnly).
/// An entry is found by its location, or else by exact artist + title.
/// Returns the number of set songs found in the library.
int markEntries(TrackCollectionManager* pTrackCollectionManager,
        const UserSettingsPointer& pConfig,
        const QList<Entry>& entries,
        bool played);

/// For each of `tracks` that is a set: markEntries() its songs.
/// Returns the number of songs marked.
int markSetsOf(TrackCollectionManager* pTrackCollectionManager,
        const UserSettingsPointer& pConfig,
        const QList<TrackPointer>& tracks,
        bool played);

} // namespace settracklist
} // namespace mixxx
