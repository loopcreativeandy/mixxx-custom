#include "library/relatedtracks.h"

#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <algorithm>
#include <vector>

#include "library/stemoriginal.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "track/track.h"
#include "util/assert.h"
#include "util/logger.h"

namespace {

const mixxx::Logger kLogger("RelatedTracks");

/// Words that only say *which version* of a song this is. Peeled off the end
/// of a title so "Bella (Robin Remix)", "Bella - Radio Edit" and "Bella" all
/// normalize to "bella".
const QSet<QString> kVersionWords = {
        QStringLiteral("remix"),
        QStringLiteral("remixes"),
        QStringLiteral("rmx"),
        QStringLiteral("mix"),
        QStringLiteral("edit"),
        QStringLiteral("edits"),
        QStringLiteral("version"),
        QStringLiteral("ver"),
        QStringLiteral("vip"),
        QStringLiteral("bootleg"),
        QStringLiteral("rework"),
        QStringLiteral("refix"),
        QStringLiteral("flip"),
        QStringLiteral("mashup"),
        QStringLiteral("extended"),
        QStringLiteral("radio"),
        QStringLiteral("club"),
        QStringLiteral("original"),
        QStringLiteral("instrumental"),
        QStringLiteral("acapella"),
        QStringLiteral("acappella"),
        QStringLiteral("acoustic"),
        QStringLiteral("live"),
        QStringLiteral("remaster"),
        QStringLiteral("remastered"),
        QStringLiteral("cover"),
        QStringLiteral("dub"),
        QStringLiteral("cut"),
        QStringLiteral("official"),
        QStringLiteral("audio"),
        QStringLiteral("video"),
        QStringLiteral("lyrics"),
        QStringLiteral("hd"),
        QStringLiteral("hq"),
        QStringLiteral("free"),
        QStringLiteral("download"),
};

/// Words that introduce a guest performer. Everything from here on is dropped
/// from a title, and used as a separator in an artist field.
const QSet<QString> kFeatureWords = {
        QStringLiteral("feat"),
        QStringLiteral("feats"),
        QStringLiteral("ft"),
        QStringLiteral("fts"),
        QStringLiteral("featuring"),
        QStringLiteral("prod"),
};

/// Additional artist-field separators. These are words rather than the
/// punctuation ("&", ",", "/", "+") that normalizeText() already flattens.
const QSet<QString> kArtistSeparatorWords = {
        QStringLiteral("and"),
        QStringLiteral("x"),
        QStringLiteral("vs"),
        QStringLiteral("versus"),
        QStringLiteral("with"),
        QStringLiteral("presents"),
        QStringLiteral("pres"),
        QStringLiteral("meets"),
};

/// Apostrophes join a word rather than separate it: "Don't Stop" is two words,
/// not three, and a rip that spells it "Dont Stop" is the same song.
bool isApostrophe(QChar c) {
    return c == QChar('\'') || c == QChar(u'’') || c == QChar(u'‘') ||
            c == QChar('`') || c == QChar(u'´');
}

/// Lower-case, strip accents, and turn every run of punctuation into a single
/// space. "Coração & Cia." -> "coracao cia".
QString normalizeText(const QString& text) {
    const QString decomposed = text.normalized(QString::NormalizationForm_KD);
    QString result;
    result.reserve(decomposed.size());
    bool pendingSeparator = false;
    for (const QChar c : decomposed) {
        if (c.category() == QChar::Mark_NonSpacing ||
                c.category() == QChar::Mark_SpacingCombining) {
            // A combining accent from the decomposition above.
            continue;
        }
        if (isApostrophe(c)) {
            continue;
        }
        if (c.isLetterOrNumber()) {
            if (pendingSeparator && !result.isEmpty()) {
                result.append(QChar(' '));
            }
            pendingSeparator = false;
            result.append(c.toLower());
        } else {
            pendingSeparator = true;
        }
    }
    return result;
}

/// Remove every bracketed section, including nested ones. Titles carry their
/// version in brackets far more often than anything identifying.
QString removeBracketedSections(const QString& text) {
    static const QRegularExpression bracketed(
            QStringLiteral(R"(\([^()]*\)|\[[^\[\]]*\]|\{[^{}]*\})"));
    QString result = text;
    QString previous;
    // Repeat because removing the inner pair exposes the outer one.
    do {
        previous = result;
        result.remove(bracketed);
    } while (result != previous);
    return result;
}

/// Levenshtein distance, abandoned as soon as it is known to exceed
/// `maxDistance`. Titles are short, so the plain two-row version is plenty.
int boundedEditDistance(const QString& lhs, const QString& rhs, int maxDistance) {
    const int lhsLength = lhs.size();
    const int rhsLength = rhs.size();
    if (std::abs(lhsLength - rhsLength) > maxDistance) {
        return maxDistance + 1;
    }
    std::vector<int> previousRow(rhsLength + 1);
    std::vector<int> currentRow(rhsLength + 1);
    for (int j = 0; j <= rhsLength; ++j) {
        previousRow[j] = j;
    }
    for (int i = 1; i <= lhsLength; ++i) {
        currentRow[0] = i;
        int rowMinimum = currentRow[0];
        for (int j = 1; j <= rhsLength; ++j) {
            const int substitutionCost = lhs[i - 1] == rhs[j - 1] ? 0 : 1;
            currentRow[j] = std::min({currentRow[j - 1] + 1,
                    previousRow[j] + 1,
                    previousRow[j - 1] + substitutionCost});
            rowMinimum = std::min(rowMinimum, currentRow[j]);
        }
        if (rowMinimum > maxDistance) {
            return maxDistance + 1;
        }
        previousRow.swap(currentRow);
    }
    return previousRow[rhsLength];
}

/// How many typos two titles of this length may differ by. Short titles have
/// to match exactly - at four characters a single edit turns one real song
/// into another.
int allowedTitleDistance(int length) {
    if (length <= 6) {
        return 0;
    }
    if (length <= 14) {
        return 1;
    }
    return 2;
}

} // namespace

