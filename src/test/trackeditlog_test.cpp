#include "library/trackedits/trackeditlog.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

using namespace mixxx::trackedits;

TEST(TrackEditLogTest, splitTagsTrimsAndDropsEmptiesAndDuplicates) {
    EXPECT_EQ(QStringList({"Sensual", "Pop!", "slow start"}),
            splitTags(QStringLiteral(" Sensual, ,Pop!,slow  start , Sensual,")));
    EXPECT_TRUE(splitTags(QStringLiteral(" , ")).isEmpty());
}

TEST(TrackEditLogTest, onlySetFieldsAreWritten) {
    TrackEdit edit;
    edit.rate = QStringLiteral("A");
    edit.add = {QStringLiteral("Sensual")};
    const QDateTime time(QDate(2026, 10, 24), QTime(21, 5));
    const QJsonObject obj = toJson(edit,
            QStringLiteral("C:/Music/a.mp3"),
            QStringLiteral("Artist"),
            QStringLiteral("Title"),
            time);
    EXPECT_EQ("261024", obj["date"].toString());
    EXPECT_EQ("C:/Music/a.mp3", obj["location"].toString());
    EXPECT_EQ("A", obj["rate"].toString());
    EXPECT_EQ(QJsonArray({"Sensual"}), obj["add"].toArray());
    EXPECT_FALSE(obj.contains("energy"));
    EXPECT_FALSE(obj.contains("remove"));
}

TEST(TrackEditLogTest, linesAreAppended) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("sub/track_edits.jsonl"));
    TrackEdit edit;
    edit.energy = QStringLiteral("7");
    const QDateTime time = QDateTime::currentDateTime();
    ASSERT_TRUE(appendLines(path, {toJson(edit, "a", "", "", time)}));
    ASSERT_TRUE(appendLines(path,
            {toJson(edit, "b", "", "", time), toJson(edit, "c", "", "", time)}));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QList<QByteArray> lines = file.readAll().trimmed().split('\n');
    ASSERT_EQ(3, lines.size());
    EXPECT_EQ("a", QJsonDocument::fromJson(lines[0]).object()["location"].toString());
    EXPECT_EQ("c", QJsonDocument::fromJson(lines[2]).object()["location"].toString());
    EXPECT_EQ("7", QJsonDocument::fromJson(lines[2]).object()["energy"].toString());
}
