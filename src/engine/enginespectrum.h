#pragma once

#include <array>
#include <memory>

#include "control/controlobject.h"
#include "control/pollingcontrolproxy.h"
#include "engine/engineobject.h"
#include "util/samplebuffer.h"

class EngineFilterBiquad1Band;

// Andy's retro spectrum analyzer: a bank of constant-Q band-pass filters over
// the main output, published as [Spectrum],band_0..band_N controls (0..1) in
// the same scaling/ballistics as the VU meters so skin meters read alike.
// Deliberately no FFT — 16 biquads on the main bus cost less than one EQ.
class EngineSpectrum : public EngineObject {
    Q_OBJECT
  public:
    static constexpr int kBands = 16;
    static constexpr double kMinFreq = 40.0;
    static constexpr double kMaxFreq = 16000.0;

    explicit EngineSpectrum(const QString& group);
    ~EngineSpectrum() override;

    void process(CSAMPLE* pInOut, const std::size_t bufferSize) override;

    void reset();

  private:
    void configureFilters(mixxx::audio::SampleRate sampleRate);
    static void doSmooth(CSAMPLE& currentValue, CSAMPLE newValue);

    std::array<std::unique_ptr<ControlObject>, kBands> m_bandControls;
    std::array<std::unique_ptr<EngineFilterBiquad1Band>, kBands> m_filters;
    std::array<CSAMPLE, kBands> m_bandSums;
    std::array<CSAMPLE, kBands> m_bandValues;
    unsigned int m_samplesCalculated;
    mixxx::audio::SampleRate m_configuredSampleRate;
    mixxx::SampleBuffer m_scratch;

    PollingControlProxy m_sampleRate;
};
