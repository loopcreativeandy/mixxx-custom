#include "util/stemcolorconfig.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QList>
#include <QPair>
#include <QStandardPaths>
#include <QString>

#include "util/cmdlineargs.h"
#include "util/layoutconfig.h"

// Regression tests for the andy-custom stem color + layout override configs,
// same shape as spectrumconfigtest.cpp and pinning the same bug class: a
// config singleton that resolved its file path through CmdlineArgs and
// *created* the file on first read. StemColorConfig::current() is reached in
// the test suite via stemtest.cpp -> StemInfoImporter::importStemInfos() ->
// fixedStemColor(), and MixxxTest never sets a temp settings path, so the old
// create-on-demand behavior wrote andys_stem_colors.ini into the user's real
// settings directory from the test binary. See mixxx-build/AUDIT_2026-08-20.md.
//
// These tests deliberately never call initialize(): they assert the behavior
// of the uninitialized state, which is the state the whole test binary runs in.

namespace {

class StemColorConfigTest : public testing::Test {};

QList<QPair<QString, bool>> snapshotCandidateFiles(const QString& fileName) {
    const QStringList candidateDirs = {
            // The exact path the pre-fix code resolved through CmdlineArgs.
            CmdlineArgs::Instance().getSettingsPath(),
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation),
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation),
    };
    QList<QPair<QString, bool>> snapshot;
    for (const QString& dir : candidateDirs) {
        if (dir.isEmpty()) {
            continue;
        }
        const QString filePath = QDir(dir).filePath(fileName);
        snapshot.append(qMakePair(filePath, QFile::exists(filePath)));
    }
    return snapshot;
}

// The core of the fix: with no settings path handed over, current() must be a
// pure in-memory lookup and never create (or remove) its ini file.
TEST_F(StemColorConfigTest, UninitializedCreatesNoFile) {
    const auto existedBefore =
            snapshotCandidateFiles(QStringLiteral("andys_stem_colors.ini"));
    ASSERT_FALSE(existedBefore.isEmpty())
            << "no candidate settings directory resolved - this test would be "
               "vacuous";

    for (int i = 0; i < 5; ++i) {
        mixxx::StemColorConfig::current();
    }

    for (const auto& [filePath, existed] : existedBefore) {
        EXPECT_EQ(existed, QFile::exists(filePath))
                << "StemColorConfig::current() changed whether "
                << filePath.toStdString()
                << " exists; it must never create or remove the file.";
    }
}

// LayoutConfig shares the exact same pattern and fix; pin it here too.
TEST_F(StemColorConfigTest, LayoutConfigUninitializedCreatesNoFile) {
    const auto existedBefore =
            snapshotCandidateFiles(QStringLiteral("andys_layout.ini"));
    ASSERT_FALSE(existedBefore.isEmpty())
            << "no candidate settings directory resolved - this test would be "
               "vacuous";

    for (int i = 0; i < 5; ++i) {
        mixxx::LayoutConfig::current();
    }

    for (const auto& [filePath, existed] : existedBefore) {
        EXPECT_EQ(existed, QFile::exists(filePath))
                << "LayoutConfig::current() changed whether "
                << filePath.toStdString()
                << " exists; it must never create or remove the file.";
    }
}

// Without a config file the built-in defaults must come back usable: the stem
// palette is painted directly, and rowTint's alpha is what keeps the library
// row tint subtle.
TEST_F(StemColorConfigTest, UninitializedReturnsUsableDefaults) {
    const mixxx::StemColorConfig config = mixxx::StemColorConfig::current();

    EXPECT_TRUE(config.drums.isValid());
    EXPECT_TRUE(config.bass.isValid());
    EXPECT_TRUE(config.other.isValid());
    EXPECT_TRUE(config.vocals.isValid());
    EXPECT_TRUE(config.rowTint.isValid());
    EXPECT_LT(config.rowTint.alpha(), 255);

    const mixxx::LayoutConfig layout = mixxx::LayoutConfig::current();
    EXPECT_GT(layout.rowHeightMinFactor, 0.0);
}

} // anonymous namespace
