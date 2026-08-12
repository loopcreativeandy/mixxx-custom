#include "soundio/audiojittertrace.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>
#include <QtDebug>

#include <algorithm>
#include <vector>

namespace {

// ~6 min of records at a 21 ms buffer; the writer drains every 250 ms, so the
// buffer only fills if the writer thread is starved for minutes.
constexpr int kFifoSize = 1 << 14;

constexpr int kDrainIntervalMs = 250;

// Keep this many finished trace files per settings dir; older ones are pruned
// so an always-on trace cannot grow the settings dir without bound.
constexpr int kMaxTraceFiles = 24;

// A callback whose inter-arrival gap exceeds this multiple of the nominal
// buffer period missed its deadline: the DAC ran dry before we were asked for
// the next buffer.
constexpr double kLateGapRatio = 1.5;

constexpr int kNumWorstGaps = 10;

QString sanitizeForFilename(const QString& name) {
    QString s = name;
    s.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")),
            QStringLiteral("_"));
    return s.left(48);
}

} // anonymous namespace

/// Drains the record FIFO to the CSV file and accumulates the summary stats.
/// Runs at low priority; the audio thread never touches file I/O.
class AudioJitterTrace::Writer : public QThread {
  public:
    Writer(AudioJitterTrace* pTrace,
            const QString& filePath,
            const QString& headerInfo,
            double nominalPeriodMs)
            : m_pTrace(pTrace),
              m_file(filePath),
              m_headerInfo(headerInfo),
              m_nominalPeriodMs(nominalPeriodMs),
              m_stop(false) {
    }

    void stop() {
        m_stop.store(true, std::memory_order_release);
    }

    void run() override {
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            qWarning() << "AudioJitterTrace: cannot open" << m_file.fileName();
            return;
        }
        QTextStream out(&m_file);
        out << m_headerInfo;
        out << "t_ms,interval_ms,dur_ms,frames,flags\n";
        while (!m_stop.load(std::memory_order_acquire)) {
            drain(out);
            out.flush();
            msleep(kDrainIntervalMs);
        }
        drain(out);
        out.flush();
        m_file.close();
        logSummary();
    }

  private:
    void drain(QTextStream& out) {
        Record rec;
        while (m_pTrace->m_fifo.read(&rec, 1) == 1) {
            const double tMs = rec.entryNs / 1e6;
            const double durMs = rec.durNs / 1e6;
            double intervalMs = -1.0;
            if (m_count > 0) {
                intervalMs = (rec.entryNs - m_lastEntryNs) / 1e6;
                accumulate(tMs, intervalMs);
            }
            m_lastEntryNs = rec.entryNs;
            ++m_count;
            m_maxDurMs = std::max(m_maxDurMs, durMs);
            if (rec.flags & (paOutputUnderflow | paInputOverflow)) {
                ++m_xrunFlagged;
            }
            out << QString::number(tMs, 'f', 3) << ','
                << QString::number(intervalMs, 'f', 3) << ','
                << QString::number(durMs, 'f', 3) << ','
                << rec.frames << ',' << rec.flags << '\n';
        }
    }

    void accumulate(double tMs, double intervalMs) {
        m_intervalSumMs += intervalMs;
        m_maxIntervalMs = std::max(m_maxIntervalMs, intervalMs);
        if (intervalMs > m_nominalPeriodMs * kLateGapRatio) {
            ++m_lateCount;
            if (m_worstGaps.size() < kNumWorstGaps ||
                    intervalMs > m_worstGaps.back().second) {
                m_worstGaps.emplace_back(tMs, intervalMs);
                std::sort(m_worstGaps.begin(),
                        m_worstGaps.end(),
                        [](const auto& a, const auto& b) {
                            return a.second > b.second;
                        });
                if (m_worstGaps.size() > kNumWorstGaps) {
                    m_worstGaps.pop_back();
                }
            }
        }
    }

    void logSummary() {
        const qint64 dropped =
                m_pTrace->m_droppedRecords.load(std::memory_order_relaxed);
        qInfo() << "AudioJitterTrace summary for" << m_file.fileName();
        qInfo() << "  callbacks:" << m_count
                << " nominal period:" << m_nominalPeriodMs << "ms"
                << " mean interval:"
                << (m_count > 1 ? m_intervalSumMs / (m_count - 1) : 0.0) << "ms";
        qInfo() << "  worst interval:" << m_maxIntervalMs << "ms"
                << " worst callback duration:" << m_maxDurMs << "ms"
                << " late arrivals (>" << m_nominalPeriodMs * kLateGapRatio
                << "ms):" << m_lateCount
                << " xrun-flagged:" << m_xrunFlagged
                << " records dropped:" << dropped;
        for (const auto& [tMs, gapMs] : m_worstGaps) {
            qInfo() << "  late gap of" << gapMs << "ms at t=" << tMs / 1000.0
                    << "s";
        }
    }

    AudioJitterTrace* const m_pTrace;
    QFile m_file;
    const QString m_headerInfo;
    const double m_nominalPeriodMs;
    std::atomic<bool> m_stop;

    qint64 m_count = 0;
    qint64 m_lastEntryNs = 0;
    double m_intervalSumMs = 0.0;
    double m_maxIntervalMs = 0.0;
    double m_maxDurMs = 0.0;
    qint64 m_lateCount = 0;
    qint64 m_xrunFlagged = 0;
    std::vector<std::pair<double, double>> m_worstGaps; // (t_ms, interval_ms)
};

