#include "soundio/audiojittertrace.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>

namespace {

QStringList traceFiles(const QString& settingsPath) {
    QDir dir(settingsPath + QStringLiteral("/jitter-trace"));
    return dir.entryList(QStringList() << QStringLiteral("jitter_*.csv"),
            QDir::Files,
            QDir::Name);
}

TEST(AudioJitterTraceTest, WritesRecordsAndHeader) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    constexpr int kCallbacks = 20;
    {
        AudioJitterTrace trace(tempDir.path(),
                QStringLiteral("Test Device"),
                QStringLiteral("TestAPI"),
                44100.0,
                1024,
                true);
        for (int i = 0; i < kCallbacks; ++i) {
            trace.onCallbackEntry(i == 5 ? paOutputUnderflow : 0);
            trace.onCallbackExit(1024);
            QThread::msleep(2);
        }
        // Destruction stops the writer, drains the FIFO and logs the summary.
    }

    QStringList files = traceFiles(tempDir.path());
    ASSERT_EQ(files.size(), 1);

    QFile file(tempDir.path() + QStringLiteral("/jitter-trace/") + files.first());
    ASSERT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QTextStream in(&file);

    int headerLines = 0;
    int dataLines = 0;
    bool sawColumns = false;
    bool sawUnderflowFlag = false;
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.startsWith(QLatin1Char('#'))) {
            ++headerLines;
        } else if (line.startsWith(QStringLiteral("t_ms,"))) {
            sawColumns = true;
        } else if (!line.isEmpty()) {
            ++dataLines;
            const QStringList cols = line.split(QLatin1Char(','));
            ASSERT_EQ(cols.size(), 5);
            EXPECT_EQ(cols.at(3), QStringLiteral("1024"));
            if (cols.at(4).toUInt() & paOutputUnderflow) {
                sawUnderflowFlag = true;
            }
        }
    }
    EXPECT_GE(headerLines, 7);
    EXPECT_TRUE(sawColumns);
    EXPECT_EQ(dataLines, kCallbacks);
    EXPECT_TRUE(sawUnderflowFlag);
}

TEST(AudioJitterTraceTest, PrunesOldTraceFiles) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    QDir dir(tempDir.path());
    ASSERT_TRUE(dir.mkpath(QStringLiteral("jitter-trace")));
    ASSERT_TRUE(dir.cd(QStringLiteral("jitter-trace")));
    // More stale files than the retention cap.
    for (int i = 0; i < 30; ++i) {
        QFile f(dir.filePath(QStringLiteral("jitter_old_%1.csv").arg(i)));
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("stale\n");
    }

    {
        AudioJitterTrace trace(tempDir.path(),
                QStringLiteral("Test Device"),
                QStringLiteral("TestAPI"),
                44100.0,
                1024,
                false);
    }

    // 23 survivors + the new trace = the retention cap of 24.
    EXPECT_EQ(traceFiles(tempDir.path()).size(), 24);
}

} // namespace
