#include "engine/enginespectrum.h"

#include <climits>
#include <cmath>

#include "audio/types.h"
#include "engine/filters/enginefilterbiquad1.h"
#include "moc_enginespectrum.cpp"
#include "util/assert.h"
#include "util/defs.h"
#include "util/sample.h"
#include "util/spectrumconfig.h"

namespace {

// Same cadence as the VU meters: fits the display frame rate.
constexpr unsigned int kUpdateRate = 30; // Hz

// Instant attack AND instant decay: the falling motion is animated in
// WSpectrumMeter with gravity ballistics (CP11 T6); a smoothed decay here
// would keep the signal floor artificially high under the falling bars.
constexpr CSAMPLE kAttackSmoothing = 1.0f;
constexpr CSAMPLE kDecaySmoothing = 1.0f;

// Sized to the largest buffer EngineObject::process() can be handed, so the
// audio thread never has to grow it.
constexpr std::size_t kScratchSize = kMaxEngineSamples;

double bandCenterFreq(int band, int numBands) {
    const double ratio = EngineSpectrum::kMaxFreq / EngineSpectrum::kMinFreq;
    return EngineSpectrum::kMinFreq *
            std::pow(ratio, static_cast<double>(band) / (numBands - 1));
}

// Filter sharpness follows the band spacing: a band-pass whose -3 dB width
// equals one band step tiles the spectrum without holes or heavy overlap.
// With BW octaves between neighbors, Q = 1 / (2^(BW/2) - 2^(-BW/2)).
// 16 bands over 40 Hz..16 kHz (8.64 octaves) => BW 0.576 oct => Q 2.5, the
// value that was hard-coded before; 32 bands halve BW and double Q.
double bandQ(int numBands) {
    const double octaves = std::log2(EngineSpectrum::kMaxFreq /
            EngineSpectrum::kMinFreq);
    const double bandwidth = octaves / (numBands - 1);
    return 1.0 / (std::pow(2.0, bandwidth / 2) - std::pow(2.0, -bandwidth / 2));
}

} // namespace

std::atomic<int> EngineSpectrum::s_listeners{0};

// static
void EngineSpectrum::registerListener() {
    s_listeners.fetch_add(1, std::memory_order_relaxed);
}

// static
void EngineSpectrum::unregisterListener() {
    s_listeners.fetch_sub(1, std::memory_order_relaxed);
}

EngineSpectrum::EngineSpectrum(const QString& group)
        : m_bands(mixxx::SpectrumConfig::current().bands),
          m_bandQ(bandQ(m_bands)),
          m_samplesCalculated(0),
          m_wasActive(false),
          m_configuredSampleRate(mixxx::audio::SampleRate()),
          m_scratch(kScratchSize),
          m_sampleRate(QStringLiteral("[App]"), QStringLiteral("samplerate")) {
    m_filters.resize(m_bands);
    m_bandSums.assign(m_bands, 0);
    m_bandValues.assign(m_bands, 0);
    m_bandControls.reserve(m_bands);
    for (int i = 0; i < m_bands; ++i) {
        m_bandControls.push_back(std::make_unique<ControlObject>(
                ConfigKey(group, QStringLiteral("band_%1").arg(i))));
    }
    // Build the filter bank here, off the audio thread, for a default rate.
    // If the engine runs at a different rate, the first active callback goes
    // through the setFrequencyCorners() path, which only rewrites
    // coefficients — no allocation happens in process() at all.
    configureFilters(mixxx::audio::SampleRate(44100));
    reset();
}

EngineSpectrum::~EngineSpectrum() = default;

void EngineSpectrum::configureFilters(mixxx::audio::SampleRate sampleRate) {
    for (int i = 0; i < m_bands; ++i) {
        const double freq = bandCenterFreq(i, m_bands);
        if (!m_filters[i]) {
            m_filters[i] = std::make_unique<EngineFilterBiquad1Band>(
                    sampleRate, freq, m_bandQ);
        } else {
            m_filters[i]->setFrequencyCorners(sampleRate, freq, m_bandQ);
        }
    }
    m_configuredSampleRate = sampleRate;
}

void EngineSpectrum::process(CSAMPLE* pInOut, const std::size_t bufferSize) {
    // No meter widget in the current skin: skip the whole filter bank. The
    // one-time reset() zeroes the band controls so a meter created later
    // starts from an empty display, and clears the accumulators; stale biquad
    // state is fine — the filters re-converge within milliseconds.
    if (s_listeners.load(std::memory_order_relaxed) == 0) {
        if (m_wasActive) {
            reset();
            m_wasActive = false;
        }
        return;
    }
    m_wasActive = true;

    const auto sampleRate = mixxx::audio::SampleRate::fromDouble(m_sampleRate.get());
    if (!sampleRate.isValid()) {
        return;
    }
    if (sampleRate != m_configuredSampleRate) {
        // All filters exist since the constructor: this only rewrites biquad
        // coefficients, which is fine on the audio thread.
        configureFilters(sampleRate);
    }
    VERIFY_OR_DEBUG_ASSERT(bufferSize <= static_cast<std::size_t>(m_scratch.size())) {
        return;
    }

    for (int i = 0; i < m_bands; ++i) {
        m_filters[i]->process(pInOut, m_scratch.data(), bufferSize);
        CSAMPLE sumL, sumR;
        SampleUtil::sumAbsPerChannel(&sumL, &sumR, m_scratch.data(), bufferSize);
        m_bandSums[i] += (sumL + sumR) / 2;
    }

    m_samplesCalculated += static_cast<unsigned int>(bufferSize / 2);

    if (m_samplesCalculated > (sampleRate / kUpdateRate)) {
        const double epsilon = .0001;
        for (int i = 0; i < m_bands; ++i) {
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
    for (int i = 0; i < m_bands; ++i) {
        m_bandControls[i]->set(0);
        m_bandSums[i] = 0;
        m_bandValues[i] = 0;
    }
    m_samplesCalculated = 0;
}