AudioJitterTrace::AudioJitterTrace(const QString& settingsPath,
        const QString& deviceName,
        const QString& hostApi,
        double sampleRate,
        int framesPerBuffer,
        bool isClkRefDevice)
        : m_fifo(kFifoSize),
          m_pending{},
          m_droppedRecords(0),
          m_nominalPeriodMs(framesPerBuffer > 0 && sampleRate > 0
                          ? framesPerBuffer / sampleRate * 1000.0
                          : 0.0) {
    QDir dir(settingsPath);
    dir.mkpath(QStringLiteral("jitter-trace"));
    dir.cd(QStringLiteral("jitter-trace"));

    // Prune the oldest finished traces so an always-on trace stays bounded.
    QStringList old = dir.entryList(QStringList() << QStringLiteral("jitter_*.csv"),
            QDir::Files,
            QDir::Time);
    for (int i = kMaxTraceFiles - 1; i < old.size(); ++i) {
        dir.remove(old.at(i));
    }

    const QString stamp = QDateTime::currentDateTime().toString(
            QStringLiteral("yyMMdd_HHmmss"));
    const QString filePath = dir.filePath(QStringLiteral("jitter_%1_%2.csv")
                    .arg(sanitizeForFilename(deviceName), stamp));

    QString header;
    header += QStringLiteral("# device: %1\n").arg(deviceName);
    header += QStringLiteral("# host_api: %1\n").arg(hostApi);
    header += QStringLiteral("# sample_rate: %1\n").arg(sampleRate);
    header += QStringLiteral("# frames_per_buffer: %1\n").arg(framesPerBuffer);
    header += QStringLiteral("# nominal_period_ms: %1\n")
                      .arg(m_nominalPeriodMs, 0, 'f', 3);
    header += QStringLiteral("# clock_reference: %1\n")
                      .arg(isClkRefDevice ? QStringLiteral("yes")
                                          : QStringLiteral("no"));
    header += QStringLiteral("# started: %1\n")
                      .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    header += QStringLiteral(
            "# flags: 1=InputUnderflow 2=InputOverflow 4=OutputUnderflow "
            "8=OutputOverflow 16=PrimingOutput\n");

    m_timer.start();
    m_pWriter = std::make_unique<Writer>(
            this, filePath, header, m_nominalPeriodMs);
    m_pWriter->start(QThread::LowPriority);
    qInfo() << "AudioJitterTrace: recording to" << filePath;
}

AudioJitterTrace::~AudioJitterTrace() {
    if (m_pWriter) {
        m_pWriter->stop();
        // The writer sleeps at most kDrainIntervalMs; give it ample slack.
        if (!m_pWriter->wait(2000)) {
            qWarning() << "AudioJitterTrace: writer thread did not stop in time";
            m_pWriter->terminate();
            m_pWriter->wait(500);
        }
    }
}

void AudioJitterTrace::onCallbackEntry(PaStreamCallbackFlags statusFlags) {
    m_pending.entryNs = m_timer.elapsed().toIntegerNanos();
    m_pending.flags = static_cast<quint32>(statusFlags);
}

void AudioJitterTrace::onCallbackExit(int framesPerBuffer) {
    m_pending.durNs = m_timer.elapsed().toIntegerNanos() - m_pending.entryNs;
    m_pending.frames = static_cast<quint32>(framesPerBuffer);
    if (m_fifo.write(&m_pending, 1) != 1) {
        m_droppedRecords.fetch_add(1, std::memory_order_relaxed);
    }
}
