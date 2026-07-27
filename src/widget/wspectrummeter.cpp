#include "widget/wspectrummeter.h"

#include <QPainter>
#include <QTimer>

#include "control/controlproxy.h"
#include "moc_wspectrummeter.cpp"
#include "skin/legacy/skincontext.h"

namespace {

constexpr int kSegmentGap = 2;   // px between LED segments
constexpr int kSegmentHeight = 5; // px per LED segment
constexpr int kBarGap = 3;       // px between bars
constexpr int kPadding = 6;      // px inset from the widget edge

constexpr qint64 kPeakHoldMs = 700;
constexpr double kPeakDecayPerSec = 0.9; // fraction of full scale per second

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

} // namespace

WSpectrumMeter::WSpectrumMeter(QWidget* pParent)
        : WWidget(pParent),
          m_pendingDecayUpdate(false) {
    m_values.fill(0);
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

    const qint64 nowMs = m_clock.elapsed();
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

    bool anyPeakFloating = false;
    for (int i = 0; i < kBands; ++i) {
        const double value = m_bands[i]->get();
        m_values[i] = value;

        // Peak hold: latch upward instantly, hold, then slide down.
        if (value >= m_peaks[i]) {
            m_peaks[i] = value;
            m_peakSetMs[i] = nowMs;
        } else {
            const qint64 sincePeak = nowMs - m_peakSetMs[i];
            if (sincePeak > kPeakHoldMs) {
                m_peaks[i] -= kPeakDecayPerSec *
                        (sincePeak - kPeakHoldMs) / 1000.0 / numSegments;
                m_peakSetMs[i] = nowMs - kPeakHoldMs;
                if (m_peaks[i] < value) {
                    m_peaks[i] = value;
                }
            }
        }
        if (m_peaks[i] > value + 0.001) {
            anyPeakFloating = true;
        }

        const int x = kPadding + qRound(i * (barWidthF + kBarGap));
        const int barWidth = qMax(1, qRound(barWidthF));
        const int litSegments = qRound(value * numSegments);
        for (int s = 0; s < numSegments; ++s) {
            const int y = bottom - (s + 1) * segmentPitch + kSegmentGap;
            const double fraction = static_cast<double>(s) / numSegments;
            p.fillRect(x,
                    y,
                    barWidth,
                    kSegmentHeight,
                    s < litSegments ? segmentColor(fraction) : kColorOff);
        }

        const int peakSegment = qRound(m_peaks[i] * numSegments);
        if (peakSegment > litSegments && peakSegment <= numSegments) {
            const int y = bottom - peakSegment * segmentPitch + kSegmentGap;
            p.fillRect(x, y, barWidth, kSegmentHeight, kColorPeak);
        }
    }

    // While a peak marker floats above its bar it must keep decaying even if
    // the audio (and thus the 30 Hz control updates) stopped.
    if (anyPeakFloating && !m_pendingDecayUpdate) {
        m_pendingDecayUpdate = true;
        QTimer::singleShot(50, this, [this]() {
            m_pendingDecayUpdate = false;
            update();
        });
    }
}
