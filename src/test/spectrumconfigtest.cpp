#include "util/spectrumconfig.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QList>
#include <QPair>
#include <QStandardPaths>
#include <QString>

#include "util/cmdlineargs.h"

// Regression tests for the andy-custom spectrum analyzer config.
//
// The bug these pin down: SpectrumConfig::current() used to resolve its file
// path from CmdlineArgs and *create* the file on first read. current() is
// called from EngineSpectrum's constructor, which EngineMixer builds
// unconditionally, so every engine test wrote andys_spectrum.ini into whatever
// QStandardPaths resolved to. In the test binary that is the AppData root (no
// application name is set), and the resulting read crashed the Windows ARM64
// test job intermittently — 8 red builds between 2026-07-30 and 2026-08-06.
// See mixxx-build/STATUS.md, 2026-08-07.
//
// These tests deliberately never call initialize(): they assert the behavior
// of the uninitialized state, which is the state the whole test binary runs in.

namespace {

class SpectrumConfigTest : public testing::Test {};

// The core of the fix: with no settings path handed over, current() must be a
// pure in-memory lookup. If this fails, some code path is touching the user's
// filesystem from a unit test again.
//
// Asserts that current() does not *create* the file rather than that the file
// is absent: a developer running this on a machine where Mixxx has actually
// run already has andys_spectrum.ini sitting in the real settings directory,
// and that is fine — what must never happen is a test creating it.
TEST_F(SpectrumConfigTest, UninitializedCreatesNoFile) {
    QStringList candidateDirs = {
            // The exact path the pre-fix code resolved through CmdlineArgs.
            // This is the one that matters: on Linux it is ~/.mixxx, on Windows
            // the AppData location that crashed the ARM64 job.
            CmdlineArgs::Instance().getSettingsPath(),
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation),
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation),
    };

    QList<QPair<QString, bool>> existedBefore;
    for (const QString& dir : candidateDirs) {
        if (dir.isEmpty()) {
            continue;
        }
        const QString filePath =
                QDir(dir).filePath(QStringLiteral("andys_spectrum.ini"));
        existedBefore.append(qMakePair(filePath, QFile::exists(filePath)));
    }
    ASSERT_FALSE(existedBefore.isEmpty())
            << "no candidate settings directory resolved - this test would be "
               "vacuous";

    // Provoke the path that used to write the template file.
    for (int i = 0; i < 5; ++i) {
        mixxx::SpectrumConfig::current();
    }

    for (const auto& [filePath, existed] : existedBefore) {
        EXPECT_EQ(existed, QFile::exists(filePath))
                << "SpectrumConfig::current() changed whether "
                << filePath.toStdString()
                << " exists; it must never create or remove the file.";
    }
}

// Without a config file the built-in defaults must come back, and `bands` in
// particular has to stay inside the range the engine allocates its filter bank
// from — EngineSpectrum indexes m_filters with it.
TEST_F(SpectrumConfigTest, UninitializedReturnsUsableDefaults) {
    const mixxx::SpectrumConfig config = mixxx::SpectrumConfig::current();

    EXPECT_GE(config.bands, mixxx::SpectrumConfig::kMinBands);
    EXPECT_LE(config.bands, mixxx::SpectrumConfig::kMaxBands);
    EXPECT_GT(config.frameIntervalMs, 0);
    EXPECT_GT(config.attack, 0.0);
    EXPECT_LE(config.attack, 1.0);
    // Hot reload defaults to on: that is what the config file exists for.
    EXPECT_TRUE(config.hotReload);
}

// Repeated calls are cached and must stay stable — the widget calls this from
// the paint path.
TEST_F(SpectrumConfigTest, RepeatedCallsAreStable) {
    const mixxx::SpectrumConfig first = mixxx::SpectrumConfig::current();
    const mixxx::SpectrumConfig second = mixxx::SpectrumConfig::current();

    EXPECT_EQ(first.bands, second.bands);
    EXPECT_EQ(first.frameIntervalMs, second.frameIntervalMs);
    EXPECT_EQ(first.hotReload, second.hotReload);
    EXPECT_DOUBLE_EQ(first.fallInitialSpeed, second.fallInitialSpeed);
    EXPECT_DOUBLE_EQ(first.peakHoldMs, second.peakHoldMs);
}

} // anonymous namespace
