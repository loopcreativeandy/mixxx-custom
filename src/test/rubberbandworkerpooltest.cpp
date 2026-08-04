#include "engine/bufferscalers/rubberbandworkerpool.h"

#include <gtest/gtest.h>

#include <cmath>

#include "engine/bufferscalers/enginebufferscalerubberband.h"
#include "engine/readaheadmanager.h"
#include "test/mixxxtest.h"
#include "util/sample.h"
#include "util/samplebuffer.h"
#include "util/types.h"

namespace {

constexpr SINT kOutputFrames = 1024;
constexpr int kIterations = 50;
const mixxx::audio::SampleRate kSampleRate(44100);

// Feeds a deterministic multi-tone signal so both scalers see identical input.
class DeterministicReadAheadManager : public ReadAheadManager {
  public:
    DeterministicReadAheadManager() = default;

    SINT getNextSamples(double dRate,
            CSAMPLE* pBuffer,
            SINT requested_samples,
            mixxx::audio::ChannelCount channelCount) override {
        Q_UNUSED(dRate);
        const int chCount = channelCount;
        for (SINT i = 0; i < requested_samples; ++i) {
            const SINT frame = (m_samplesRead + i) / chCount;
            const int ch = (m_samplesRead + i) % chCount;
            // A different frequency per channel so a channel mixup would
            // change the output.
            pBuffer[i] = 0.5f *
                    static_cast<float>(
                            sin(2.0 * M_PI * (110.0 + 55.0 * ch) * frame /
                                    kSampleRate));
        }
        m_samplesRead += requested_samples;
        return requested_samples;
    }

  private:
    SINT m_samplesRead = 0;
};

class RubberBandWorkerPoolTest : public MixxxTest {
  protected:
    // Runs a full stem-channel scaling session with the given pool
    // configuration and returns the concatenated output.
    std::vector<CSAMPLE> scaleStemTrack(bool parallelStems, bool engineFiner) {
        config()->setValue(
                ConfigKey(QStringLiteral("[App]"),
                        QStringLiteral("keylock_parallel_stems")),
                parallelStems);
        RubberBandWorkerPool::createInstance(config());

        std::vector<CSAMPLE> output;
        {
            DeterministicReadAheadManager readAhead;
            EngineBufferScaleRubberBand scaler(&readAhead);
            scaler.setSignal(kSampleRate, mixxx::audio::ChannelCount::stem());
            scaler.useEngineFiner(engineFiner);

            double tempoRatio = 1.25;
            double pitchRatio = 1.0;
            scaler.setScaleParameters(1.0, &tempoRatio, &pitchRatio);

            const SINT bufferSize = kOutputFrames *
                    mixxx::audio::ChannelCount::stem();
            mixxx::SampleBuffer buffer(bufferSize);
            for (int i = 0; i < kIterations; ++i) {
                scaler.scaleBuffer(buffer.data(), bufferSize);
                output.insert(output.end(),
                        buffer.data(),
                        buffer.data() + bufferSize);
            }
        }
        RubberBandWorkerPool::destroy();
        return output;
    }
};

TEST_F(RubberBandWorkerPoolTest, ParallelStemScalingMatchesSerial) {
    // The parallel scheduling must neither deadlock (this test completing at
    // all covers that) nor alter the audio: the same four 2-channel
    // stretchers process the same input, only on different threads.
    const bool engineFiner = EngineBufferScaleRubberBand::isEngineFinerAvailable();
    std::vector<CSAMPLE> serial = scaleStemTrack(false, engineFiner);
    std::vector<CSAMPLE> parallel = scaleStemTrack(true, engineFiner);

    ASSERT_EQ(serial.size(), parallel.size());

    // The output must not be silence, otherwise this test passes vacuously.
    CSAMPLE peak = 0;
    for (CSAMPLE sample : serial) {
        peak = std::max(peak, std::abs(sample));
    }
    EXPECT_GT(peak, 0.1f);

    for (size_t i = 0; i < serial.size(); ++i) {
        ASSERT_EQ(serial[i], parallel[i]) << "first mismatch at sample " << i;
    }
}

} // namespace
