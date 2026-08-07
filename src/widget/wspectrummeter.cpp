#include "widget/wspectrummeter.h"

#include <QPainter>
#include <QTimer>

#include "control/controlproxy.h"
#include "engine/enginespectrum.h"
#include "moc_wspectrummeter.cpp"
#include "skin/legacy/skincontext.h"
#include "util/spectrumconfig.h"

namespace {

constexpr int kSegmentGap = 2;   // px between LED segments
constexpr int kSegmentHeight = 5; // px per LED segment
constexpr int kBarGap = 3;       // px between bars
constexpr double kMinBarWidth = 4.0; // px below which the gap is given up
constexpr int kPadding = 6;      // px inset from the widget edge

// Below this fraction the partially lit top LED isn't worth drawing.
constexpr double kMinVisibleSegmentFraction = 0.04;

// Classic device palette, tied into Andy's stem accent colors.
const QColor kColorLow(0x45, 0xdb, 0x6b);   // green up to 60 %
const QColor kColorMid(0xff, 0xa6, 0x30);   // amber up to 85 %
const QColor kColorHigh(0xff, 0x45, 0x45);  // red above
const QColor kColorOff(0x1d, 0x1d, 0x24);   // unlit segment
const QColor kColorPeak(0xe8, 0xe8, 0xf0);  // peak-hold marker

QColor segmentColor(double fraction) {
    if (fraction < 0.6) {
        return kColorLow;
    }
    if (fraction < 0.85) {
        return kColorMid;
    }
    return kColorHigh;
}

/// Mix a lit segment color towards the unlit color, used to fade the topmost
/// LED by its fractional level so the bars glide instead of stepping.
QColor dimmedSegment(const QColor& lit, double fraction) {
    const double f = qBound(0.0, fraction, 1.0);
    return QColor(
            qRound(kColorOff.red() + (lit.red() - kColorOff.red()) * f),
            qRound(kColorOff.green() + (lit.green() - kColorOff.green()) * f),
            qRound(kColorOff.blue() + (lit.blue() - kColorOff.blue()) * f));
}

} // namespace

WSpectrumMeter::WSpectrumMeter(QWidget* pParent)
        : WWidget(pParent),
          m_numBands(0),
          m_lastFrameMs(0),
          m_pendingDecayUpdate(false) {
    m_clock.start();
    const int configuredBands = mixxx::SpectrumConfig::current().bands;
    for (int i = 0; i < configuredBands; ++i) {
        // AllowMissingOrInvalid: a shrunk band count in the ini leaves the
        // upper controls unregistered, which must not trip the debug assert.
        auto pBand = std::make_unique<ControlProxy>(
                QStringLiteral("[Spectrum]"),
                QStringLiteral("band_%1").arg(i),
                this,
                ControlFlag::AllowMissingOrInvalid);
        if (!pBand->valid()) {
            // The engine was built with fewer bands (ini changed since
            // startup): stop here rather than drawing bars that never move.
            break;
        }
        pBand->connectValueChanged(this, &WSpectrumMeter::bandChanged);
        m_bands.push_back(std::move(pBand));
    }
    // AllowMissingOrInvalid: skins without the tick box simply leave the file's
    // hot_reload key in charge.
    m_pHotReload = std::make_unique<ControlProxy>(QStringLiteral("[Skin]"),
            QStringLiteral("spectrum_hot_reload"),
            this,
            ControlFlag::AllowMissingOrInvalid);
    if (m_pHotReload->valid()) {
        mixxx::SpectrumConfig::setHotReloadOverride(m_pHotReload->toBool());
        m_pHotReload->connectValueChanged(this, &WSpectrumMeter::hotReloadChanged);
    }

    m_numBands = static_cast<int>(m_bands.size());
    m_values.assign(m_numBands, 0);
    m_displayed.assign(m_numBands, 0);
    m_fallVelocity.assign(m_numBands, 0);
    m_peaks.assign(m_numBands, 0);
    m_peakVelocity.assign(m_numBands, 0);
    m_peakSetMs.assign(m_numBands, 0);
}

WSpectrumMeter::~WSpectrumMeter() {
    // Safety net for a meter destroyed while still visible (skin teardown
    // doesn't reliably deliver a final hide event).
    if (m_registeredListener) {
        EngineSpectrum::unregisterListener();
        m_registeredListener = false;
    }
}

// The engine-side filter bank only runs while at least one meter is actually
// on screen, so the skin-settings toggle (and skins without the widget) drop
// the whole spectrum cost, not just the painting. Qt delivers Show/Hide events
// to children when an ancestor group is toggled, so a visibility-bound
// WidgetGroup wrapper lands here too.
void WSpectrumMeter::showEvent(QShowEvent* e) {
    WWidget::showEvent(e);
    if (!m_registeredListener) {
        EngineSpectrum::registerListener();
        m_registeredListener = true;
    }
}

void WSpectrumMeter::hideEvent(QHideEvent* e) {
    WWidget::hideEvent(e);
    if (m_registeredListener) {
        EngineSpectrum::unregisterListener();
        m_registeredListener = false;
    }
}

