#pragma once

#include <array>
#include <memory>

#include <QElapsedTimer>

#include "widget/wwidget.h"

class ControlProxy;
class QDomNode;
class SkinContext;

// Retro hi-fi spectrum display: one LED-segment bar per [Spectrum],band_N
// control (bass left, highs right), with per-band peak hold. Painted with
// plain QPainter rects; repaints are driven by the 30 Hz control updates
// from EngineSpectrum, no own timer while idle.
class WSpectrumMeter : public WWidget {
    Q_OBJECT
  public:
    explicit WSpectrumMeter(QWidget* pParent = nullptr);

    void setup(const QDomNode& node, const SkinContext& context);

  protected:
    void paintEvent(QPaintEvent* e) override;

  private slots:
    void bandChanged(double value);

  private:
    static constexpr int kBands = 16; // must match EngineSpectrum::kBands

    std::array<std::unique_ptr<ControlProxy>, kBands> m_bands;
    std::array<double, kBands> m_values;
    // Displayed bar heights fall under gravity instead of tracking the
    // control value directly (CP11 T6).
    std::array<double, kBands> m_displayed;
    std::array<double, kBands> m_fallVelocity;
    std::array<double, kBands> m_peaks;
    std::array<qint64, kBands> m_peakSetMs;
    QElapsedTimer m_clock;
    qint64 m_lastFrameMs;
    bool m_pendingDecayUpdate;
};
