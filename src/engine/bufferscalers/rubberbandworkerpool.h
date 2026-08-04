#pragma once

#include <QThreadPool>

#include "audio/types.h"
#include "preferences/usersettings.h"
#include "util/singleton.h"

// RubberBandWorkerPool is a global pool manager for RubberBandWorkerPool. It
// allows a the Engine thread to use a pool of agnostic RubberBandWorker which
// can be distributed stretching job
class RubberBandTask;

class RubberBandWorkerPool : public QThreadPool, public Singleton<RubberBandWorkerPool> {
  public:
    const mixxx::audio::ChannelCount& channelPerWorker() const {
        return m_channelPerWorker;
    }

    /// Try to schedule a stretching task on a pool worker. Returns false when
    /// parallel scheduling is disabled or no worker is free; the caller must
    /// then run the task on its own thread.
    bool tryStartTask(RubberBandTask* pTask);

  protected:
    RubberBandWorkerPool(UserSettingsPointer pConfig = nullptr);

  private:
    ;
    mixxx::audio::ChannelCount m_channelPerWorker;
    // [App],keylock_parallel_stems - when false, all stretchers run serially
    // on the engine thread (upstream's effective behavior).
    bool m_parallelAllowed;

    friend class Singleton<RubberBandWorkerPool>;
};
