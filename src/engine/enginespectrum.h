#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include "control/controlobject.h"
#include "control/pollingcontrolproxy.h"
#include "engine/engineobject.h"
#include "util/samplebuffer.h"

class EngineFilterBiquad1Band;

// Andy's retro spectrum analyzer: a bank of constant-Q band-pass filters over
// the main output, published as [Spectrum],band_0..band_N controls (0..1) in
// the same scaling/ballistics as the VU meters so skin meters read alike.
// Deliberately no FFT — even 32 biquads on the main bus cost less than one EQ.
// The band count comes from andys_spectrum.ini and is fixed for the lifetime
// of the engine (the controls are created once); the widget reads the same
// value, so both sides agree without talking to each other.
class EngineSpectrum : public EngineObject {
    Q_OBJECT
  public:
    static constexpr double kMinFreq = 40.0;
    static constexpr double kMaxFreq = 16000.0;

    explicit EngineSpectrum(const QString& group);
    ~EngineSpectrum() override;

    void process(CSAMPLE* pInOut, const std::size_t bufferSize) override;

    void reset();

    // Skins without a <SpectrumMeter> shouldn't pay for the filter bank on the
    // audio thread: each WSpectrumMeter registers itself here and process()
    // early-outs while the count is zero.
    static void registerListener();
    static void unregisterListener();

  private:
    void configureFilters(mixxx::audio::SampleRate sampleRate);
    static void doSmooth(CSAMPLE& currentValue, CSAMPLE newValue);

    const int m_bands;
    const double m_bandQ;
    std::vector<std::unique_ptr<ControlObject>> m_bandControls;
    std::vector<std::unique_ptr<EngineFilterBiquad1Band>> m_filters;
    std::vector<CSAMPLE> m_bandSums;
    std::vector<CSAMPLE> m_bandValues;
    unsigned int m_samplesCalculated;
    bool m_wasActive;
    mixxx::audio::SampleRate m_configuredSampleRate;
    mixxx::SampleBuffer m_scratch;

    PollingControlProxy m_sampleRate;

    static std::atomic<int> s_listeners;
};
