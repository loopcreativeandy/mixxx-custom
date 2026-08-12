#pragma once

#include <portaudio.h>

#include <QString>
#include <QThread>
#include <atomic>
#include <memory>

#include "util/fifo.h"
#include "util/performancetimer.h"

/// Records per-callback timing of a PortAudio stream to a CSV file so that
/// dropouts can be diagnosed after a session: did the callback arrive late
/// (driver/DPC jitter) or run long (compute)?
///
/// The audio thread only stamps a small record into a lock-free ring buffer;
/// a low-priority writer thread drains it to
/// <settings>/jitter-trace/jitter_<device>_<stamp>.csv every 250 ms and logs
/// a jitter summary to mixxx.log when the stream closes.
class AudioJitterTrace {
  public:
    AudioJitterTrace(const QString& settingsPath,
            const QString& deviceName,
            const QString& hostApi,
            double sampleRate,
            int framesPerBuffer,
            bool isClkRefDevice);
    ~AudioJitterTrace();

    /// Called from the audio callback. Wait-free.
    void onCallbackEntry(PaStreamCallbackFlags statusFlags);
    void onCallbackExit(int framesPerBuffer);

    /// RAII helper for use inside the process callbacks: records entry on
    /// construction and exit when the callback scope unwinds.
    class Scope {
      public:
        Scope(AudioJitterTrace* pTrace,
                PaStreamCallbackFlags statusFlags,
                int framesPerBuffer)
                : m_pTrace(pTrace), m_framesPerBuffer(framesPerBuffer) {
            if (m_pTrace) {
                m_pTrace->onCallbackEntry(statusFlags);
            }
        }
        ~Scope() {
            if (m_pTrace) {
                m_pTrace->onCallbackExit(m_framesPerBuffer);
            }
        }

      private:
        AudioJitterTrace* const m_pTrace;
        const int m_framesPerBuffer;
        DISALLOW_COPY_AND_ASSIGN(Scope);
    };

  private:
    struct Record {
        qint64 entryNs;  // since trace start
        qint64 durNs;    // time spent inside the callback
        quint32 frames;  // frames requested by this callback
        quint32 flags;   // PaStreamCallbackFlags (5 bits used)
    };

    class Writer;

    PerformanceTimer m_timer;
    FIFO<Record> m_fifo;
    Record m_pending;
    std::atomic<qint64> m_droppedRecords;
    std::unique_ptr<Writer> m_pWriter;
    double m_nominalPeriodMs;

    DISALLOW_COPY_AND_ASSIGN(AudioJitterTrace);
};
