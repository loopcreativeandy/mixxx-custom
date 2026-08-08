#pragma once

#include <QList>
#include <QSet>
#include <QString>

#include "preferences/usersettings.h"
#include "track/track_decl.h"
#include "track/trackid.h"

class QSqlDatabase;
class Track;
class TrackCollectionManager;

/// Andy's "don't play the same song twice" rule (andy-custom): when a track is
/// marked played, every other copy of that same song in the library should be
/// marked played too, so Auto DJ skips it and he can see at a glance that he
/// already played it.
///
/// Two tracks count as the same song when either
///
///  * they are the two halves of a stem/original pair (see stemoriginal.h), or
///  * their artist and title match after normalization - which is deliberately
///    fuzzy, so a remix, an edit, a slightly differently spelled rip and the
///    original all collapse onto one another.
///
/// Both rules and the play-count behaviour are individually switchable in
/// Preferences -> Auto DJ; see kMarkRelated*ConfigKey below.
namespace mixxx {
namespace relatedtracks {

/// Propagate the played flag to the stem/original counterpart. Default on.
extern const ConfigKey kMarkRelatedStemConfigKey;
/// Propagate the played flag to fuzzy artist+title matches. Default on.
extern const ConfigKey kMarkRelatedSameSongConfigKey;
/// Also bump the persistent play count and last-played time of the related
/// tracks instead of only setting the session played flag. Default off: Andy
/// wants the Auto DJ grey-out, not a distorted play history.
extern const ConfigKey kMarkRelatedPlayCountConfigKey;

struct Options {
    bool stemCounterpart = true;
    bool sameArtistTitle = true;
    bool updatePlayCount = false;

    bool anyRuleEnabled() const {
        return stemCounterpart || sameArtistTitle;
    }
};

/// Read the three switches above, applying the defaults documented there.
Options optionsFromConfig(const UserSettingsPointer& pConfig);

/// Fold a title down to the part that identifies the song: accents, case and
/// punctuation are dropped, bracketed sections ("(Extended Mix)", "[feat. X]")
/// are removed, a trailing "feat. …" is cut off and trailing version words
/// ("remix", "edit", "radio", …) are peeled away.
///
/// Only *trailing* version words are removed - "Original Sin" has to survive
/// intact. Returns an empty string if nothing identifying is left, which the
/// matcher treats as "never matches".
QString normalizeTitle(const QString& title);

/// The individual performers in an artist field, normalized like
/// normalizeTitle() and split on the usual separators ("&", ",", "x", "vs",
/// "feat.", "presents", …). "Jorge & Mateus feat. Anitta" yields
/// {"jorge", "mateus", "anitta"}.
QSet<QString> artistTokens(const QString& artist);

/// True if two normalized titles are close enough to be the same song. Exact
/// up to eight characters, then tolerating one typo, and two past fourteen.
bool normalizedTitlesMatch(const QString& lhs, const QString& rhs);

/// True if the two artist/title pairs describe the same song under the fuzzy
/// rule above. Both a shared performer and a matching title are required -
/// title alone is far too loose in a library full of covers.
bool isSameSong(const QString& lhsArtist,
        const QString& lhsTitle,
        const QString& rhsArtist,
        const QString& rhsTitle);

/// Every track in the library that is the same song as `track` under the
/// enabled rules, never including `track` itself.
QList<TrackId> findRelatedTrackIds(const QSqlDatabase& database,
        const Track& track,
        const Options& options);

/// What the propagation should do about the play count, which depends on what
/// happened to the track that triggered it. Mixxx marks tracks played in two
/// different ways and the related tracks have to follow suit.
enum class PlayCountMode {
    /// The source track only had its session played flag set (the right-click
    /// "Mark as played", the playlist-wide mark, joining a setlog). Related
    /// tracks get exactly that, and the play-count switch does not apply -
    /// nothing was counted here in the first place.
    FlagOnly,
    /// The source track's play count was bumped as well (Auto DJ actually
    /// playing it, or the Played tick box in the library table). Related
    /// tracks follow the "count related tracks as really played" switch.
    FollowSetting,
};

/// Mark everything related to `playedTrack` as played (or as not played).
/// Does nothing when both rules are switched off.
///
/// Returns the number of related tracks that were updated.
int propagatePlayedState(TrackCollectionManager* pTrackCollectionManager,
        const UserSettingsPointer& pConfig,
        const Track& playedTrack,
        bool played,
        PlayCountMode playCountMode);

/// Bulk form of propagatePlayedState() for the "mark this whole selection /
/// playlist played" actions. The library is scanned once for the entire batch
/// instead of once per track, which is the difference between marking a
/// 100-track playlist instantly and visibly stalling.
///
/// Returns the number of related tracks that were updated.
int propagatePlayedStateForTracks(TrackCollectionManager* pTrackCollectionManager,
        const UserSettingsPointer& pConfig,
        const QList<TrackPointer>& playedTracks,
        bool played,
        PlayCountMode playCountMode);

} // namespace relatedtracks
} // namespace mixxx
