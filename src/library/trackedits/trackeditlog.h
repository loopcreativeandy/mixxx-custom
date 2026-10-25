#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

#include "preferences/usersettings.h"

class QLineEdit;
class QWidget;

// andy-custom: rate / tag tracks for later.
//
// Metadata -> "Rate / Tag for the Files..." in the track menu records a dated
// tier rating, a hand energy code and tags to add or remove, without touching
// the file or the library. Every edit is one JSON line in
// <settings>/track_edits/track_edits.jsonl. The fields are the ones PhoneDJ's
// edits.json uses (rate / energy / add / remove), so the same apply step
// writes them into the mp3 tags later, like the live session logs.
namespace mixxx::trackedits {

struct TrackEdit {
    QString rate;   // "S".."D" or empty: dated rating for today, e.g. AAA261024
    QString energy; // "0".."9" or empty: hand energy code in GROUPING
    QStringList add;
    QStringList remove;

    bool isEmpty() const {
        return rate.isEmpty() && energy.isEmpty() && add.isEmpty() && remove.isEmpty();
    }
};

// Comma-separated tags (COMMENT is comma-separated), trimmed, no empties
// or duplicates.
QStringList splitTags(const QString& text);

QJsonObject toJson(const TrackEdit& edit,
        const QString& location,
        const QString& artist,
        const QString& title,
        const QDateTime& time);

QString logFilePath(const UserSettingsPointer& pConfig);

// Appends one line per entry; false if the file can't be written.
bool appendLines(const QString& filePath, const QList<QJsonObject>& lines);

// The tag list from search_tags.txt (one tag per line, # = comment), looked
// up in the settings dir first, then next to the executable. File order is
// kept: it is the suggestion priority. Shared with the library search box.
QStringList loadTagList(const QString& settingsPath, QString* pLoadedFrom = nullptr);

// The comma-separated entry the cursor is in: [start, end) without the
// leading blanks; word = the typed part from start up to the cursor.
struct TagSegment {
    int start;
    int end;
    QString word;
};
TagSegment tagSegmentAt(const QString& text, int cursor);

// Replaces the entry at the cursor with tag. At the end of the text a ", "
// follows so the next tag can be typed right away. *pNewCursor = after it.
QString replaceTagSegment(const QString& text, int cursor, const QString& tag, int* pNewCursor);

// Popup tag completion for a comma-separated field (no-op without tags).
void attachTagCompleter(QLineEdit* pEdit, const QStringList& tags);

// nullopt when cancelled or nothing was entered. tagSuggestions feed the
// popup completion in the add / remove fields.
std::optional<TrackEdit> askForEdit(QWidget* pParent,
        const QString& subject,
        const QString& currentComment,
        const QStringList& tagSuggestions = {});

} // namespace mixxx::trackedits
