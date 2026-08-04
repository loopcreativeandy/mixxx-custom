#include "engine/bufferscalers/rubberbandworkerpool.h"

#include <rubberband/RubberBandStretcher.h>

#include "engine/bufferscalers/rubberbandtask.h"
#include "engine/engine.h"
#include "util/assert.h"

RubberBandWorkerPool::RubberBandWorkerPool(UserSettingsPointer pConfig)
        : QThreadPool() {
    bool multiThreadedOnStereo = pConfig &&
            pConfig->getValue(ConfigKey(QStringLiteral("[App]"),
                                      QStringLiteral("keylock_multithreading")),
                    false);
    m_parallelAllowed = pConfig &&
            pConfig->getValue(ConfigKey(QStringLiteral("[App]"),
                                      QStringLiteral("keylock_parallel_stems")),
                    false);
    m_channelPerWorker = multiThreadedOnStereo
            ? mixxx::audio::ChannelCount::mono()
            : mixxx::audio::ChannelCount::stereo();
    DEBUG_ASSERT(mixxx::kMaxEngineChannelInputCount % m_channelPerWorker == 0);

    int numCore = QThread::idealThreadCount();
    int numRBTasks = qMin(numCore, mixxx::kMaxEngineChannelInputCount / m_channelPerWorker);

    qDebug() << "RubberBand will use" << numRBTasks << "tasks to scale the audio signal"
             << (m_parallelAllowed ? "(parallel scheduling enabled)"
                                   : "(parallel scheduling disabled)");

    setThreadPriority(QThread::HighPriority);
    // The RB pool will only be used to scale n-1 buffer sample, so the engine
    // thread takes care of the last buffer and doesn't have to be idle.
    setMaxThreadCount(numRBTasks - 1);

    // Once spawned, keep the workers alive forever. The default 30 s expiry
    // would tear idle threads down and re-spawn them from inside the audio
    // callback, which is not realtime-safe.
    setExpiryTimeout(-1);

    // NOTE: upstream reserved all maxThreadCount() slots here ("so the engine
    // thread will also perform a stretching operation"). QThreadPool counts
    // reserved threads as active, so tryStart() then refuses to schedule
    // almost every task and the stretchers silently run serially on the
    // engine thread. The engine thread still gets its share of work without
    // any reservation: RubberBandWrapper::process() runs a task inline
    // whenever tryStart() finds no free worker.
}

bool RubberBandWorkerPool::tryStartTask(RubberBandTask* pTask) {
    if (!m_parallelAllowed) {
        return false;
    }
    return tryStart(pTask);
}
