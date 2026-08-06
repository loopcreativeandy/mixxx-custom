#pragma once

#include <QDoubleSpinBox>

#include "control/controlproxy.h"
#include "widget/wbasewidget.h"

class QDomNode;
class SkinContext;

/// Deck BPM readout that can be edited: right-click, type a BPM, Enter, and
/// the playback rate moves so the loaded track plays at that tempo (it sets
/// the [ChannelN],bpm control, the same path a MIDI BPM knob uses).
/// Left click is untouched and still taps tempo_tap, exactly like the
/// invisible tap button the skin used to stack on top of the number —
/// that is the primary way this readout is used, editing is the exception.
/// The BPM is always shown with a decimal point, never a locale comma.
class WTempoSpinBox : public QDoubleSpinBox, public WBaseWidget {
    Q_OBJECT
  public:
    WTempoSpinBox(QWidget* parent, const QString& group);

    void setup(const QDomNode& node, const SkinContext& context);

  private slots:
    void slotControlValueChanged(double newValue);

  private:
    void setLineEditClickThrough(bool clickThrough);
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
    QValidator::State validate(QString& input, int& pos) const override;
    double valueFromText(const QString& text) const override;

    ControlProxy m_bpmControl;
    ControlProxy m_tempoTapControl;
    bool m_editing;
    double m_scaleFactor;
};
