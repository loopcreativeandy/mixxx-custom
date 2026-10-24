#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

#include "preferences/usersettings.h"

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

// nullopt when cancelled or nothing was entered.
std::optional<TrackEdit> askForEdit(QWidget* pParent,
        const QString& subject,
        const QString& currentComment);

} // namespace mixxx::trackedits
