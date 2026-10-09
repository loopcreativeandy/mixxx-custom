#include "widget/wsearchlineedit.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTest>

#include "test/mixxxtest.h"

class WSearchLineEditTagTest : public MixxxTest {
  protected:
    void SetUp() override {
        QFile file(QDir(config()->getSettingsPath()).filePath("search_tags.txt"));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("# comment\nZoukable!\nZouk!\nLastSong!\n\nLambada!\n");
        file.close();
        m_pEdit = std::make_unique<WSearchLineEdit>(nullptr, config());
        m_pEdit->show();
        m_pEdit->setFocus();
    }

    void type(const QString& text) {
        QTest::keyClicks(m_pEdit.get(), text);
    }

    QString lastSearch(QSignalSpy& spy) {
        if (spy.isEmpty() && !spy.wait(2000)) {
            return QStringLiteral("<no search>");
        }
        return spy.takeLast().at(0).toString();
    }

    std::unique_ptr<WSearchLineEdit> m_pEdit;
};

TEST_F(WSearchLineEditTagTest, SuggestsTagAndSearchesTypedText) {
    QSignalSpy spy(m_pEdit.get(), &WSearchLineEdit::search);
    type("las");
    EXPECT_EQ(QStringLiteral("LastSong!"), m_pEdit->lineEdit()->text());
    EXPECT_EQ(QStringLiteral("tSong!"), m_pEdit->lineEdit()->selectedText());
    // Earlier suggestions recase the typed letters; search is case-insensitive
    EXPECT_EQ(QStringLiteral("las"), lastSearch(spy).toLower());

    // Right accepts the tag and searches for it
    QTest::keyClick(m_pEdit.get(), Qt::Key_Right);
    EXPECT_EQ(QStringLiteral("LastSong!"), m_pEdit->lineEdit()->text());
    EXPECT_FALSE(m_pEdit->lineEdit()->hasSelectedText());
    EXPECT_EQ(QStringLiteral("LastSong!"), lastSearch(spy));
}

TEST_F(WSearchLineEditTagTest, ShortestMatchWinsAndLaterWords) {
    type("bpm:120 zouk");
    EXPECT_EQ(QStringLiteral("bpm:120 Zouk!"), m_pEdit->lineEdit()->text());
    type("a");
    EXPECT_EQ(QStringLiteral("bpm:120 Zoukable!"), m_pEdit->lineEdit()->text());
}

TEST_F(WSearchLineEditTagTest, BackspaceDropsSuggestion) {
    type("lam");
    EXPECT_EQ(QStringLiteral("Lambada!"), m_pEdit->lineEdit()->text());
    QTest::keyClick(m_pEdit.get(), Qt::Key_Backspace);
    EXPECT_EQ(QStringLiteral("lam"), m_pEdit->lineEdit()->text().toLower());
    EXPECT_FALSE(m_pEdit->lineEdit()->hasSelectedText());
}

TEST_F(WSearchLineEditTagTest, NoSuggestionForOneCharOrUnknownWord) {
    type("l");
    EXPECT_EQ(QStringLiteral("l"), m_pEdit->lineEdit()->text());
    m_pEdit->lineEdit()->clear();
    type("xyz");
    EXPECT_EQ(QStringLiteral("xyz"), m_pEdit->lineEdit()->text());
}
