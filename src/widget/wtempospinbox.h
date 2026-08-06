#pragma once

#include <QDoubleSpinBox>

#include "control/controlproxy.h"
#include "widget/wbasewidget.h"

class QDomNode;
class SkinContext;

/// Deck BPM readout that can be edited: double-click, type a BPM, Enter, and
/// the playback rate moves so the loaded track plays at that tempo (it sets
/// the [ChannelN],bpm control, the same path a MIDI BPM knob uses).
/// While not editing it behaves like the old display: a left click taps
/// tempo_tap, a right click taps bpm_tap.
class WTempoSpinBox : public QDoubleSpinBox, public WBaseWidget {
    Q_OBJECT
  public:
    WTempoSpinBox(QWidget* parent, const QString& group);

    void setup(const QDomNode& node, const SkinContext& context);

  private slots:
    void slotControlValueChanged(double newValue);

  private:
    void beginEdit();
    void finishEdit(bool tookFocus);
    void commitEdit();

    bool event(QEvent* pEvent) override;
    void keyPressEvent(QKeyEvent* pEvent) override;
    void mousePressEvent(QMouseEvent* pEvent) override;
    void mouseReleaseEvent(QMouseEvent* pEvent) override;
    void mouseDoubleClickEvent(QMouseEvent* pEvent) override;
    void wheelEvent(QWheelEvent* pEvent) override;
    void focusOutEvent(QFocusEvent* pEvent) override;

    ControlProxy m_bpmControl;
    ControlProxy m_tempoTapControl;
    ControlProxy m_bpmTapControl;
    bool m_editing;
    double m_scaleFactor;
};