namespace mixxx {
namespace relatedtracks {

const ConfigKey kMarkRelatedStemConfigKey(
        QStringLiteral("[Auto DJ]"), QStringLiteral("MarkRelatedStemPlayed"));
const ConfigKey kMarkRelatedSameSongConfigKey(
        QStringLiteral("[Auto DJ]"), QStringLiteral("MarkRelatedSameSongPlayed"));
const ConfigKey kMarkRelatedPlayCountConfigKey(
        QStringLiteral("[Auto DJ]"), QStringLiteral("MarkRelatedPlayCount"));

Options optionsFromConfig(const UserSettingsPointer& pConfig) {
    Options options;
    if (!pConfig) {
        return options;
    }
    options.stemCounterpart = pConfig->getValue(kMarkRelatedStemConfigKey, true);
    options.sameArtistTitle = pConfig->getValue(kMarkRelatedSameSongConfigKey, true);
    options.updatePlayCount = pConfig->getValue(kMarkRelatedPlayCountConfigKey, false);
    return options;
}

QString normalizeTitle(const QString& title) {
    const QStringList words =
            normalizeText(removeBracketedSections(title))
                    .split(QChar(' '), Qt::SkipEmptyParts);
    QStringList kept;
    for (const QString& word : words) {
        if (kFeatureWords.contains(word) && !kept.isEmpty()) {
            // "Titanium feat. Sia" -> "titanium". Never drop everything: a
            // title that *starts* with "ft" is not a guest list.
            break;
        }
        kept.append(word);
    }
    // Peel trailing version words, but never the last one standing: "Live"
    // and "Remix" are real song titles.
    while (kept.size() > 1 && kVersionWords.contains(kept.constLast())) {
        kept.removeLast();
    }
    return kept.join(QChar(' '));
}

QSet<QString> artistTokens(const QString& artist) {
    QSet<QString> tokens;
    // Split on the punctuation separators first: normalizeText() flattens them
    // into plain spaces, so "Jorge & Mateus" would otherwise come back as the
    // single performer "jorge mateus".
    static const QRegularExpression punctuationSeparators(
            QStringLiteral(R"([&,;/|+×])"));
    const QStringList sections =
            removeBracketedSections(artist).split(punctuationSeparators);
    for (const QString& section : sections) {
        const QStringList words =
                normalizeText(section).split(QChar(' '), Qt::SkipEmptyParts);
        QStringList current;
        const auto flush = [&tokens, &current]() {
            if (!current.isEmpty()) {
                tokens.insert(current.join(QChar(' ')));
                current.clear();
            }
        };
        for (const QString& word : words) {
            if (kFeatureWords.contains(word) || kArtistSeparatorWords.contains(word)) {
                flush();
                continue;
            }
            current.append(word);
        }
        flush();
    }
    return tokens;
}

bool normalizedTitlesMatch(const QString& lhs, const QString& rhs) {
    if (lhs.isEmpty() || rhs.isEmpty()) {
        return false;
    }
    if (lhs == rhs) {
        return true;
    }
    const int maxDistance =
            allowedTitleDistance(std::min(lhs.size(), rhs.size()));
    if (maxDistance <= 0) {
        return false;
    }
    return boundedEditDistance(lhs, rhs, maxDistance) <= maxDistance;
}

bool isSameSong(const QString& lhsArtist,
        const QString& lhsTitle,
        const QString& rhsArtist,
        const QString& rhsTitle) {
    if (!normalizedTitlesMatch(normalizeTitle(lhsTitle), normalizeTitle(rhsTitle))) {
        return false;
    }
    const QSet<QString> lhsArtists = artistTokens(lhsArtist);
    const QSet<QString> rhsArtists = artistTokens(rhsArtist);
    if (lhsArtists.isEmpty() || rhsArtists.isEmpty()) {
        // One of them has no artist at all - the title alone is not enough.
        return false;
    }
    return lhsArtists.intersects(rhsArtists);
}

QList<TrackId> findRelatedTrackIds(const QSqlDatabase& database,
        const Track& track,
        const Options& options) {
    QList<TrackId> relatedTrackIds;
    if (!options.anyRuleEnabled()) {
        return relatedTrackIds;
    }
    const TrackId ownTrackId = track.getId();
    const auto append = [&relatedTrackIds, &ownTrackId](TrackId trackId) {
        if (!trackId.isValid() || trackId == ownTrackId ||
                relatedTrackIds.contains(trackId)) {
            return;
        }
        relatedTrackIds.append(trackId);
    };

    if (options.stemCounterpart) {
        // Only one of the two directions can return anything: the helper
        // refuses to look for the kind the track already is.
        append(stemoriginal::findCounterpartTrackId(
                database, track, stemoriginal::Counterpart::Original));
        append(stemoriginal::findCounterpartTrackId(
                database, track, stemoriginal::Counterpart::Stem));
    }

    if (!options.sameArtistTitle) {
        return relatedTrackIds;
    }

    const QString ownTitle = normalizeTitle(track.getTitle());
    const QSet<QString> ownArtists = artistTokens(track.getArtist());
    if (ownTitle.isEmpty() || ownArtists.isEmpty()) {
        return relatedTrackIds;
    }

    // The fuzzy comparison cannot be expressed in SQL, so every library row is
    // checked in memory. Andy's library is a few thousand tracks and this runs
    // once per track change on the GUI thread, which measures in single-digit
    // milliseconds - cheap enough to keep the matching honest.
    QSqlQuery query(database);
    query.setForwardOnly(true);
    query.prepare(QStringLiteral(
            "SELECT library.id, library.artist, library.title "
            "FROM library "
            "INNER JOIN track_locations "
            "ON library.location = track_locations.id "
            "WHERE library.mixxx_deleted = 0 "
            "AND track_locations.fs_deleted = 0"));
    if (!query.exec()) {
        kLogger.warning() << "Failed to look up tracks related to"
                          << track.getLocation() << query.lastError();
        return relatedTrackIds;
    }
    while (query.next()) {
        const TrackId candidateTrackId(query.value(0));
        if (!candidateTrackId.isValid() || candidateTrackId == ownTrackId ||
                relatedTrackIds.contains(candidateTrackId)) {
            continue;
        }
        const QSet<QString> candidateArtists = artistTokens(query.value(1).toString());
        if (!ownArtists.intersects(candidateArtists)) {
            // Cheaper than normalizing the title, so it goes first.
            continue;
        }
        if (!normalizedTitlesMatch(ownTitle, normalizeTitle(query.value(2).toString()))) {
            continue;
        }
        relatedTrackIds.append(candidateTrackId);
    }
    return relatedTrackIds;
}

int propagatePlayedState(TrackCollectionManager* pTrackCollectionManager,
        const UserSettingsPointer& pConfig,
        const Track& playedTrack,
        bool played) {
    VERIFY_OR_DEBUG_ASSERT(pTrackCollectionManager) {
        return 0;
    }
    const Options options = optionsFromConfig(pConfig);
    if (!options.anyRuleEnabled()) {
        return 0;
    }
    const QList<TrackId> relatedTrackIds = findRelatedTrackIds(
            pTrackCollectionManager->internalCollection()->database(),
            playedTrack,
            options);
    int updatedCount = 0;
    for (const TrackId& relatedTrackId : relatedTrackIds) {
        const TrackPointer pRelatedTrack =
                pTrackCollectionManager->getTrackById(relatedTrackId);
        if (!pRelatedTrack) {
            continue;
        }
        if (pRelatedTrack->getPlayCounter().isPlayed() == played &&
                !options.updatePlayCount) {
            // Already in the requested state, and we are not touching the
            // play count - nothing would change.
            continue;
        }
        if (options.updatePlayCount) {
            pRelatedTrack->updatePlayCounter(played);
        } else {
            pRelatedTrack->updatePlayedStatusKeepPlayCount(played);
        }
        ++updatedCount;
    }
    if (updatedCount > 0) {
        kLogger.debug() << "Marked" << updatedCount << "track(s) related to"
                        << playedTrack.getLocation()
                        << (played ? "as played" : "as not played");
    }
    return updatedCount;
}

} // namespace relatedtracks
} // namespace mixxx
