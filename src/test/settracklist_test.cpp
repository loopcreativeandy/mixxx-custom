#include "library/settracklist.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QTemporaryDir>

#include "test/mixxxtest.h"

using namespace mixxx::settracklist;

namespace {

QList<Entry> sampleEntries() {
    return {
            Entry{QStringLiteral("D:/Music/Zouk/Glue.mp3"),
                    QStringLiteral("Bicep"),
                    QStringLiteral("Glue"),
                    0,
                    95000},
            Entry{QStringLiteral("D:/Music/Zouk/Deixa – Ao Vivo.flac"),
                    QStringLiteral("Lagum, Ana Gabriela"),
                    QStringLiteral("Deixa (Ao Vivo) ü"),
                    81000,
                    200000},
    };
}

class SetTracklistTest : public MixxxTest {
  protected:
    // Copies a test file into a temp dir, so tags can be written.
    QString copyTestFile(const QString& name) {
        const QString source = getTestDir().filePath(QStringLiteral("id3-test-data/") + name);
        const QString target = m_tempDir.filePath(QStringLiteral("set_") + name);
        EXPECT_TRUE(QFile::copy(source, target)) << source.toStdString();
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
        return target;
    }

    void expectTagRoundTrip(const QString& name) {
        const QString location = copyTestFile(name);
        ASSERT_TRUE(write(location, sampleEntries()));
        // Without the sidecar the list must come from the tags.
        ASSERT_TRUE(QFile::remove(sidecarPath(location)));
        EXPECT_EQ(sampleEntries(), read(location)) << name.toStdString();
    }

    QTemporaryDir m_tempDir;
};

TEST_F(SetTracklistTest, JsonRoundTrip) {
    EXPECT_EQ(sampleEntries(), fromJson(toJson(sampleEntries())));
}

TEST_F(SetTracklistTest, ForeignJsonIsNotASet) {
    EXPECT_TRUE(fromJson(QByteArrayLiteral("{\"tracks\":[{\"title\":\"x\"}]}")).isEmpty());
    EXPECT_TRUE(fromJson(QByteArrayLiteral("not json")).isEmpty());
    EXPECT_TRUE(fromJson(QByteArray()).isEmpty());
}

TEST_F(SetTracklistTest, SidecarSitsNextToTheRecording) {
    EXPECT_EQ(QStringLiteral("/rec/2026-10-29_17h00m00s_Zouk_X__Glue.tracks.json"),
            sidecarPath(QStringLiteral("/rec/2026-10-29_17h00m00s_Zouk_X__Glue.mp3")));
}

TEST_F(SetTracklistTest, ReachedThirtySecondsInOrAtTheEnd) {
    EXPECT_EQ(81000 + 30000, reachedAtMillis(sampleEntries()[1]));
    EXPECT_EQ(5000 + 12000, reachedAtMillis(Entry{{}, {}, QStringLiteral("t"), 5000, 12000}));
}

TEST_F(SetTracklistTest, Mp3TagRoundTrip) {
    expectTagRoundTrip(QStringLiteral("cover-test-øé~ł€˚-png.mp3"));
}

TEST_F(SetTracklistTest, FlacTagRoundTrip) {
    expectTagRoundTrip(QStringLiteral("cover-test-øé~ł€˚.flac"));
}

TEST_F(SetTracklistTest, OggTagRoundTrip) {
    expectTagRoundTrip(QStringLiteral("cover-test-øé~ł€˚.ogg"));
}

TEST_F(SetTracklistTest, SidecarAloneIsEnough) {
    const QString location = copyTestFile(QStringLiteral("cover-test-øé~ł€˚.wav"));
    ASSERT_TRUE(write(location, sampleEntries()));
    EXPECT_TRUE(QFile::exists(sidecarPath(location)));
    EXPECT_EQ(sampleEntries(), read(location));
}

TEST_F(SetTracklistTest, PlainTrackIsNotASet) {
    const QString location = copyTestFile(QStringLiteral("cover-test-øé~ł€˚-jpg.mp3"));
    EXPECT_TRUE(read(location).isEmpty());
}

} // namespace
