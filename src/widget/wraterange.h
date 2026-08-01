#pragma once

#include "widget/wnumber.h"

class ControlProxy;

enum class VerticalPosition {
    Top,
    Bottom
};

enum class DisplayType {
    Default,
    Prefix,
    Range
};

class WRateRange : public WNumber {
    Q_OBJECT
  public:
    explicit WRateRange(const QString& group, QWidget* parent = nullptr);
    void setup(const QDomNode& node, const SkinContext& context) override;

  private slots:
    void slotRateDirChanged(double dir);
    void slotClampToggleChanged(double v);
    void setValue(double range) override;

  private:
    ControlProxy* m_pRateRangeControl;
    ControlProxy* m_pRateDirControl;
    VerticalPosition m_nodePosition;
    DisplayType m_nodeDisplay;
    QString m_nodeText;
    // Optional asymmetric clamp (percent of playback speed) matching the
    // tempo slider's RateClampMin/MaxPercent. Active when min < max.
    double m_dClampMinPercent = 0.0;
    double m_dClampMaxPercent = 0.0;
    // Optional runtime on/off toggle matching the slider's RateClampControl.
    ControlProxy* m_pClampToggle = nullptr;
};