void WSpectrumMeter::setup(const QDomNode& node, const SkinContext& context) {
    Q_UNUSED(node);
    Q_UNUSED(context);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void WSpectrumMeter::hotReloadChanged(double value) {
    mixxx::SpectrumConfig::setHotReloadOverride(value != 0);
}

void WSpectrumMeter::bandChanged(double value) {
    Q_UNUSED(value);
    update();
}

void WSpectrumMeter::paintEvent(QPaintEvent* e) {
    Q_UNUSED(e);
    QPainter p(this);
    p.fillRect(rect(), QColor(0x0f, 0x0f, 0x12));

    const mixxx::SpectrumConfig config = mixxx::SpectrumConfig::current();
    const qint64 nowMs = m_clock.elapsed();
    double dt = (nowMs - m_lastFrameMs) / 1000.0;
    m_lastFrameMs = nowMs;
    if (dt <= 0 || dt > 0.2) {
        // First frame or a long stall (window hidden): don't teleport.
        dt = 0.05;
    }
    const int w = width() - 2 * kPadding;
    const int h = height() - 2 * kPadding;
    if (w <= 0 || h <= 0 || m_numBands <= 0) {
        return;
    }

    const int segmentPitch = kSegmentHeight + kSegmentGap;
    const int numSegments = qMax(1, h / segmentPitch);
    // With many bands the fixed gap eats the bars, so give it up before the
    // bars get unreadably thin.
    int barGap = kBarGap;
    double barWidthF = 0;
    while (true) {
        barWidthF = (w - (m_numBands - 1) * barGap) / static_cast<double>(m_numBands);
        if (barWidthF >= kMinBarWidth || barGap == 0) {
            break;
        }
        --barGap;
    }
    if (barWidthF < 1) {
        return;
    }
    const int bottom = kPadding + h;

    bool anyMotionPending = false;
    for (int i = 0; i < m_numBands; ++i) {
        const double value = m_bands[i]->get();
        m_values[i] = value;

        // Rise fast, then drop at a speed that is already high when the fall
        // starts (fallInitialSpeed) and only accelerates on top of it
        // (fallGravity). A fall that starts from zero speed eases in, which
        // is what read as "exponential and too slow".
        if (value >= m_displayed[i]) {
            m_displayed[i] += (value - m_displayed[i]) * config.attack;
            m_fallVelocity[i] = 0;
            if (m_displayed[i] < value - 0.001) {
                anyMotionPending = true;
            }
        } else {
            if (m_fallVelocity[i] <= 0) {
                m_fallVelocity[i] = config.fallInitialSpeed;
            }
            m_fallVelocity[i] += config.fallGravity * dt;
            m_displayed[i] -= m_fallVelocity[i] * dt;
            if (m_displayed[i] <= value) {
                m_displayed[i] = value;
                m_fallVelocity[i] = 0;
            } else {
                anyMotionPending = true;
            }
        }
        const double displayed = m_displayed[i];

        // Peak hold: latch upward instantly, hold, then fall ballistically -
        // same shape as the bars (CP13): start at peakFallSpeed and
        // accelerate under peakFallGravity, which is 0 for the old linear
        // slide. Speeds are in full scales per second.
        if (displayed >= m_peaks[i]) {
            m_peaks[i] = displayed;
            m_peakSetMs[i] = nowMs;
            m_peakVelocity[i] = 0;
        } else if (nowMs - m_peakSetMs[i] > config.peakHoldMs) {
            if (m_peakVelocity[i] <= 0) {
                m_peakVelocity[i] = config.peakFallSpeed;
            }
            m_peakVelocity[i] += config.peakFallGravity * dt;
            m_peaks[i] -= m_peakVelocity[i] * dt;
            if (m_peaks[i] < displayed) {
                m_peaks[i] = displayed;
                m_peakVelocity[i] = 0;
            }
        }
        if (m_peaks[i] > displayed + 0.001) {
            anyMotionPending = true;
        }

        const int x = kPadding + qRound(i * (barWidthF + barGap));
        const int barWidth = qMax(1, qRound(barWidthF));
        const double litExact = qBound(0.0, displayed, 1.0) * numSegments;
        const int litSegments = static_cast<int>(litExact);
        const double topFraction = litExact - litSegments;
        for (int s = 0; s < numSegments; ++s) {
            const int y = bottom - (s + 1) * segmentPitch + kSegmentGap;
            const double fraction = static_cast<double>(s) / numSegments;
            QColor color = kColorOff;
            if (s < litSegments) {
                color = segmentColor(fraction);
            } else if (s == litSegments && config.smoothTopSegment &&
                    topFraction > kMinVisibleSegmentFraction) {
                color = dimmedSegment(segmentColor(fraction), topFraction);
            }
            p.fillRect(x, y, barWidth, kSegmentHeight, color);
        }

        const int peakSegment = qRound(m_peaks[i] * numSegments);
        if (peakSegment > litSegments && peakSegment <= numSegments) {
            const int y = bottom - peakSegment * segmentPitch + kSegmentGap;
            p.fillRect(x, y, barWidth, kSegmentHeight, kColorPeak);
        }
    }

    // While bars are falling or peak markers float above their bars, keep
    // animating even if the audio (and thus the 30 Hz control updates)
    // stopped.
    if (anyMotionPending && !m_pendingDecayUpdate) {
        m_pendingDecayUpdate = true;
        QTimer::singleShot(config.frameIntervalMs, this, [this]() {
            m_pendingDecayUpdate = false;
            update();
        });
    }
}
