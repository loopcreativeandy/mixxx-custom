#pragma once

#include <QString>

#include "widget/slidereventhandler.h"
#include "widget/wwidget.h"
#include "widget/wpixmapstore.h"

class QDomNode;
class SkinContext;
class ControlProxy;

/// A widget for a slider composed of a background pixmap and a handle.
class WSliderComposed : public WWidget  {
    Q_OBJECT
  public:
    explicit WSliderComposed(QWidget* parent = nullptr);
    ~WSliderComposed() override;

    void setup(const QDomNode& node, const SkinContext& context);
    void setSliderPixmap(
            const PixmapSource& sourceSlider,
            Paintable::DrawMode drawMode,
            double scaleFactor);
    void setHandlePixmap(
            const PixmapSource& sourceHandle,
            Paintable::DrawMode mode,
            double scaleFactor);
    // This is called by LegacySkinParser::setupConnections() before setup()
    // because it needs 'horizontal' for picking the correct keyboard shortcut
    // command (left/right or up/down.
    // Doesn't recognize variables, hence we don't store the result in m_bHorizontal,
    // that's done in setup() where we have a SkinContext, i.e. variable support.
    bool tryParseHorizontal(const QDomNode& node) const;
    void inputActivity();

  public slots:
    void onConnectedControlChanged(double dParameter, double dValue) override;
    void fillDebugTooltip(QStringList* debug) override;

  private slots:
    // Enable/disable the non-linear response at runtime from a control bound to
    // a skin-settings toggle. Off = linear.
    void slotExponentToggleChanged(double v);
    // Recompute the asymmetric-rate parameter window when the deck's
    // rateRange or rate_dir changes.
    void slotRateClampSourceChanged(double v);

  protected:
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
    void drawBar(QPainter* pPainter);
    void wheelEvent(QWheelEvent* e) override;
    void resizeEvent(QResizeEvent* pEvent) override;

  private:
    double calculateHandleLength();
    void unsetPixmaps();
    void applyRateClampWindow();

    // Length of handle in pixels
    double m_dHandleLength;
    // Length of the slider in pixels.
    double m_dSliderLength;
    // True if it's a horizontal slider
    bool m_bHorizontal;
    // Properties to draw the level bar
    double m_dBarWidth;
    double m_dBarBgWidth;
    double m_dBarStart;
    double m_dBarEnd;
    double m_dBarBgStart;
    double m_dBarBgEnd;
    double m_dBarAxisPos;
    bool m_bBarUnipolar;
    QColor m_barColor;
    QColor m_barBgColor;
    Qt::PenCapStyle m_barPenCap;
    // Pointer to pixmap of the slider
    PaintablePointer m_pSlider;
    // Pointer to pixmap of the handle
    PaintablePointer m_pHandle;
    SliderEventHandler<WSliderComposed> m_handler;
    // Non-linear response configured in the skin (applied when the optional
    // toggle control, if any, is on). m_dConfiguredExponent 1.0 = linear.
    double m_dConfiguredExponent;
    double m_dConfiguredCenter;
    // Optional control that toggles the non-linear response on/off at runtime.
    ControlProxy* m_pExponentToggle;
    // Asymmetric rate clamp configured in the skin, in percent of playback
    // speed (e.g. -32 / +8). Active when min < max; the reachable window is
    // derived from the connected deck's rateRange and rate_dir at runtime.
    double m_dRateClampMinPercent;
    double m_dRateClampMaxPercent;
    ControlProxy* m_pRateRangeControl;
    ControlProxy* m_pRateDirControl;
    // Optional control (bound to a skin-settings toggle) that switches the
    // asymmetric clamp on/off at runtime. Without it the clamp is static.
    ControlProxy* m_pRateClampToggle;

    friend class SliderEventHandler<WSliderComposed>;
};
