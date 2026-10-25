#include "library/trackedits/trackeditlog.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QAbstractItemView>
#include <QCompleter>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>

#include "test/mixxxtest.h"

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

TEST(TrackEditLogTest, tagSegmentIsTheEntryUnderTheCursor) {
    const QString text = QStringLiteral("Sensual, Po, slow");
    const TagSegment mid = tagSegmentAt(text, 11); // after "Po"
    EXPECT_EQ(9, mid.start);
    EXPECT_EQ(11, mid.end);
    EXPECT_EQ("Po", mid.word);
    const TagSegment first = tagSegmentAt(text, 3);
    EXPECT_EQ(0, first.start);
    EXPECT_EQ("Sen", first.word);
    EXPECT_EQ("sl", tagSegmentAt(QStringLiteral("a,  sl"), 6).word);
}

TEST(TrackEditLogTest, replacingAtTheEndAppendsASeparator) {
    int cursor = -1;
    EXPECT_EQ("Sensual, LastSong!, ",
            replaceTagSegment(QStringLiteral("Sensual, la"), 11, "LastSong!", &cursor));
    EXPECT_EQ(20, cursor);
    // in the middle: the rest is kept, cursor right after the tag
    EXPECT_EQ("Sensual, Pop!, slow",
            replaceTagSegment(QStringLiteral("Sensual, Po, slow"), 11, "Pop!", &cursor));
    EXPECT_EQ(13, cursor);
}

TEST(TrackEditLogTest, tagListKeepsFileOrderAndSkipsComments) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QFile file(dir.filePath("search_tags.txt"));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("# most used first\nSensual!\n\nLastSong!\nSensual!\n  Pop!  \n");
    file.close();
    QString loadedFrom;
    EXPECT_EQ(QStringList({"Sensual!", "LastSong!", "Pop!"}),
            loadTagList(dir.path(), &loadedFrom));
    EXPECT_EQ(file.fileName(), loadedFrom);
}

// Through a real line edit in a dialog with a default Save button: typing
// pops the suggestions up, Enter takes the first one and must not accept
// (= save) the dialog.
class TrackEditTagCompleterTest : public MixxxTest {};

TEST_F(TrackEditTagCompleterTest, enterPicksTheSuggestionWithoutSaving) {
    QDialog dialog;
    auto* pLayout = new QVBoxLayout(&dialog);
    auto* pEdit = new QLineEdit(&dialog);
    pLayout->addWidget(pEdit);
    auto* pButtons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    pLayout->addWidget(pButtons);
    pButtons->button(QDialogButtonBox::Ok)->setDefault(true);
    QObject::connect(pButtons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    attachTagCompleter(pEdit, {"Zoukable!", "Sensual!", "Sensual slow!", "LastSong!"});
    dialog.show();
    pEdit->setFocus();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&dialog));

    QTest::keyClicks(pEdit, "Lambada!, se");
    QCompleter* pCompleter = pEdit->findChild<QCompleter*>();
    ASSERT_NE(nullptr, pCompleter);
    ASSERT_TRUE(pCompleter->popup()->isVisible());
    EXPECT_EQ(2, pCompleter->completionCount()); // both Sensual tags, in file order
    QTest::keyClick(pCompleter->popup(), Qt::Key_Return);
    EXPECT_EQ("Lambada!, Sensual!, ", pEdit->text());
    EXPECT_TRUE(dialog.isVisible());
    EXPECT_EQ(0, dialog.result());

    // one letter is not enough, an unknown word shows nothing
    QTest::keyClicks(pEdit, "z");
    EXPECT_FALSE(pCompleter->popup()->isVisible());
    QTest::keyClicks(pEdit, "q");
    EXPECT_FALSE(pCompleter->popup()->isVisible());
}
