#include "widget/wtempospinbox.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QWheelEvent>

#include "control/controlobject.h"
#include "moc_wtempospinbox.cpp"
#include "skin/legacy/skincontext.h"
#include "widget/wbeatspinbox.h"

WTempoSpinBox::WTempoSpinBox(QWidget* parent, const QString& group)
        : QDoubleSpinBox(parent),
          WBaseWidget(this),
          m_bpmControl(ConfigKey(group, QStringLiteral("bpm")),
                  this,
                  ControlFlag::NoAssertIfMissing),
          m_tempoTapControl(ConfigKey(group, QStringLiteral("tempo_tap")),
                  this,
                  ControlFlag::NoAssertIfMissing),
          m_bpmTapControl(ConfigKey(group, QStringLiteral("bpm_tap")),
                  this,
                  ControlFlag::NoAssertIfMissing),
          m_editing(false),
          m_scaleFactor(1.0) {
    // reuse the font-scaling line edit from WBeatSpinBox
    setLineEdit(new WBeatLineEdit(this));
    setDecimals(2);
    setRange(0.0, 999.0);
    setKeyboardTracking(false);
    setButtonSymbols(QAbstractSpinBox::NoButtons);
    setAlignment(Qt::AlignCenter);
    // Right click must reach mousePressEvent to tap bpm_tap.
    setContextMenuPolicy(Qt::NoContextMenu);
    // Read-only display until the user double-clicks into it; also keeps
    // the widget from grabbing keyboard focus (and thereby the app-wide
    // keyboard shortcuts) during normal use.
    setReadOnly(true);
    setFocusPolicy(Qt::NoFocus);
    lineEdit()->setFocusPolicy(Qt::NoFocus);

    setValue(m_bpmControl.get());
    m_bpmControl.connectValueChanged(this, &WTempoSpinBox::slotControlValueChanged);
}

void WTempoSpinBox::setup(const QDomNode& node, const SkinContext& context) {
    Q_UNUSED(node);
    m_scaleFactor = context.getScaleFactor();
    qobject_cast<WBeatLineEdit*>(lineEdit())->setScaleFactor(m_scaleFactor);
}

void WTempoSpinBox::slotControlValueChanged(double newValue) {
    if (!m_editing && value() != newValue) {
        setValue(newValue);
    }
}

void WTempoSpinBox::beginEdit() {
    m_editing = true;
    setReadOnly(false);
    setFocusPolicy(Qt::ClickFocus);
    lineEdit()->setFocusPolicy(Qt::ClickFocus);
    setFocus(Qt::MouseFocusReason);
    lineEdit()->selectAll();
}

void WTempoSpinBox::finishEdit(bool tookFocus) {
    // must be cleared before clearFocus(), the resulting focusOutEvent
    // would otherwise run the cancel path
    m_editing = false;
    setReadOnly(true);
    setFocusPolicy(Qt::NoFocus);
    lineEdit()->setFocusPolicy(Qt::NoFocus);
    if (tookFocus) {
        clearFocus();
    }
    // show what the engine actually settled on (rate range may clamp)
    setValue(m_bpmControl.get());
}

void WTempoSpinBox::commitEdit() {
    interpretText();
    const double newBpm = value();
    // 0 would mean "stop"; an empty/zero entry is treated as a cancel.
    // Setting the control moves the rate slider exactly like a mapped
    // BPM knob would (BpmControl::slotUpdateRateSlider).
    if (newBpm > 0 && m_bpmControl.get() > 0) {
        m_bpmControl.set(newBpm);
    }
}

bool WTempoSpinBox::event(QEvent* pEvent) {
    if (pEvent->type() == QEvent::ToolTip) {
        updateTooltip();
    } else if (pEvent->type() == QEvent::FontChange) {
        const QFont& fonti = font();
        // Only scale pixel size fonts, point size fonts are scaled by the OS.
        // This font instance is only used for size measuring in
        // QAbstractSpinBox::minimumSizeHint(), the lineEdit()->font() is
        // used for rendering (see WBeatSpinBox).
        if (fonti.pixelSize() > 0) {
            const_cast<QFont&>(fonti).setPixelSize(
                    static_cast<int>(fonti.pixelSize() * m_scaleFactor));
        }
    }
    return QDoubleSpinBox::event(pEvent);
}

void WTempoSpinBox::keyPressEvent(QKeyEvent* pEvent) {
    if (!m_editing) {
        QDoubleSpinBox::keyPressEvent(pEvent);
        return;
    }
    if (pEvent->key() == Qt::Key_Return || pEvent->key() == Qt::Key_Enter) {
        commitEdit();
        finishEdit(true);
        ControlObject::set(ConfigKey("[Library]", "refocus_prev_widget"), 1);
        return;
    }
    if (pEvent->key() == Qt::Key_Escape) {
        finishEdit(true);
        ControlObject::set(ConfigKey("[Library]", "refocus_prev_widget"), 1);
        return;
    }
    QDoubleSpinBox::keyPressEvent(pEvent);
}

void WTempoSpinBox::mousePressEvent(QMouseEvent* pEvent) {
    if (!m_editing) {
        // behave like the invisible tap button the old skin stacked on top
        // of the BPM number
        if (pEvent->button() == Qt::LeftButton) {
            m_tempoTapControl.set(1);
        } else if (pEvent->button() == Qt::RightButton) {
            m_bpmTapControl.set(1);
        }
        pEvent->accept();
        return;
    }
    QDoubleSpinBox::mousePressEvent(pEvent);
}

void WTempoSpinBox::mouseReleaseEvent(QMouseEvent* pEvent) {
    if (!m_editing) {
        if (pEvent->button() == Qt::LeftButton) {
            m_tempoTapControl.set(0);
        } else if (pEvent->button() == Qt::RightButton) {
            m_bpmTapControl.set(0);
        }
        pEvent->accept();
        return;
    }
    QDoubleSpinBox::mouseReleaseEvent(pEvent);
}

void WTempoSpinBox::mouseDoubleClickEvent(QMouseEvent* pEvent) {
    if (!m_editing && pEvent->button() == Qt::LeftButton) {
        beginEdit();
        pEvent->accept();
        return;
    }
    QDoubleSpinBox::mouseDoubleClickEvent(pEvent);
}

void WTempoSpinBox::wheelEvent(QWheelEvent* pEvent) {
    // never let a stray scroll change the tempo
    pEvent->ignore();
}

void WTempoSpinBox::focusOutEvent(QFocusEvent* pEvent) {
    if (m_editing) {
        // clicking elsewhere cancels instead of committing a half-typed BPM
        finishEdit(false);
    }
    QDoubleSpinBox::focusOutEvent(pEvent);
}
