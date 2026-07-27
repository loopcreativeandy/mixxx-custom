#include "widget/wspectrummeter.h"

#include <QPainter>
#include <QTimer>

#include "control/controlproxy.h"
#include "moc_wspectrummeter.cpp"
#include "skin/legacy/skincontext.h"
#include "util/spectrumconfig.h"

namespace {

constexpr int kSegmentGap = 2;   // px between LED segments
constexpr int kSegmentHeight = 5; // px per LED segment
constexpr int kBarGap = 3;       // px between bars
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
          m_lastFrameMs(0),
          m_pendingDecayUpdate(false) {
    m_values.fill(0);
    m_displayed.fill(0);
    m_fallVelocity.fill(0);
    m_peaks.fill(0);
    m_peakSetMs.fill(0);
    m_clock.start();
    for (int i = 0; i < kBands; ++i) {
        m_bands[i] = std::make_unique<ControlProxy>(
                QStringLiteral("[Spectrum]"),
                QStringLiteral("band_%1").arg(i),
                this);
        m_bands[i]->connectValueChanged(this, &WSpectrumMeter::bandChanged);
    }
}

void WSpectrumMeter::setup(const QDomNode& node, const SkinContext& context) {
    Q_UNUSED(node);
    Q_UNUSED(context);
    setAttribute(Qt::WA_OpaquePaintEvent);
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
    if (w <= 0 || h <= 0) {
        return;
    }

    const int segmentPitch = kSegmentHeight + kSegmentGap;
    const int numSegments = qMax(1, h / segmentPitch);
    const double barWidthF =
            (w - (kBands - 1) * kBarGap) / static_cast<double>(kBands);
    if (barWidthF < 1) {
        return;
    }
    const int bottom = kPadding + h;

    bool anyMotionPending = false;
    for (int i = 0; i < kBands; ++i) {
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

        // Peak hold: latch upward instantly, hold, then slide down.
        if (displayed >= m_peaks[i]) {
            m_peaks[i] = displayed;
            m_peakSetMs[i] = nowMs;
        } else {
            const qint64 sincePeak = nowMs - m_peakSetMs[i];
            if (sincePeak > config.peakHoldMs) {
                // Speed is in full scales per second - the old code also
                // divided by the segment count, which left the markers
                // hanging near the top for half a minute.
                m_peaks[i] -= config.peakFallSpeed *
                        (sincePeak - config.peakHoldMs) / 1000.0;
                m_peakSetMs[i] = nowMs - static_cast<qint64>(config.peakHoldMs);
                if (m_peaks[i] < displayed) {
                    m_peaks[i] = displayed;
                }
            }
        }
        if (m_peaks[i] > displayed + 0.001) {
            anyMotionPending = true;
        }

        const int x = kPadding + qRound(i * (barWidthF + kBarGap));
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
