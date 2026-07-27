#include "engine/enginespectrum.h"

#include <climits>
#include <cmath>

#include "audio/types.h"
#include "engine/filters/enginefilterbiquad1.h"
#include "moc_enginespectrum.cpp"
#include "util/sample.h"

namespace {

// Same cadence as the VU meters: fits the display frame rate.
constexpr unsigned int kUpdateRate = 30; // Hz

// VU-style ballistics: instant attack, smoothed decay.
constexpr CSAMPLE kAttackSmoothing = 1.0f;
constexpr CSAMPLE kDecaySmoothing = 0.2f;

// Filter sharpness. The 16 bands are spaced ~2/3 octave apart
// ((16000/40)^(1/15) ≈ 1.49x per step); Q 2.5 keeps neighbors from
// bleeding into each other without ringing.
constexpr double kBandQ = 2.5;

constexpr std::size_t kInitialScratchSize = 16384;

double bandCenterFreq(int band) {
    const double ratio = EngineSpectrum::kMaxFreq / EngineSpectrum::kMinFreq;
    return EngineSpectrum::kMinFreq *
            std::pow(ratio,
                    static_cast<double>(band) /
                            (EngineSpectrum::kBands - 1));
}

} // namespace

EngineSpectrum::EngineSpectrum(const QString& group)
        : m_samplesCalculated(0),
          m_configuredSampleRate(mixxx::audio::SampleRate()),
          m_scratch(kInitialScratchSize),
          m_sampleRate(QStringLiteral("[App]"), QStringLiteral("samplerate")) {
    for (int i = 0; i < kBands; ++i) {
        m_bandControls[i] = std::make_unique<ControlObject>(
                ConfigKey(group, QStringLiteral("band_%1").arg(i)));
        m_bandSums[i] = 0;
        m_bandValues[i] = 0;
    }
    reset();
}

EngineSpectrum::~EngineSpectrum() = default;

void EngineSpectrum::configureFilters(mixxx::audio::SampleRate sampleRate) {
    for (int i = 0; i < kBands; ++i) {
        const double freq = bandCenterFreq(i);
        if (!m_filters[i]) {
            m_filters[i] = std::make_unique<EngineFilterBiquad1Band>(
                    sampleRate, freq, kBandQ);
        } else {
            m_filters[i]->setFrequencyCorners(sampleRate, freq, kBandQ);
        }
    }
    m_configuredSampleRate = sampleRate;
}

void EngineSpectrum::process(CSAMPLE* pInOut, const std::size_t bufferSize) {
    const auto sampleRate = mixxx::audio::SampleRate::fromDouble(m_sampleRate.get());
    if (!sampleRate.isValid()) {
        return;
    }
    if (sampleRate != m_configuredSampleRate) {
        configureFilters(sampleRate);
    }
    if (m_scratch.size() < static_cast<SINT>(bufferSize)) {
        m_scratch = mixxx::SampleBuffer(bufferSize);
    }

    for (int i = 0; i < kBands; ++i) {
        m_filters[i]->process(pInOut, m_scratch.data(), bufferSize);
        CSAMPLE sumL, sumR;
        SampleUtil::sumAbsPerChannel(&sumL, &sumR, m_scratch.data(), bufferSize);
        m_bandSums[i] += (sumL + sumR) / 2;
    }

    m_samplesCalculated += static_cast<unsigned int>(bufferSize / 2);

    if (m_samplesCalculated > (sampleRate / kUpdateRate)) {
        const double epsilon = .0001;
        for (int i = 0; i < kBands; ++i) {
            // Same log scaling as EngineVuMeter so the meters read alike.
            doSmooth(m_bandValues[i],
                    std::log10(SHRT_MAX * m_bandSums[i] /
                                    (m_samplesCalculated * 1000) +
                            1));
            if (fabs(m_bandValues[i] - m_bandControls[i]->get()) > epsilon) {
                m_bandControls[i]->set(m_bandValues[i]);
            }
            m_bandSums[i] = 0;
        }
        m_samplesCalculated = 0;
    }
}

void EngineSpectrum::doSmooth(CSAMPLE& currentValue, CSAMPLE newValue) {
    if (currentValue > newValue) {
        currentValue -= kDecaySmoothing * (currentValue - newValue);
    } else {
        currentValue += kAttackSmoothing * (newValue - currentValue);
    }
    if (currentValue < 0) {
        currentValue = 0;
    }
    if (currentValue > 1.0) {
        currentValue = 1.0;
    }
}

void EngineSpectrum::reset() {
    for (int i = 0; i < kBands; ++i) {
        m_bandControls[i]->set(0);
        m_bandSums[i] = 0;
        m_bandValues[i] = 0;
    }
    m_samplesCalculated = 0;
}
