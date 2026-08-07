#pragma once

#include <memory>
#include <vector>

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
    ~WSpectrumMeter() override;

    void setup(const QDomNode& node, const SkinContext& context);

  protected:
    void paintEvent(QPaintEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

  private slots:
    void bandChanged(double value);
    void hotReloadChanged(double value);

  private:
    // Bar count from andys_spectrum.ini, matching EngineSpectrum: both read
    // the same key once at startup. Any bar whose [Spectrum],band_N control
    // is missing (ini edited between engine and skin creation) is dropped, so
    // the widget can never draw dead bars.
    int m_numBands;

    std::vector<std::unique_ptr<ControlProxy>> m_bands;
    // "Spectrum Live Reload" tick box in the skin settings. Owned here rather
    // than by SpectrumConfig because a ControlProxy must live and die on one
    // thread; this widget pushes the value into SpectrumConfig instead.
    std::unique_ptr<ControlProxy> m_pHotReload;
    std::vector<double> m_values;
    // Displayed bar heights fall under gravity instead of tracking the
    // control value directly (CP11 T6).
    std::vector<double> m_displayed;
    std::vector<double> m_fallVelocity;
    std::vector<double> m_peaks;
    std::vector<double> m_peakVelocity;
    std::vector<qint64> m_peakSetMs;
    QElapsedTimer m_clock;
    qint64 m_lastFrameMs;
    bool m_pendingDecayUpdate;
    bool m_registeredListener = false;
};
